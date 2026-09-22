#include "rtbpf/rx_adapter.h"

#include <string.h>

static int valid_link_type(uint32_t link_type)
{
    return link_type == RTBPF_LINK_WIFI_ETHERNET ||
           link_type == RTBPF_LINK_ETHERNET ||
           link_type == RTBPF_LINK_RAW_IP;
}

static void release_owned(rtbpf_rx_adapter_t *adapter, void *buffer_handle)
{
    adapter->release(adapter->callback_context, buffer_handle);
    adapter->stats.releases++;
}

int rtbpf_rx_adapter_init(rtbpf_rx_adapter_t *adapter,
                          rtbpf_net_hook_t *hook,
                          rtbpf_rx_handoff_fn handoff,
                          rtbpf_rx_release_fn release,
                          void *callback_context,
                          uint32_t ingress_port,
                          uint32_t link_type,
                          uint32_t maximum_frame_length)
{
    if (adapter == NULL || hook == NULL || handoff == NULL || release == NULL ||
        !valid_link_type(link_type) || maximum_frame_length == 0u) {
        return -1;
    }
    memset(adapter, 0, sizeof(*adapter));
    adapter->hook = hook;
    adapter->handoff = handoff;
    adapter->release = release;
    adapter->callback_context = callback_context;
    adapter->ingress_port = ingress_port;
    adapter->link_type = link_type;
    adapter->maximum_frame_length = maximum_frame_length;
    adapter->enabled = 1u;
    return 0;
}

void rtbpf_rx_adapter_set_platform(rtbpf_rx_adapter_t *adapter,
                                   rtbpf_cycles_now_fn cycles_now,
                                   rtbpf_is_in_isr_fn is_in_isr,
                                   rtbpf_rx_prepare_fn prepare_rx)
{
    if (adapter == NULL) return;
    adapter->cycles_now = cycles_now;
    adapter->is_in_isr = is_in_isr;
    adapter->prepare_rx = prepare_rx;
}

void rtbpf_rx_adapter_set_enabled(rtbpf_rx_adapter_t *adapter, int enabled)
{
    if (adapter != NULL) adapter->enabled = enabled != 0 ? 1u : 0u;
}

rtbpf_rx_result_t rtbpf_rx_adapter_process(rtbpf_rx_adapter_t *adapter,
                                           void *buffer_handle,
                                           const uint8_t *data,
                                           uint32_t length,
                                           uint32_t window_epoch,
                                           int32_t rssi_dbm,
                                           uint32_t channel,
                                           uint64_t timestamp_ns)
{
    if (adapter == NULL || adapter->release == NULL || adapter->handoff == NULL ||
        adapter->hook == NULL) {
        return RTBPF_RX_INVALID_FRAME;
    }

    adapter->stats.received++;
    adapter->stats.received_bytes += length;
    if (buffer_handle == NULL || data == NULL || length == 0u ||
        length > adapter->maximum_frame_length) {
        adapter->stats.invalid_frames++;
        if (buffer_handle != NULL) release_owned(adapter, buffer_handle);
        return RTBPF_RX_INVALID_FRAME;
    }
    if (adapter->is_in_isr != NULL &&
        adapter->is_in_isr(adapter->callback_context) != 0) {
        adapter->stats.wrong_context++;
        release_owned(adapter, buffer_handle);
        return RTBPF_RX_WRONG_CONTEXT;
    }
    if (adapter->enabled == 0u) {
        adapter->stats.disabled_drops++;
        release_owned(adapter, buffer_handle);
        return RTBPF_RX_DISABLED;
    }
    if (adapter->prepare_rx != NULL &&
        adapter->prepare_rx(adapter->callback_context, data, length) != 0) {
        adapter->stats.prepare_failures++;
        release_owned(adapter, buffer_handle);
        return RTBPF_RX_INVALID_FRAME;
    }

    rtbpf_net_packet_t packet;
    memset(&packet, 0, sizeof(packet));
    packet.data = data;
    packet.length = length;
    packet.ingress_port = adapter->ingress_port;
    packet.link_type = adapter->link_type;
    packet.window_epoch = window_epoch;
    packet.rssi_dbm = rssi_dbm;
    packet.channel = channel;
    packet.timestamp_ns = timestamp_ns;
    packet.private_buffer_handle = buffer_handle;

    const uint64_t aborts_before = adapter->hook->stats.aborted;
    uint32_t cycle_start = 0u;
    if (adapter->cycles_now != NULL)
        cycle_start = adapter->cycles_now(adapter->callback_context);
    const rtbpf_net_action_t action = rtbpf_net_hook_run(adapter->hook, &packet);
    if (adapter->cycles_now != NULL) {
        const uint32_t elapsed = adapter->cycles_now(adapter->callback_context) - cycle_start;
        adapter->stats.cycle_samples++;
        adapter->stats.total_cycles += elapsed;
        if (elapsed > adapter->stats.maximum_cycles)
            adapter->stats.maximum_cycles = elapsed;
    }
    adapter->stats.vm_aborts += adapter->hook->stats.aborted - aborts_before;

    if (action == RTBPF_NET_PASS) {
        const rtbpf_handoff_result_t handoff = adapter->handoff(
            adapter->callback_context, buffer_handle, data, length);
        if (handoff == RTBPF_HANDOFF_TAKEN) {
            adapter->stats.forwarded++;
            return RTBPF_RX_FORWARDED;
        }
        adapter->stats.handoff_failed++;
        release_owned(adapter, buffer_handle);
        return RTBPF_RX_HANDOFF_FAILED;
    }

    adapter->stats.filter_dropped++;
    release_owned(adapter, buffer_handle);
    return RTBPF_RX_FILTER_DROPPED;
}
