#include "rtbpf/abi.h"
#include "rtbpf/helpers.h"
#include "rtbpf/hook.h"
#include "rtbpf/interpreter.h"
#include "rtbpf/maps.h"
#include "rtbpf/net.h"
#include "rtbpf/program.h"
#include "rtbpf/verifier.h"
#include "static_programs.h"

#include <stdio.h>
#include <string.h>

static unsigned tests_run;
static unsigned tests_failed;

#define CHECK(expr) do { \
    tests_run++; \
    if (!(expr)) { \
        tests_failed++; \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); \
    } \
} while (0)

static rtbpf_net_packet_t packet_of(const uint8_t *bytes, uint32_t length)
{
    rtbpf_net_packet_t packet;
    memset(&packet, 0, sizeof(packet));
    packet.data = bytes;
    packet.length = length;
    packet.ingress_port = 1u;
    packet.link_type = RTBPF_LINK_WIFI_ETHERNET;
    packet.window_epoch = 123u;
    packet.rssi_dbm = -42;
    packet.channel = 6u;
    packet.timestamp_ns = 999u;
    return packet;
}

static void test_abi(void)
{
    CHECK(sizeof(rtbpf_insn_t) == 8u);
    CHECK(sizeof(rtbpf_net_md_v1_t) == 40u);
    CHECK(RTBPF_NET_ABORTED == 0 && RTBPF_NET_DROP == 1 && RTBPF_NET_PASS == 2);
}

static void test_array_map(void)
{
    uint32_t storage[4];
    rtbpf_map_t map;
    CHECK(rtbpf_map_array_init(&map, RTBPF_MAP_ARRAY, sizeof(uint32_t), 4u,
                               storage, sizeof(storage), RTBPF_MAP_F_PROGRAM_WRITE) == 0);
    uint32_t key = 2u;
    uint32_t *value = rtbpf_map_lookup(&map, &key, sizeof(key));
    CHECK(value != NULL && *value == 0u);
    *value = 77u;
    CHECK(storage[2] == 77u);
    key = 4u;
    CHECK(rtbpf_map_lookup(&map, &key, sizeof(key)) == NULL);
    CHECK(rtbpf_map_array_init(&map, RTBPF_MAP_ARRAY, 16u, 4u,
                               storage, sizeof(storage), 0u) != 0);
}

static void test_no_program_and_static_attach(void)
{
    const uint8_t bytes[] = {0x01};
    rtbpf_net_packet_t packet = packet_of(bytes, sizeof(bytes));
    rtbpf_net_hook_t hook;
    rtbpf_net_hook_init(&hook, RTBPF_LINK_WIFI_ETHERNET, RTBPF_NET_DROP);
    CHECK(rtbpf_net_hook_run(&hook, &packet) == RTBPF_NET_PASS);
    CHECK(hook.stats.no_program == 1u && hook.stats.passed == 1u);
    CHECK(rtbpf_net_hook_attach_static(&hook, &rtbpf_example_pass_program) == 0);
    CHECK(rtbpf_net_hook_run(&hook, &packet) == RTBPF_NET_PASS);
    CHECK(hook.stats.maximum_instructions_observed == 2u);
    rtbpf_net_hook_detach_static(&hook);
    CHECK(rtbpf_net_hook_run(&hook, &packet) == RTBPF_NET_PASS);
}

static void test_checked_packet_filter(void)
{
    const uint8_t ff[] = {0xff};
    const uint8_t ok[] = {0x45};
    rtbpf_net_hook_t hook;
    rtbpf_net_hook_init(&hook, RTBPF_LINK_WIFI_ETHERNET, RTBPF_NET_DROP);
    CHECK(rtbpf_net_hook_attach_static(&hook, &rtbpf_example_drop_ff_program) == 0);

    rtbpf_net_packet_t packet = packet_of(ff, sizeof(ff));
    CHECK(rtbpf_net_hook_run(&hook, &packet) == RTBPF_NET_DROP);
    packet = packet_of(ok, sizeof(ok));
    CHECK(rtbpf_net_hook_run(&hook, &packet) == RTBPF_NET_PASS);
    packet = packet_of(NULL, 0u);
    CHECK(rtbpf_net_hook_run(&hook, &packet) == RTBPF_NET_PASS);
    CHECK(hook.stats.dropped == 1u && hook.stats.passed == 2u);
    CHECK(hook.stats.aborted == 0u);
}

static void test_runtime_bad_access_is_contained(void)
{
    static const rtbpf_insn_t insns[] = {
        RTBPF_LDX_W(2, 1, 0),
        RTBPF_LDX_W(3, 2, 0), /* no length proof; runtime catches empty packet */
        RTBPF_MOV64_IMM(0, RTBPF_NET_PASS),
        RTBPF_EXIT(),
    };
    const rtbpf_program_t program = {
        .id = 10u, .instructions = insns,
        .instruction_count = 4u, .maximum_executed_instructions = 4u,
        .stack_size = RTBPF_DEFAULT_STACK_SIZE,
    };
    rtbpf_verify_report_t report;
    CHECK(rtbpf_verify_program(&program, &report) == RTBPF_VERIFY_PACKET_BOUNDS);
    rtbpf_net_hook_t hook;
    rtbpf_net_hook_init(&hook, RTBPF_LINK_WIFI_ETHERNET, RTBPF_NET_DROP);
    CHECK(rtbpf_net_hook_attach_static(&hook, &program) != 0);

    rtbpf_runner_t runner;
    rtbpf_runner_init(&runner);
    rtbpf_net_md_v1_t context = {0};
    rtbpf_vm_result_t result = rtbpf_interpret(&runner, &program, &context, NULL, 0u);
    CHECK(result.status == RTBPF_VM_BAD_ACCESS);
}

static void test_watchdog(void)
{
    static const rtbpf_insn_t insns[] = { RTBPF_JA(-1) };
    const rtbpf_program_t program = {
        .id = 11u, .instructions = insns,
        .instruction_count = 1u, .maximum_executed_instructions = 7u,
        .stack_size = RTBPF_DEFAULT_STACK_SIZE,
    };
    rtbpf_verify_report_t report;
    CHECK(rtbpf_verify_program(&program, &report) == RTBPF_VERIFY_BACK_EDGE);
    rtbpf_runner_t runner;
    rtbpf_runner_init(&runner);
    rtbpf_net_md_v1_t context = {0};
    rtbpf_vm_result_t result = rtbpf_interpret(&runner, &program, &context, NULL, 0u);
    CHECK(result.status == RTBPF_VM_LIMIT_EXCEEDED);
    CHECK(result.executed_instructions == 7u);
}

static void test_array_map_from_bytecode(void)
{
    uint32_t storage[1];
    rtbpf_map_t map;
    CHECK(rtbpf_map_array_init(&map, RTBPF_MAP_ARRAY, sizeof(uint32_t), 1u,
                               storage, sizeof(storage), RTBPF_MAP_F_PROGRAM_WRITE) == 0);
    static const rtbpf_insn_t insns[] = {
        RTBPF_LD_MAP(1, 0),
        RTBPF_MOV64_REG(2, 10),
        RTBPF_ADD64_IMM(2, -4),
        RTBPF_ST_W(2, 0, 0),
        RTBPF_CALL(RTBPF_HELPER_MAP_LOOKUP),
        RTBPF_JEQ_IMM(0, 0, 3),
        RTBPF_LDX_W(3, 0, 0),
        RTBPF_ADD64_IMM(3, 1),
        RTBPF_STX_W(0, 3, 0),
        RTBPF_MOV64_IMM(0, RTBPF_NET_PASS),
        RTBPF_EXIT(),
    };
    rtbpf_program_t program = {
        .id = 12u, .instructions = insns,
        .instruction_count = 11u, .maximum_executed_instructions = 11u,
        .stack_size = RTBPF_DEFAULT_STACK_SIZE, .map_count = 1u,
        .maps = { &map },
    };
    rtbpf_net_hook_t hook;
    rtbpf_net_hook_init(&hook, RTBPF_LINK_WIFI_ETHERNET, RTBPF_NET_DROP);
    CHECK(rtbpf_net_hook_attach_static(&hook, &program) == 0);
    rtbpf_net_packet_t packet = packet_of(NULL, 0u);
    CHECK(rtbpf_net_hook_run(&hook, &packet) == RTBPF_NET_PASS);
    CHECK(rtbpf_net_hook_run(&hook, &packet) == RTBPF_NET_PASS);
    CHECK(storage[0] == 2u);
}

static void test_read_only_map_is_enforced(void)
{
    uint32_t storage[1];
    rtbpf_map_t map;
    CHECK(rtbpf_map_array_init(&map, RTBPF_MAP_CONFIG_ARRAY, sizeof(uint32_t), 1u,
                               storage, sizeof(storage), 0u) == 0);
    static const rtbpf_insn_t insns[] = {
        RTBPF_LD_MAP(1, 0), RTBPF_MOV64_REG(2, 10), RTBPF_ADD64_IMM(2, -4),
        RTBPF_ST_W(2, 0, 0), RTBPF_CALL(RTBPF_HELPER_MAP_LOOKUP),
        RTBPF_JEQ_IMM(0, 0, 1), RTBPF_ST_W(0, 0, 99),
        RTBPF_MOV64_IMM(0, RTBPF_NET_PASS), RTBPF_EXIT(),
    };
    rtbpf_program_t program = {
        .id = 13u, .instructions = insns, .instruction_count = 9u,
        .maximum_executed_instructions = 9u, .stack_size = RTBPF_DEFAULT_STACK_SIZE,
        .map_count = 1u, .maps = { &map },
    };
    rtbpf_verify_report_t report;
    CHECK(rtbpf_verify_program(&program, &report) == RTBPF_VERIFY_MAP_READ_ONLY);
    rtbpf_runner_t runner;
    rtbpf_runner_init(&runner);
    rtbpf_net_md_v1_t context = {0};
    rtbpf_vm_result_t result = rtbpf_interpret(&runner, &program, &context, NULL, 0u);
    CHECK(result.status == RTBPF_VM_BAD_ACCESS);
    CHECK(storage[0] == 0u);
}

static void test_bad_opcode_and_link_profile(void)
{
    static const rtbpf_insn_t insns[] = { RTBPF_INSN(0xfe, 0, 0, 0, 0) };
    const rtbpf_program_t program = {
        .id = 14u, .instructions = insns, .instruction_count = 1u,
        .maximum_executed_instructions = 1u, .stack_size = RTBPF_DEFAULT_STACK_SIZE,
    };
    rtbpf_verify_report_t report;
    CHECK(rtbpf_verify_program(&program, &report) == RTBPF_VERIFY_BAD_OPCODE);
    rtbpf_net_hook_t hook;
    rtbpf_net_hook_init(&hook, RTBPF_LINK_WIFI_ETHERNET, RTBPF_NET_DROP);
    CHECK(rtbpf_net_hook_attach_static(&hook, &program) != 0);
    CHECK(rtbpf_net_hook_attach_static(&hook, &rtbpf_example_pass_program) == 0);
    rtbpf_net_packet_t packet = packet_of(NULL, 0u);
    packet.link_type = RTBPF_LINK_RAW_IP;
    CHECK(rtbpf_net_hook_run(&hook, &packet) == RTBPF_NET_DROP);
    CHECK(hook.stats.aborted == 1u);
}

int main(void)
{
    test_abi();
    test_array_map();
    test_no_program_and_static_attach();
    test_checked_packet_filter();
    test_runtime_bad_access_is_contained();
    test_watchdog();
    test_array_map_from_bytecode();
    test_read_only_map_is_enforced();
    test_bad_opcode_and_link_profile();

    printf("Phase 1 tests: %u checks, %u failures\n", tests_run, tests_failed);
    return tests_failed == 0u ? 0 : 1;
}
