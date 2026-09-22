#include "static_programs.h"

#include "rtbpf/net.h"

static const rtbpf_insn_t pass_instructions[] = {
    RTBPF_MOV64_IMM(0, RTBPF_NET_PASS),
    RTBPF_EXIT(),
};

const rtbpf_program_t rtbpf_example_pass_program = {
    .id = 1u,
    .instructions = pass_instructions,
    .instruction_count = sizeof(pass_instructions) / sizeof(pass_instructions[0]),
    .maximum_executed_instructions = 2u,
    .stack_size = RTBPF_DEFAULT_STACK_SIZE,
};

/* Safely drop a packet whose first byte is 0xff; pass an empty packet. */
static const rtbpf_insn_t drop_ff_instructions[] = {
    RTBPF_LDX_W(2, 1, 0),       /* r2 = ctx->data */
    RTBPF_LDX_W(3, 1, 4),       /* r3 = ctx->data_end */
    RTBPF_MOV64_REG(4, 2),
    RTBPF_ADD64_IMM(4, 1),
    RTBPF_JGT_REG(4, 3, 4),     /* if data + 1 > end: pass */
    RTBPF_LDX_B(5, 2, 0),
    RTBPF_JNE_IMM(5, 0xff, 2),  /* if first byte != ff: pass */
    RTBPF_MOV64_IMM(0, RTBPF_NET_DROP),
    RTBPF_EXIT(),
    RTBPF_MOV64_IMM(0, RTBPF_NET_PASS),
    RTBPF_EXIT(),
};

const rtbpf_program_t rtbpf_example_drop_ff_program = {
    .id = 2u,
    .instructions = drop_ff_instructions,
    .instruction_count = sizeof(drop_ff_instructions) / sizeof(drop_ff_instructions[0]),
    .maximum_executed_instructions = 11u,
    .stack_size = RTBPF_DEFAULT_STACK_SIZE,
};
