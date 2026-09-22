#include "rtbpf_st67_lwip_glue.h"

#include "FreeRTOS.h"
#include "task.h"
#include "lwip/err.h"
#include "lwip/pbuf.h"
#include "rtbpf/hook.h"
#include "rtbpf/net.h"
#include "rtbpf_port_stm32h5.h"
#include "w6x_api.h"

#include <limits.h>
#include <stddef.h>
#include <string.h>

#ifndef RTBPF_ST67_PBUF_SLOTS
#define RTBPF_ST67_PBUF_SLOTS 16u
#endif
#ifndef RTBPF_ST67_MAX_FRAME
#define RTBPF_ST67_MAX_FRAME 1520u
#endif

typedef struct {
    struct pbuf_custom custom;
    void *driver_buffer;
    uint8_t in_use;
} rtbpf_st67_pbuf_slot_t;

typedef struct {
    struct netif *netif;
} rtbpf_st67_interface_context_t;

static rtbpf_net_hook_t sta_hook;
static rtbpf_net_hook_t ap_hook;
static rtbpf_st67_adapter_t st67_adapter;
static rtbpf_st67_interface_context_t interface_context[RTBPF_ST67_INTERFACE_COUNT];
static rtbpf_st67_pbuf_slot_t pbuf_slots[RTBPF_ST67_PBUF_SLOTS];
static uint8_t glue_initialized;

static rtbpf_st67_pbuf_slot_t *slot_from_pbuf(struct pbuf *pb)
{
    const size_t pbuf_offset = offsetof(rtbpf_st67_pbuf_slot_t, custom) +
                               offsetof(struct pbuf_custom, pbuf);
    return (rtbpf_st67_pbuf_slot_t *)((uint8_t *)pb - pbuf_offset);
}

static rtbpf_st67_pbuf_slot_t *slot_allocate(void *driver_buffer)
{
    rtbpf_st67_pbuf_slot_t *result = NULL;
    taskENTER_CRITICAL();
    for (uint32_t i = 0u; i < RTBPF_ST67_PBUF_SLOTS; ++i) {
        if (pbuf_slots[i].in_use == 0u) {
            pbuf_slots[i].in_use = 1u;
            pbuf_slots[i].driver_buffer = driver_buffer;
            result = &pbuf_slots[i];
            break;
        }
    }
    taskEXIT_CRITICAL();
    return result;
}

static void slot_cancel(rtbpf_st67_pbuf_slot_t *slot)
{
    taskENTER_CRITICAL();
    slot->driver_buffer = NULL;
    slot->in_use = 0u;
    taskEXIT_CRITICAL();
}

static void st67_custom_pbuf_free(struct pbuf *pb)
{
    rtbpf_st67_pbuf_slot_t *slot = slot_from_pbuf(pb);
    void *driver_buffer = slot->driver_buffer;
    if (driver_buffer != NULL) {
        (void)W6X_Netif_free(driver_buffer);
    }
    slot_cancel(slot);
}

static void st67_release(void *context, void *buffer_handle)
{
    (void)context;
    (void)W6X_Netif_free(buffer_handle);
}

static rtbpf_handoff_result_t st67_handoff(void *context, void *buffer_handle,
                                           const uint8_t *data, uint32_t length)
{
    rtbpf_st67_interface_context_t *interface = context;
    if (interface == NULL || interface->netif == NULL) {
        return RTBPF_HANDOFF_REJECTED;
    }

    rtbpf_st67_pbuf_slot_t *slot = slot_allocate(buffer_handle);
    if (slot == NULL) {
        return RTBPF_HANDOFF_REJECTED;
    }
    memset(&slot->custom, 0, sizeof(slot->custom));
    slot->custom.custom_free_function = st67_custom_pbuf_free;
    struct pbuf *pb = pbuf_alloced_custom(PBUF_RAW, (u16_t)length, PBUF_REF,
                                          &slot->custom, (void *)(uintptr_t)data,
                                          (u16_t)length);
    if (pb == NULL) {
        slot_cancel(slot);
        return RTBPF_HANDOFF_REJECTED;
    }

    if (interface->netif->input(pb, interface->netif) != ERR_OK) {
        /* pbuf_free invokes st67_custom_pbuf_free: ownership is consumed here. */
        (void)pbuf_free(pb);
    }
    return RTBPF_HANDOFF_TAKEN;
}

int rtbpf_st67_lwip_init(struct netif *sta_netif, struct netif *ap_netif,
                         const rtbpf_program_t *sta_program,
                         const rtbpf_program_t *ap_program)
{
    if (glue_initialized != 0u || sta_netif == NULL) return -1;
    memset(pbuf_slots, 0, sizeof(pbuf_slots));
    interface_context[RTBPF_ST67_STA_INDEX].netif = sta_netif;
    interface_context[RTBPF_ST67_AP_INDEX].netif = ap_netif;

    rtbpf_net_hook_init(&sta_hook, RTBPF_LINK_WIFI_ETHERNET, RTBPF_NET_DROP);
    rtbpf_net_hook_init(&ap_hook, RTBPF_LINK_WIFI_ETHERNET, RTBPF_NET_DROP);
    if ((sta_program != NULL && rtbpf_net_hook_attach_static(&sta_hook, sta_program) != 0) ||
        (ap_program != NULL && rtbpf_net_hook_attach_static(&ap_hook, ap_program) != 0)) {
        return -1;
    }
    if (rtbpf_st67_adapter_init(&st67_adapter, &sta_hook, &ap_hook,
                                st67_handoff, st67_release,
                                &interface_context[RTBPF_ST67_STA_INDEX],
                                &interface_context[RTBPF_ST67_AP_INDEX],
                                RTBPF_ST67_MAX_FRAME) != 0) {
        return -1;
    }
    const rtbpf_cycles_now_fn cycle_fn = rtbpf_stm32h5_cycles_init() == 0 ?
                                          rtbpf_stm32h5_cycles_now : NULL;
    rtbpf_st67_adapter_set_platform(&st67_adapter, cycle_fn,
                                    rtbpf_stm32h5_is_in_isr,
                                    rtbpf_stm32h5_prepare_rx);
    glue_initialized = 1u;
    return 0;
}

int32_t rtbpf_st67_lwip_rx_process(uint32_t link_index)
{
    if (link_index >= RTBPF_ST67_INTERFACE_COUNT) return -1;
    const uint32_t w6x_link = link_index == RTBPF_ST67_STA_INDEX ?
                              W6X_NET_IF_STA : W6X_NET_IF_AP;
    void *buffer = NULL;
    uint8_t *payload = NULL;
    const int32_t length = W6X_Netif_input(w6x_link, &buffer, &payload);
    if (length <= 0) {
        if (buffer != NULL) (void)W6X_Netif_free(buffer);
        return length;
    }

    const TickType_t tick = xTaskGetTickCount();
    const uint32_t epoch = (uint32_t)(tick / configTICK_RATE_HZ);
    (void)rtbpf_st67_adapter_process(&st67_adapter, link_index, buffer, payload,
                                     (uint32_t)length, epoch, INT32_MIN, 0u, 0u);
    /* Return the positive length even when filtered so the ST NET_IF task drains its queue. */
    return length;
}

void rtbpf_st67_lwip_set_enabled(int enabled)
{
    rtbpf_st67_adapter_set_enabled(&st67_adapter, enabled);
}

const rtbpf_st67_adapter_t *rtbpf_st67_lwip_adapter(void)
{
    return &st67_adapter;
}
