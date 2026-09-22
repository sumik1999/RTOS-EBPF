#ifndef RTBPF_VERIFIER_H
#define RTBPF_VERIFIER_H

#include <stdint.h>
#include "rtbpf/program.h"

#ifdef __cplusplus
extern "C" {
#endif

#define RTBPF_VERIFY_MAX_INSNS 512u
#define RTBPF_VERIFY_MAX_STATES 256u       /* simultaneously queued abstract states */
#define RTBPF_VERIFY_MAX_STEPS 4096u       /* total abstract states processed */

typedef enum {
    RTBPF_VERIFY_OK = 0,
    RTBPF_VERIFY_BAD_ARGUMENT,
    RTBPF_VERIFY_TOO_LARGE,
    RTBPF_VERIFY_BAD_OPCODE,
    RTBPF_VERIFY_BAD_REGISTER,
    RTBPF_VERIFY_BAD_JUMP,
    RTBPF_VERIFY_BACK_EDGE,
    RTBPF_VERIFY_FALLTHROUGH,
    RTBPF_VERIFY_UNINITIALIZED,
    RTBPF_VERIFY_BAD_CONTEXT_ACCESS,
    RTBPF_VERIFY_PACKET_BOUNDS,
    RTBPF_VERIFY_STACK_BOUNDS,
    RTBPF_VERIFY_STACK_UNINITIALIZED,
    RTBPF_VERIFY_BAD_MAP,
    RTBPF_VERIFY_MAP_BOUNDS,
    RTBPF_VERIFY_MAP_READ_ONLY,
    RTBPF_VERIFY_BAD_HELPER,
    RTBPF_VERIFY_BAD_ALU,
    RTBPF_VERIFY_BAD_RETURN,
    RTBPF_VERIFY_STATE_LIMIT,
    RTBPF_VERIFY_BUDGET_TOO_SMALL,
} rtbpf_verify_status_t;

typedef struct {
    rtbpf_verify_status_t status;
    uint32_t instruction_index;
    uint32_t maximum_path_instructions;
    uint32_t explored_states;
} rtbpf_verify_report_t;

rtbpf_verify_status_t rtbpf_verify_program(const rtbpf_program_t *program,
                                            rtbpf_verify_report_t *report);
const char *rtbpf_verify_status_string(rtbpf_verify_status_t status);

#ifdef __cplusplus
}
#endif
#endif
