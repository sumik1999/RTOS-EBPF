#ifndef RTBPF_PROGRAM_H
#define RTBPF_PROGRAM_H

#include <stddef.h>
#include <stdint.h>
#include "rtbpf/abi.h"
#include "rtbpf/maps.h"

#ifdef __cplusplus
extern "C" {
#endif

#define RTBPF_MAX_PROGRAM_MAPS 8u
#define RTBPF_DEFAULT_STACK_SIZE 512u

typedef struct {
    uint32_t id;
    const rtbpf_insn_t *instructions;
    uint32_t instruction_count;
    uint32_t maximum_executed_instructions;
    uint16_t stack_size;
    uint16_t map_count;
    rtbpf_map_t *maps[RTBPF_MAX_PROGRAM_MAPS];
} rtbpf_program_t;

#ifdef __cplusplus
}
#endif
#endif
