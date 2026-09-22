#include "rtbpf/abi.h"
#include "rtbpf/helpers.h"
#include "rtbpf/maps.h"
#include "rtbpf/net.h"
#include "rtbpf/program.h"
#include "rtbpf/verifier.h"
#include "static_programs.h"

#include <stdio.h>
#include <string.h>

static unsigned checks;
static unsigned failures;
#define CHECK(x) do { checks++; if (!(x)) { failures++; \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); } } while (0)

static rtbpf_program_t program_of(const rtbpf_insn_t *insns, uint32_t count,
                                  uint32_t budget)
{
    rtbpf_program_t p;
    memset(&p, 0, sizeof(p));
    p.id = 100u;
    p.instructions = insns;
    p.instruction_count = count;
    p.maximum_executed_instructions = budget;
    p.stack_size = RTBPF_DEFAULT_STACK_SIZE;
    return p;
}

static rtbpf_verify_status_t verify(const rtbpf_program_t *p,
                                    rtbpf_verify_report_t *report)
{
    memset(report, 0xa5, sizeof(*report));
    return rtbpf_verify_program(p, report);
}

static void positive_programs(void)
{
    rtbpf_verify_report_t report;
    CHECK(verify(&rtbpf_example_pass_program, &report) == RTBPF_VERIFY_OK);
    CHECK(report.maximum_path_instructions == 2u);
    CHECK(verify(&rtbpf_example_drop_ff_program, &report) == RTBPF_VERIFY_OK);
    CHECK(report.maximum_path_instructions <=
          rtbpf_example_drop_ff_program.maximum_executed_instructions);
}

static void structural_rejections(void)
{
    rtbpf_verify_report_t report;
    static const rtbpf_insn_t bad_jump[] = { RTBPF_JA(4), RTBPF_EXIT() };
    rtbpf_program_t p = program_of(bad_jump, 2u, 2u);
    CHECK(verify(&p, &report) == RTBPF_VERIFY_BAD_JUMP);
    CHECK(report.instruction_index == 0u);

    static const rtbpf_insn_t back_edge[] = { RTBPF_JA(-1) };
    p = program_of(back_edge, 1u, 4u);
    CHECK(verify(&p, &report) == RTBPF_VERIFY_BACK_EDGE);

    static const rtbpf_insn_t fallthrough[] = { RTBPF_MOV64_IMM(0, RTBPF_NET_PASS) };
    p = program_of(fallthrough, 1u, 1u);
    CHECK(verify(&p, &report) == RTBPF_VERIFY_FALLTHROUGH);

    static const rtbpf_insn_t bad_helper[] = {
        RTBPF_CALL(99), RTBPF_MOV64_IMM(0, RTBPF_NET_PASS), RTBPF_EXIT()
    };
    p = program_of(bad_helper, 3u, 3u);
    CHECK(verify(&p, &report) == RTBPF_VERIFY_BAD_HELPER);
}

static void type_and_memory_rejections(void)
{
    rtbpf_verify_report_t report;
    static const rtbpf_insn_t uninit[] = {
        RTBPF_MOV64_REG(0, 2), RTBPF_EXIT()
    };
    rtbpf_program_t p = program_of(uninit, 2u, 2u);
    CHECK(verify(&p, &report) == RTBPF_VERIFY_UNINITIALIZED);

    static const rtbpf_insn_t bad_context[] = {
        RTBPF_LDX_B(2, 1, 8), RTBPF_MOV64_IMM(0, RTBPF_NET_PASS), RTBPF_EXIT()
    };
    p = program_of(bad_context, 3u, 3u);
    CHECK(verify(&p, &report) == RTBPF_VERIFY_BAD_CONTEXT_ACCESS);

    static const rtbpf_insn_t packet_without_proof[] = {
        RTBPF_LDX_W(2, 1, 0), RTBPF_LDX_B(3, 2, 0),
        RTBPF_MOV64_IMM(0, RTBPF_NET_PASS), RTBPF_EXIT()
    };
    p = program_of(packet_without_proof, 4u, 4u);
    CHECK(verify(&p, &report) == RTBPF_VERIFY_PACKET_BOUNDS);

    static const rtbpf_insn_t stack_uninitialized[] = {
        RTBPF_LDX_W(2, 10, -4), RTBPF_MOV64_IMM(0, RTBPF_NET_PASS), RTBPF_EXIT()
    };
    p = program_of(stack_uninitialized, 3u, 3u);
    CHECK(verify(&p, &report) == RTBPF_VERIFY_STACK_UNINITIALIZED);

    static const rtbpf_insn_t stack_oob[] = {
        RTBPF_ST_W(10, 0, 7), RTBPF_MOV64_IMM(0, RTBPF_NET_PASS), RTBPF_EXIT()
    };
    p = program_of(stack_oob, 3u, 3u);
    CHECK(verify(&p, &report) == RTBPF_VERIFY_STACK_BOUNDS);
}

static void map_rejections_and_success(void)
{
    uint32_t writable_storage[1];
    uint32_t readonly_storage[1];
    rtbpf_map_t writable, readonly;
    CHECK(rtbpf_map_array_init(&writable, RTBPF_MAP_ARRAY, 4u, 1u,
                               writable_storage, sizeof(writable_storage),
                               RTBPF_MAP_F_PROGRAM_WRITE) == 0);
    CHECK(rtbpf_map_array_init(&readonly, RTBPF_MAP_CONFIG_ARRAY, 4u, 1u,
                               readonly_storage, sizeof(readonly_storage), 0u) == 0);

    static const rtbpf_insn_t nullable_access[] = {
        RTBPF_LD_MAP(1, 0), RTBPF_MOV64_REG(2, 10), RTBPF_ADD64_IMM(2, -4),
        RTBPF_ST_W(2, 0, 0), RTBPF_CALL(RTBPF_HELPER_MAP_LOOKUP),
        RTBPF_LDX_W(3, 0, 0), RTBPF_MOV64_IMM(0, RTBPF_NET_PASS), RTBPF_EXIT(),
    };
    rtbpf_program_t p = program_of(nullable_access, 8u, 8u);
    p.map_count = 1u; p.maps[0] = &writable;
    rtbpf_verify_report_t report;
    CHECK(verify(&p, &report) == RTBPF_VERIFY_BAD_MAP);

    static const rtbpf_insn_t out_of_bounds[] = {
        RTBPF_LD_MAP(1, 0), RTBPF_MOV64_REG(2, 10), RTBPF_ADD64_IMM(2, -4),
        RTBPF_ST_W(2, 0, 0), RTBPF_CALL(RTBPF_HELPER_MAP_LOOKUP),
        RTBPF_JEQ_IMM(0, 0, 2), RTBPF_LDX_W(3, 0, 4), RTBPF_JA(0),
        RTBPF_MOV64_IMM(0, RTBPF_NET_PASS), RTBPF_EXIT(),
    };
    p = program_of(out_of_bounds, 10u, 10u);
    p.map_count = 1u; p.maps[0] = &writable;
    CHECK(verify(&p, &report) == RTBPF_VERIFY_MAP_BOUNDS);

    static const rtbpf_insn_t readonly_write[] = {
        RTBPF_LD_MAP(1, 0), RTBPF_MOV64_REG(2, 10), RTBPF_ADD64_IMM(2, -4),
        RTBPF_ST_W(2, 0, 0), RTBPF_CALL(RTBPF_HELPER_MAP_LOOKUP),
        RTBPF_JEQ_IMM(0, 0, 2), RTBPF_ST_W(0, 0, 9), RTBPF_JA(0),
        RTBPF_MOV64_IMM(0, RTBPF_NET_PASS), RTBPF_EXIT(),
    };
    p = program_of(readonly_write, 10u, 10u);
    p.map_count = 1u; p.maps[0] = &readonly;
    CHECK(verify(&p, &report) == RTBPF_VERIFY_MAP_READ_ONLY);
}

static void return_and_budget_rejections(void)
{
    rtbpf_verify_report_t report;
    static const rtbpf_insn_t bad_return[] = {
        RTBPF_MOV64_IMM(0, 99), RTBPF_EXIT()
    };
    rtbpf_program_t p = program_of(bad_return, 2u, 2u);
    CHECK(verify(&p, &report) == RTBPF_VERIFY_BAD_RETURN);

    static const rtbpf_insn_t small_budget[] = {
        RTBPF_MOV64_IMM(0, RTBPF_NET_PASS), RTBPF_EXIT()
    };
    p = program_of(small_budget, 2u, 1u);
    CHECK(verify(&p, &report) == RTBPF_VERIFY_BUDGET_TOO_SMALL);
    CHECK(report.maximum_path_instructions == 2u);
}

int main(void)
{
    positive_programs();
    structural_rejections();
    type_and_memory_rejections();
    map_rejections_and_success();
    return_and_budget_rejections();
    printf("Verifier tests: %u checks, %u failures\n", checks, failures);
    return failures == 0u ? 0 : 1;
}
