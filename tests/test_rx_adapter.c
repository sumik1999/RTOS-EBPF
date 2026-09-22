#include "rtbpf/abi.h"
#include "rtbpf/hook.h"
#include "rtbpf/net.h"
#include "rtbpf/program.h"
#include "rtbpf/rx_adapter.h"
#include "rtbpf/st67_adapter.h"
#include "static_programs.h"

#include <stdio.h>
#include <string.h>

static unsigned checks;
static unsigned failures;
#define CHECK(x) do { checks++; if (!(x)) { failures++; \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); } } while (0)

typedef struct {
    unsigned terminal_transitions;
    unsigned handoffs;
    unsigned releases;
} mock_buffer_t;

typedef struct {
    int handoff_reject;
    int in_isr;
    int prepare_fail;
    uint32_t cycles;
    unsigned context_id;
    unsigned last_handoff_context;
} mock_context_t;

static rtbpf_handoff_result_t mock_handoff(void *opaque, void *handle,
                                           const uint8_t *data, uint32_t length)
{
    mock_context_t *context = opaque;
    mock_buffer_t *buffer = handle;
    (void)data; (void)length;
    buffer->handoffs++;
    context->last_handoff_context = context->context_id;
    if (context->handoff_reject) return RTBPF_HANDOFF_REJECTED;
    buffer->terminal_transitions++;
    return RTBPF_HANDOFF_TAKEN;
}

static void mock_release(void *opaque, void *handle)
{
    (void)opaque;
    mock_buffer_t *buffer = handle;
    buffer->releases++;
    buffer->terminal_transitions++;
}

static uint32_t mock_cycles(void *opaque)
{
    mock_context_t *context = opaque;
    context->cycles += 50u;
    return context->cycles;
}

static int mock_isr(void *opaque)
{
    return ((mock_context_t *)opaque)->in_isr;
}

static int mock_prepare(void *opaque, const uint8_t *data, uint32_t length)
{
    (void)data; (void)length;
    return ((mock_context_t *)opaque)->prepare_fail ? -1 : 0;
}

static void test_pass_drop_and_handoff_failure(void)
{
    rtbpf_net_hook_t hook;
    rtbpf_rx_adapter_t adapter;
    mock_context_t context = {0};
    const uint8_t pass_frame[] = {0x45};
    const uint8_t drop_frame[] = {0xff};

    rtbpf_net_hook_init(&hook, RTBPF_LINK_WIFI_ETHERNET, RTBPF_NET_DROP);
    CHECK(rtbpf_net_hook_attach_static(&hook, &rtbpf_example_drop_ff_program) == 0);
    CHECK(rtbpf_rx_adapter_init(&adapter, &hook, mock_handoff, mock_release,
                                &context, 1u, RTBPF_LINK_WIFI_ETHERNET, 1520u) == 0);
    rtbpf_rx_adapter_set_platform(&adapter, mock_cycles, mock_isr, mock_prepare);

    mock_buffer_t passed = {0};
    CHECK(rtbpf_rx_adapter_process(&adapter, &passed, pass_frame, sizeof(pass_frame),
                                   1u, -40, 6u, 10u) == RTBPF_RX_FORWARDED);
    CHECK(passed.terminal_transitions == 1u && passed.handoffs == 1u && passed.releases == 0u);

    mock_buffer_t dropped = {0};
    CHECK(rtbpf_rx_adapter_process(&adapter, &dropped, drop_frame, sizeof(drop_frame),
                                   1u, -40, 6u, 11u) == RTBPF_RX_FILTER_DROPPED);
    CHECK(dropped.terminal_transitions == 1u && dropped.releases == 1u && dropped.handoffs == 0u);

    context.handoff_reject = 1;
    mock_buffer_t rejected = {0};
    CHECK(rtbpf_rx_adapter_process(&adapter, &rejected, pass_frame, sizeof(pass_frame),
                                   1u, -40, 6u, 12u) == RTBPF_RX_HANDOFF_FAILED);
    CHECK(rejected.terminal_transitions == 1u && rejected.releases == 1u && rejected.handoffs == 1u);
    CHECK(adapter.stats.received == 3u && adapter.stats.forwarded == 1u);
    CHECK(adapter.stats.filter_dropped == 1u && adapter.stats.handoff_failed == 1u);
    CHECK(adapter.stats.cycle_samples == 3u && adapter.stats.maximum_cycles == 50u);
}

static void test_rejection_paths(void)
{
    rtbpf_net_hook_t hook;
    rtbpf_rx_adapter_t adapter;
    mock_context_t context = {0};
    const uint8_t frame[] = {0x45};
    rtbpf_net_hook_init(&hook, RTBPF_LINK_WIFI_ETHERNET, RTBPF_NET_DROP);
    CHECK(rtbpf_rx_adapter_init(&adapter, &hook, mock_handoff, mock_release,
                                &context, 1u, RTBPF_LINK_WIFI_ETHERNET, 4u) == 0);
    rtbpf_rx_adapter_set_platform(&adapter, mock_cycles, mock_isr, mock_prepare);

    mock_buffer_t oversized = {0};
    CHECK(rtbpf_rx_adapter_process(&adapter, &oversized, frame, 5u, 0u, 0, 0u, 0u) ==
          RTBPF_RX_INVALID_FRAME);
    CHECK(oversized.terminal_transitions == 1u);

    context.in_isr = 1;
    mock_buffer_t isr = {0};
    CHECK(rtbpf_rx_adapter_process(&adapter, &isr, frame, 1u, 0u, 0, 0u, 0u) ==
          RTBPF_RX_WRONG_CONTEXT);
    CHECK(isr.terminal_transitions == 1u);
    context.in_isr = 0;

    context.prepare_fail = 1;
    mock_buffer_t prepare = {0};
    CHECK(rtbpf_rx_adapter_process(&adapter, &prepare, frame, 1u, 0u, 0, 0u, 0u) ==
          RTBPF_RX_INVALID_FRAME);
    CHECK(prepare.terminal_transitions == 1u && adapter.stats.prepare_failures == 1u);
    context.prepare_fail = 0;

    rtbpf_rx_adapter_set_enabled(&adapter, 0);
    mock_buffer_t disabled = {0};
    CHECK(rtbpf_rx_adapter_process(&adapter, &disabled, frame, 1u, 0u, 0, 0u, 0u) ==
          RTBPF_RX_DISABLED);
    CHECK(disabled.terminal_transitions == 1u);
}

static void test_runtime_fault_is_fail_closed(void)
{
    rtbpf_insn_t mutable_insns[] = {
        RTBPF_MOV64_IMM(0, RTBPF_NET_PASS), RTBPF_EXIT(),
    };
    rtbpf_program_t program = {
        .id = 300u, .instructions = mutable_insns, .instruction_count = 2u,
        .maximum_executed_instructions = 2u, .stack_size = RTBPF_DEFAULT_STACK_SIZE,
    };
    rtbpf_net_hook_t hook;
    rtbpf_rx_adapter_t adapter;
    mock_context_t context = {0};
    const uint8_t frame[] = {0x45};
    rtbpf_net_hook_init(&hook, RTBPF_LINK_WIFI_ETHERNET, RTBPF_NET_DROP);
    CHECK(rtbpf_net_hook_attach_static(&hook, &program) == 0);
    mutable_insns[0].opcode = 0xfe; /* Simulates corruption after verification. */
    CHECK(rtbpf_rx_adapter_init(&adapter, &hook, mock_handoff, mock_release,
                                &context, 1u, RTBPF_LINK_WIFI_ETHERNET, 1520u) == 0);
    mock_buffer_t buffer = {0};
    CHECK(rtbpf_rx_adapter_process(&adapter, &buffer, frame, 1u, 0u, 0, 0u, 0u) ==
          RTBPF_RX_FILTER_DROPPED);
    CHECK(buffer.terminal_transitions == 1u && adapter.stats.vm_aborts == 1u);
}

static void test_st67_interface_mapping(void)
{
    rtbpf_net_hook_t sta_hook, ap_hook;
    rtbpf_st67_adapter_t adapter;
    mock_context_t sta = {.context_id = 10u};
    mock_context_t ap = {.context_id = 20u};
    const uint8_t frame[] = {0x45};
    rtbpf_net_hook_init(&sta_hook, RTBPF_LINK_WIFI_ETHERNET, RTBPF_NET_DROP);
    rtbpf_net_hook_init(&ap_hook, RTBPF_LINK_WIFI_ETHERNET, RTBPF_NET_DROP);
    CHECK(rtbpf_st67_adapter_init(&adapter, &sta_hook, &ap_hook,
                                  mock_handoff, mock_release, &sta, &ap, 1520u) == 0);

    mock_buffer_t sta_buffer = {0};
    CHECK(rtbpf_st67_adapter_process(&adapter, RTBPF_ST67_STA_INDEX, &sta_buffer,
                                     frame, 1u, 0u, 0, 0u, 0u) == RTBPF_RX_FORWARDED);
    CHECK(sta.last_handoff_context == 10u && ap.last_handoff_context == 0u);

    mock_buffer_t ap_buffer = {0};
    CHECK(rtbpf_st67_adapter_process(&adapter, RTBPF_ST67_AP_INDEX, &ap_buffer,
                                     frame, 1u, 0u, 0, 0u, 0u) == RTBPF_RX_FORWARDED);
    CHECK(ap.last_handoff_context == 20u);

    mock_buffer_t unknown = {0};
    CHECK(rtbpf_st67_adapter_process(&adapter, 9u, &unknown, frame, 1u,
                                     0u, 0, 0u, 0u) == RTBPF_RX_INVALID_FRAME);
    CHECK(unknown.terminal_transitions == 1u && adapter.unknown_link_drops == 1u);
}

int main(void)
{
    test_pass_drop_and_handoff_failure();
    test_rejection_paths();
    test_runtime_fault_is_fail_closed();
    test_st67_interface_mapping();
    printf("RX adapter tests: %u checks, %u failures\n", checks, failures);
    return failures == 0u ? 0 : 1;
}
