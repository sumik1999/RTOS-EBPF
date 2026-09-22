#ifndef RTBPF_RX_ADAPTER_H
#define RTBPF_RX_ADAPTER_H

#include <stdint.h>
#include "rtbpf/hook.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    /* The upper layer accepted ownership, or consumed/released it internally. */
    RTBPF_HANDOFF_TAKEN = 0,
    /* Ownership remains with the adapter and must be released by it. */
    RTBPF_HANDOFF_REJECTED = 1,
} rtbpf_handoff_result_t;

typedef rtbpf_handoff_result_t (*rtbpf_rx_handoff_fn)(
    void *context, void *buffer_handle, const uint8_t *data, uint32_t length);
typedef void (*rtbpf_rx_release_fn)(void *context, void *buffer_handle);
typedef uint32_t (*rtbpf_cycles_now_fn)(void *context);
typedef int (*rtbpf_is_in_isr_fn)(void *context);
typedef int (*rtbpf_rx_prepare_fn)(void *context, const uint8_t *data,
                                   uint32_t length);

typedef enum {
    RTBPF_RX_FORWARDED = 0,
    RTBPF_RX_FILTER_DROPPED,
    RTBPF_RX_HANDOFF_FAILED,
    RTBPF_RX_INVALID_FRAME,
    RTBPF_RX_WRONG_CONTEXT,
    RTBPF_RX_DISABLED,
} rtbpf_rx_result_t;

typedef struct {
    uint64_t received;
    uint64_t received_bytes;
    uint64_t forwarded;
    uint64_t filter_dropped;
    uint64_t handoff_failed;
    uint64_t invalid_frames;
    uint64_t prepare_failures;
    uint64_t wrong_context;
    uint64_t disabled_drops;
    uint64_t releases;
    uint64_t vm_aborts;
    uint64_t cycle_samples;
    uint64_t total_cycles;
    uint32_t maximum_cycles;
} rtbpf_rx_adapter_stats_t;

typedef struct {
    rtbpf_net_hook_t *hook;
    rtbpf_rx_handoff_fn handoff;
    rtbpf_rx_release_fn release;
    rtbpf_cycles_now_fn cycles_now;
    rtbpf_is_in_isr_fn is_in_isr;
    rtbpf_rx_prepare_fn prepare_rx;
    void *callback_context;
    uint32_t ingress_port;
    uint32_t link_type;
    uint32_t maximum_frame_length;
    uint8_t enabled;
    rtbpf_rx_adapter_stats_t stats;
} rtbpf_rx_adapter_t;

int rtbpf_rx_adapter_init(rtbpf_rx_adapter_t *adapter,
                          rtbpf_net_hook_t *hook,
                          rtbpf_rx_handoff_fn handoff,
                          rtbpf_rx_release_fn release,
                          void *callback_context,
                          uint32_t ingress_port,
                          uint32_t link_type,
                          uint32_t maximum_frame_length);
void rtbpf_rx_adapter_set_platform(rtbpf_rx_adapter_t *adapter,
                                   rtbpf_cycles_now_fn cycles_now,
                                   rtbpf_is_in_isr_fn is_in_isr,
                                   rtbpf_rx_prepare_fn prepare_rx);
void rtbpf_rx_adapter_set_enabled(rtbpf_rx_adapter_t *adapter, int enabled);
rtbpf_rx_result_t rtbpf_rx_adapter_process(rtbpf_rx_adapter_t *adapter,
                                           void *buffer_handle,
                                           const uint8_t *data,
                                           uint32_t length,
                                           uint32_t window_epoch,
                                           int32_t rssi_dbm,
                                           uint32_t channel,
                                           uint64_t timestamp_ns);

#ifdef __cplusplus
}
#endif
#endif
