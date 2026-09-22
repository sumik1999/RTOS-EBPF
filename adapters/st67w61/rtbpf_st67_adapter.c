#include "rtbpf/st67_adapter.h"

#include <string.h>

int rtbpf_st67_adapter_init(rtbpf_st67_adapter_t *adapter,
                            rtbpf_net_hook_t *sta_hook,
                            rtbpf_net_hook_t *ap_hook,
                            rtbpf_rx_handoff_fn handoff,
                            rtbpf_rx_release_fn release,
                            void *sta_callback_context,
                            void *ap_callback_context,
                            uint32_t maximum_frame_length)
{
    if (adapter == NULL || sta_hook == NULL || ap_hook == NULL ||
        handoff == NULL || release == NULL || maximum_frame_length == 0u) {
        return -1;
    }
    memset(adapter, 0, sizeof(*adapter));
    adapter->release = release;
    adapter->release_context = sta_callback_context;
    if (rtbpf_rx_adapter_init(&adapter->interface[RTBPF_ST67_STA_INDEX],
                              sta_hook, handoff, release, sta_callback_context,
                              1u, RTBPF_LINK_WIFI_ETHERNET,
                              maximum_frame_length) != 0 ||
        rtbpf_rx_adapter_init(&adapter->interface[RTBPF_ST67_AP_INDEX],
                              ap_hook, handoff, release, ap_callback_context,
                              2u, RTBPF_LINK_WIFI_ETHERNET,
                              maximum_frame_length) != 0) {
        memset(adapter, 0, sizeof(*adapter));
        return -1;
    }
    return 0;
}

void rtbpf_st67_adapter_set_platform(rtbpf_st67_adapter_t *adapter,
                                     rtbpf_cycles_now_fn cycles_now,
                                     rtbpf_is_in_isr_fn is_in_isr,
                                     rtbpf_rx_prepare_fn prepare_rx)
{
    if (adapter == NULL) return;
    for (uint32_t i = 0u; i < RTBPF_ST67_INTERFACE_COUNT; ++i) {
        rtbpf_rx_adapter_set_platform(&adapter->interface[i], cycles_now,
                                      is_in_isr, prepare_rx);
    }
}

void rtbpf_st67_adapter_set_enabled(rtbpf_st67_adapter_t *adapter, int enabled)
{
    if (adapter == NULL) return;
    for (uint32_t i = 0u; i < RTBPF_ST67_INTERFACE_COUNT; ++i)
        rtbpf_rx_adapter_set_enabled(&adapter->interface[i], enabled);
}

rtbpf_rx_result_t rtbpf_st67_adapter_process(rtbpf_st67_adapter_t *adapter,
                                             uint32_t link_index,
                                             void *buffer_handle,
                                             const uint8_t *ethernet_frame,
                                             uint32_t length,
                                             uint32_t window_epoch,
                                             int32_t rssi_dbm,
                                             uint32_t channel,
                                             uint64_t timestamp_ns)
{
    if (adapter == NULL || adapter->release == NULL) return RTBPF_RX_INVALID_FRAME;
    if (link_index >= RTBPF_ST67_INTERFACE_COUNT) {
        adapter->unknown_link_drops++;
        if (buffer_handle != NULL)
            adapter->release(adapter->release_context, buffer_handle);
        return RTBPF_RX_INVALID_FRAME;
    }
    return rtbpf_rx_adapter_process(&adapter->interface[link_index], buffer_handle,
                                    ethernet_frame, length, window_epoch,
                                    rssi_dbm, channel, timestamp_ns);
}
