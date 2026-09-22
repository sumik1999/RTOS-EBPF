#include "rtbpf_st67_lwip_glue.h"
#include "static_programs.h"

#include "FreeRTOS.h"
#include "lwip/err.h"
#include "lwip/pbuf.h"

#include <stdio.h>
#include <string.h>

static unsigned checks, failures;
#define CHECK(x) do { checks++; if (!(x)) { failures++; \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); } } while (0)

typedef struct { unsigned frees; } mock_w6_buffer_t;
static void *next_buffer;
static uint8_t *next_payload;
static int32_t next_length;
static int input_reject;
static struct pbuf *accepted[32];
static unsigned accepted_count;
static uint32_t fake_cycles;

TickType_t xTaskGetTickCount(void) { return 5000u; }
int rtbpf_stm32h5_cycles_init(void) { return 0; }
uint32_t rtbpf_stm32h5_cycles_now(void *context)
{
    (void)context; fake_cycles += 25u; return fake_cycles;
}
int rtbpf_stm32h5_is_in_isr(void *context) { (void)context; return 0; }
int rtbpf_stm32h5_prepare_rx(void *context, const uint8_t *data, uint32_t length)
{
    (void)context; return data != NULL && length != 0u ? 0 : -1;
}

int32_t W6X_Netif_input(uint32_t link_id, void **buffer, uint8_t **data)
{
    (void)link_id;
    *buffer = next_buffer;
    *data = next_payload;
    return next_length;
}

int32_t W6X_Netif_free(void *buffer)
{
    ((mock_w6_buffer_t *)buffer)->frees++;
    return 0;
}

struct pbuf *pbuf_alloced_custom(int layer, u16_t length, int type,
                                 struct pbuf_custom *custom, void *payload,
                                 u16_t payload_length)
{
    (void)layer; (void)type; (void)payload_length;
    custom->pbuf.payload = payload;
    custom->pbuf.len = length;
    custom->pbuf.tot_len = length;
    return &custom->pbuf;
}

u16_t pbuf_free(struct pbuf *pb)
{
    struct pbuf_custom *custom = (struct pbuf_custom *)pb;
    custom->custom_free_function(pb);
    return 1u;
}

static err_t mock_netif_input(struct pbuf *pb, struct netif *netif)
{
    (void)netif;
    if (input_reject) return ERR_IF;
    accepted[accepted_count++] = pb;
    return ERR_OK;
}

static int32_t process(mock_w6_buffer_t *buffer, uint8_t *payload, int32_t length)
{
    next_buffer = buffer;
    next_payload = payload;
    next_length = length;
    return rtbpf_st67_lwip_rx_process(RTBPF_ST67_STA_INDEX);
}

int main(void)
{
    struct netif sta = {.input = mock_netif_input};
    struct netif ap = {.input = mock_netif_input};
    CHECK(rtbpf_st67_lwip_init(&sta, &ap, &rtbpf_example_drop_ff_program,
                               &rtbpf_example_drop_ff_program) == 0);

    uint8_t pass[] = {0x45};
    uint8_t drop[] = {0xff};
    mock_w6_buffer_t pass_buffer = {0};
    CHECK(process(&pass_buffer, pass, 1) == 1);
    CHECK(pass_buffer.frees == 0u && accepted_count == 1u);
    CHECK(pbuf_free(accepted[--accepted_count]) == 1u);
    CHECK(pass_buffer.frees == 1u);

    mock_w6_buffer_t drop_buffer = {0};
    CHECK(process(&drop_buffer, drop, 1) == 1);
    CHECK(drop_buffer.frees == 1u && accepted_count == 0u);

    input_reject = 1;
    mock_w6_buffer_t rejected = {0};
    CHECK(process(&rejected, pass, 1) == 1);
    CHECK(rejected.frees == 1u && accepted_count == 0u);
    input_reject = 0;

    mock_w6_buffer_t pool_buffers[17];
    memset(pool_buffers, 0, sizeof(pool_buffers));
    for (unsigned i = 0; i < 17u; ++i)
        CHECK(process(&pool_buffers[i], pass, 1) == 1);
    CHECK(accepted_count == 16u);
    CHECK(pool_buffers[16].frees == 1u);
    for (unsigned i = 0; i < accepted_count; ++i)
        (void)pbuf_free(accepted[i]);
    accepted_count = 0u;
    for (unsigned i = 0; i < 17u; ++i)
        CHECK(pool_buffers[i].frees == 1u);

    rtbpf_st67_lwip_set_enabled(0);
    mock_w6_buffer_t disabled = {0};
    CHECK(process(&disabled, pass, 1) == 1);
    CHECK(disabled.frees == 1u && accepted_count == 0u);

    const rtbpf_st67_adapter_t *adapter = rtbpf_st67_lwip_adapter();
    CHECK(adapter->interface[RTBPF_ST67_STA_INDEX].stats.maximum_cycles == 25u);
    CHECK(adapter->interface[RTBPF_ST67_STA_INDEX].stats.handoff_failed == 1u);
    CHECK(adapter->interface[RTBPF_ST67_STA_INDEX].stats.disabled_drops == 1u);

    printf("ST67 glue tests: %u checks, %u failures\n", checks, failures);
    return failures == 0u ? 0 : 1;
}
