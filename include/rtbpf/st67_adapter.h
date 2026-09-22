#ifndef RTBPF_ST67_ADAPTER_H
#define RTBPF_ST67_ADAPTER_H

#include <stdint.h>
#include "rtbpf/rx_adapter.h"

#ifdef __cplusplus
extern "C" {
#endif

#define RTBPF_ST67_STA_INDEX 0u
#define RTBPF_ST67_AP_INDEX 1u
#define RTBPF_ST67_INTERFACE_COUNT 2u

typedef struct {
    rtbpf_rx_adapter_t interface[RTBPF_ST67_INTERFACE_COUNT];
    rtbpf_rx_release_fn release;
    void *release_context;
    uint32_t unknown_link_drops;
} rtbpf_st67_adapter_t;

int rtbpf_st67_adapter_init(rtbpf_st67_adapter_t *adapter,
                            rtbpf_net_hook_t *sta_hook,
                            rtbpf_net_hook_t *ap_hook,
                            rtbpf_rx_handoff_fn handoff,
                            rtbpf_rx_release_fn release,
                            void *sta_callback_context,
                            void *ap_callback_context,
                            uint32_t maximum_frame_length);
void rtbpf_st67_adapter_set_platform(rtbpf_st67_adapter_t *adapter,
                                     rtbpf_cycles_now_fn cycles_now,
                                     rtbpf_is_in_isr_fn is_in_isr,
                                     rtbpf_rx_prepare_fn prepare_rx);
void rtbpf_st67_adapter_set_enabled(rtbpf_st67_adapter_t *adapter, int enabled);
rtbpf_rx_result_t rtbpf_st67_adapter_process(rtbpf_st67_adapter_t *adapter,
                                             uint32_t link_index,
                                             void *buffer_handle,
                                             const uint8_t *ethernet_frame,
                                             uint32_t length,
                                             uint32_t window_epoch,
                                             int32_t rssi_dbm,
                                             uint32_t channel,
                                             uint64_t timestamp_ns);

#ifdef __cplusplus
}
#endif
#endif
