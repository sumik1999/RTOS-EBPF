#include "rtbpf_port_stm32h5.h"

#include "stm32h5xx.h"

#include <limits.h>
#include <stddef.h>
#include <stdint.h>

#ifndef RTBPF_STM32_RX_NEEDS_DCACHE_INVALIDATE
#define RTBPF_STM32_RX_NEEDS_DCACHE_INVALIDATE 0
#endif

int rtbpf_stm32h5_cycles_init(void)
{
#if defined(DWT) && defined(CoreDebug) && defined(DWT_CTRL_CYCCNTENA_Msk)
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0u;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    __DSB();
    __ISB();
    return (DWT->CTRL & DWT_CTRL_CYCCNTENA_Msk) != 0u ? 0 : -1;
#else
    return -1;
#endif
}

uint32_t rtbpf_stm32h5_cycles_now(void *context)
{
    (void)context;
#if defined(DWT)
    return DWT->CYCCNT;
#else
    return 0u;
#endif
}

int rtbpf_stm32h5_is_in_isr(void *context)
{
    (void)context;
    return __get_IPSR() != 0u ? 1 : 0;
}

int rtbpf_stm32h5_prepare_rx(void *context, const uint8_t *data, uint32_t length)
{
    (void)context;
    if (data == NULL || length == 0u) return -1;

#if RTBPF_STM32_RX_NEEDS_DCACHE_INVALIDATE && defined(__DCACHE_PRESENT) && (__DCACHE_PRESENT == 1U)
    const uintptr_t address = (uintptr_t)data;
    if (address > UINTPTR_MAX - (uintptr_t)length - 31u) return -1;
    const uintptr_t begin = address & ~(uintptr_t)31u;
    const uintptr_t end = (address + length + 31u) & ~(uintptr_t)31u;
    const uintptr_t span = end - begin;
    if (span > (uintptr_t)INT32_MAX) return -1;
    SCB_InvalidateDCache_by_Addr((void *)begin, (int32_t)span);
#endif
    __DSB();
    return 0;
}
