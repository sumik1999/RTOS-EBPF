#include "rtbpf/abi.h"
#include "rtbpf/program.h"
#include "rtbpf/verifier.h"

#include <stdio.h>
#include <string.h>

int main(void)
{
    rtbpf_insn_t instructions[RTBPF_VERIFY_MAX_INSNS];
    const size_t bytes = fread(instructions, 1u, sizeof(instructions), stdin);
    const uint32_t count = (uint32_t)(bytes / sizeof(rtbpf_insn_t));
    if (count == 0u) return 0;

    rtbpf_program_t program;
    memset(&program, 0, sizeof(program));
    program.id = 0xf00du;
    program.instructions = instructions;
    program.instruction_count = count;
    program.maximum_executed_instructions = RTBPF_VERIFY_MAX_INSNS;
    program.stack_size = RTBPF_DEFAULT_STACK_SIZE;

    rtbpf_verify_report_t report;
    (void)rtbpf_verify_program(&program, &report);
    return 0;
}
