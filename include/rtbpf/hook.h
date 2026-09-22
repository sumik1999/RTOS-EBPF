#ifndef RTBPF_HOOK_H
#define RTBPF_HOOK_H

#include <stdint.h>
#include "rtbpf/interpreter.h"
#include "rtbpf/net.h"
#include "rtbpf/program.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint64_t packets;
    uint64_t bytes;
    uint64_t passed;
    uint64_t dropped;
    uint64_t aborted;
    uint64_t no_program;
    uint64_t invalid_returns;
    uint64_t instruction_limit_faults;
    uint64_t bad_access_faults;
    uint32_t maximum_instructions_observed;
} rtbpf_net_stats_t;

typedef struct {
    const rtbpf_program_t *program; /* immutable while attached in Phase 1 */
    rtbpf_runner_t runner;
    rtbpf_net_stats_t stats;
    uint32_t expected_link_type; /* zero accepts any declared V1 profile */
    rtbpf_net_action_t fault_action;
} rtbpf_net_hook_t;

void rtbpf_net_hook_init(rtbpf_net_hook_t *hook, uint32_t expected_link_type,
                         rtbpf_net_action_t fault_action);
int rtbpf_net_hook_attach_static(rtbpf_net_hook_t *hook,
                                 const rtbpf_program_t *program);
void rtbpf_net_hook_detach_static(rtbpf_net_hook_t *hook);
rtbpf_net_action_t rtbpf_net_hook_run(rtbpf_net_hook_t *hook,
                                      const rtbpf_net_packet_t *packet);

#ifdef __cplusplus
}
#endif
#endif
