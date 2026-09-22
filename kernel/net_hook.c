#include "rtbpf/hook.h"
#include "rtbpf/verifier.h"

#include <limits.h>
#include <string.h>

void rtbpf_net_hook_init(rtbpf_net_hook_t *hook, uint32_t expected_link_type,
                         rtbpf_net_action_t fault_action)
{
    if (hook == NULL) return;
    memset(hook, 0, sizeof(*hook));
    hook->expected_link_type = expected_link_type;
    hook->fault_action = (fault_action == RTBPF_NET_PASS) ?
                         RTBPF_NET_PASS : RTBPF_NET_DROP;
    rtbpf_runner_init(&hook->runner);
}

int rtbpf_net_hook_attach_static(rtbpf_net_hook_t *hook,
                                 const rtbpf_program_t *program)
{
    if (hook == NULL || program == NULL || program->instructions == NULL ||
        program->instruction_count == 0u ||
        program->maximum_executed_instructions == 0u ||
        program->stack_size == 0u || program->stack_size > RTBPF_MAX_STACK ||
        program->map_count > RTBPF_MAX_PROGRAM_MAPS) {
        return -1;
    }
    rtbpf_verify_report_t report;
    if (rtbpf_verify_program(program, &report) != RTBPF_VERIFY_OK) {
        return -1;
    }
    hook->program = program;
    return 0;
}

void rtbpf_net_hook_detach_static(rtbpf_net_hook_t *hook)
{
    if (hook != NULL) hook->program = NULL;
}

static rtbpf_net_action_t account_fault(rtbpf_net_hook_t *hook,
                                        rtbpf_vm_status_t status)
{
    hook->stats.aborted++;
    if (status == RTBPF_VM_LIMIT_EXCEEDED)
        hook->stats.instruction_limit_faults++;
    if (status == RTBPF_VM_BAD_ACCESS)
        hook->stats.bad_access_faults++;
    return hook->fault_action;
}

rtbpf_net_action_t rtbpf_net_hook_run(rtbpf_net_hook_t *hook,
                                      const rtbpf_net_packet_t *packet)
{
    if (hook == NULL || packet == NULL ||
        (packet->data == NULL && packet->length != 0u)) {
        return RTBPF_NET_ABORTED;
    }

    hook->stats.packets++;
    hook->stats.bytes += packet->length;

    if (hook->expected_link_type != 0u &&
        packet->link_type != hook->expected_link_type) {
        return account_fault(hook, RTBPF_VM_BAD_ARGUMENT);
    }
    if (hook->program == NULL) {
        hook->stats.no_program++;
        hook->stats.passed++;
        return RTBPF_NET_PASS;
    }

    rtbpf_net_md_v1_t context;
    memset(&context, 0, sizeof(context));
    context.ingress_port = packet->ingress_port;
    context.link_type = packet->link_type;
    context.packet_length = packet->length;
    context.window_epoch = packet->window_epoch;
    context.rssi_dbm = packet->rssi_dbm;
    context.channel = packet->channel;
    context.timestamp_ns = packet->timestamp_ns;

    const rtbpf_vm_result_t result = rtbpf_interpret(
        &hook->runner, hook->program, &context, packet->data, packet->length);
    if (result.executed_instructions > hook->stats.maximum_instructions_observed)
        hook->stats.maximum_instructions_observed = result.executed_instructions;
    if (result.status != RTBPF_VM_OK)
        return account_fault(hook, result.status);

    if (result.return_value == RTBPF_NET_PASS) {
        hook->stats.passed++;
        return RTBPF_NET_PASS;
    }
    if (result.return_value == RTBPF_NET_DROP) {
        hook->stats.dropped++;
        return RTBPF_NET_DROP;
    }

    hook->stats.invalid_returns++;
    return account_fault(hook, RTBPF_VM_BAD_PROGRAM);
}
