#ifndef RTBPF_PORT_STM32H5_H
#define RTBPF_PORT_STM32H5_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int rtbpf_stm32h5_cycles_init(void);
uint32_t rtbpf_stm32h5_cycles_now(void *context);
int rtbpf_stm32h5_is_in_isr(void *context);
int rtbpf_stm32h5_prepare_rx(void *context, const uint8_t *data, uint32_t length);

#ifdef __cplusplus
}
#endif
#endif
