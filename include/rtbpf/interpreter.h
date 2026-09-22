#ifndef RTBPF_INTERPRETER_H
#define RTBPF_INTERPRETER_H

#include <stdint.h>
#include "rtbpf/net.h"
#include "rtbpf/program.h"

#ifdef __cplusplus
extern "C" {
#endif

#define RTBPF_MAX_STACK 512u

typedef enum {
    RTBPF_VM_OK = 0,
    RTBPF_VM_BAD_ARGUMENT,
    RTBPF_VM_BAD_PROGRAM,
    RTBPF_VM_BAD_OPCODE,
    RTBPF_VM_BAD_REGISTER,
    RTBPF_VM_BAD_PC,
    RTBPF_VM_BAD_ACCESS,
    RTBPF_VM_BAD_ALU,
    RTBPF_VM_BAD_HELPER,
    RTBPF_VM_LIMIT_EXCEEDED,
} rtbpf_vm_status_t;

typedef struct {
    uint8_t stack[RTBPF_MAX_STACK];
} rtbpf_runner_t;

typedef struct {
    rtbpf_vm_status_t status;
    uint64_t return_value;
    uint32_t executed_instructions;
} rtbpf_vm_result_t;

void rtbpf_runner_init(rtbpf_runner_t *runner);
rtbpf_vm_result_t rtbpf_interpret(rtbpf_runner_t *runner,
                                  const rtbpf_program_t *program,
                                  const rtbpf_net_md_v1_t *context,
                                  const uint8_t *packet,
                                  uint32_t packet_length);
const char *rtbpf_vm_status_string(rtbpf_vm_status_t status);

#ifdef __cplusplus
}
#endif
#endif
