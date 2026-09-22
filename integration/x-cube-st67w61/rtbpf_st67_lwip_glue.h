#ifndef RTBPF_ST67_LWIP_GLUE_H
#define RTBPF_ST67_LWIP_GLUE_H

#include <stdint.h>
#include "lwip/netif.h"
#include "rtbpf/program.h"
#include "rtbpf/st67_adapter.h"

#ifdef __cplusplus
extern "C" {
#endif

int rtbpf_st67_lwip_init(struct netif *sta_netif, struct netif *ap_netif,
                         const rtbpf_program_t *sta_program,
                         const rtbpf_program_t *ap_program);
int32_t rtbpf_st67_lwip_rx_process(uint32_t link_index);
void rtbpf_st67_lwip_set_enabled(int enabled);
const rtbpf_st67_adapter_t *rtbpf_st67_lwip_adapter(void);

#ifdef __cplusplus
}
#endif
#endif
