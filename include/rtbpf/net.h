#ifndef RTBPF_NET_H
#define RTBPF_NET_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    RTBPF_LINK_WIFI_ETHERNET = 1,
    RTBPF_LINK_ETHERNET = 2,
    RTBPF_LINK_RAW_IP = 3,
} rtbpf_link_type_t;

typedef enum {
    RTBPF_NET_ABORTED = 0,
    RTBPF_NET_DROP = 1,
    RTBPF_NET_PASS = 2,
} rtbpf_net_action_t;

/* Stable VM-visible ABI. data/data_end are capabilities, not native addresses. */
typedef struct __attribute__((packed)) {
    uint32_t data;
    uint32_t data_end;
    uint32_t ingress_port;
    uint32_t link_type;
    uint32_t packet_length;
    uint32_t window_epoch;
    int32_t rssi_dbm;
    uint32_t channel;
    uint64_t timestamp_ns;
} rtbpf_net_md_v1_t;

_Static_assert(offsetof(rtbpf_net_md_v1_t, data) == 0u, "data ABI offset");
_Static_assert(offsetof(rtbpf_net_md_v1_t, data_end) == 4u, "data_end ABI offset");
_Static_assert(offsetof(rtbpf_net_md_v1_t, timestamp_ns) == 32u, "timestamp ABI offset");
_Static_assert(sizeof(rtbpf_net_md_v1_t) == 40u, "NET_MD_V1 ABI size");

typedef struct {
    const uint8_t *data;
    uint32_t length;
    uint32_t ingress_port;
    uint32_t link_type;
    uint32_t window_epoch;
    int32_t rssi_dbm;
    uint32_t channel;
    uint64_t timestamp_ns;
    void *private_buffer_handle; /* trusted adapter only; never VM-visible */
} rtbpf_net_packet_t;

#ifdef __cplusplus
}
#endif
#endif
