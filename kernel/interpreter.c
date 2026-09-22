#include "rtbpf/interpreter.h"

#include "rtbpf/helpers.h"

#include <limits.h>
#include <stddef.h>
#include <string.h>

typedef enum {
    REG_UNINIT = 0,
    REG_SCALAR,
    REG_CONTEXT,
    REG_PACKET,
    REG_PACKET_END,
    REG_STACK,
    REG_MAP_HANDLE,
    REG_MAP_VALUE,
} reg_tag_t;

typedef struct {
    reg_tag_t tag;
    uint64_t scalar;
    int64_t offset;
    rtbpf_map_t *map;
    uint8_t *base;
    uint32_t extent;
    uint8_t writable;
} vm_reg_t;

typedef struct {
    vm_reg_t reg[RTBPF_REGS];
    uint32_t pc;
    uint32_t executed;
    uint32_t limit;
    rtbpf_runner_t *runner;
    const rtbpf_program_t *program;
    const rtbpf_net_md_v1_t *context;
    const uint8_t *packet;
    uint32_t packet_length;
} vm_t;

static vm_reg_t scalar_reg(uint64_t value)
{
    vm_reg_t reg;
    memset(&reg, 0, sizeof(reg));
    reg.tag = REG_SCALAR;
    reg.scalar = value;
    return reg;
}

static int add_offset(int64_t base, int64_t delta, int64_t *out)
{
    if ((delta > 0 && base > INT64_MAX - delta) ||
        (delta < 0 && base < INT64_MIN - delta)) {
        return -1;
    }
    *out = base + delta;
    return 0;
}

static int range_ok(int64_t offset, uint32_t width, uint32_t extent)
{
    return offset >= 0 && (uint64_t)offset <= extent &&
           (uint64_t)width <= (uint64_t)extent - (uint64_t)offset;
}

static rtbpf_vm_status_t branch(vm_t *vm, int16_t relative)
{
    const int64_t target = (int64_t)vm->pc + (int64_t)relative;
    if (target < 0 || target >= (int64_t)vm->program->instruction_count) {
        return RTBPF_VM_BAD_PC;
    }
    vm->pc = (uint32_t)target;
    return RTBPF_VM_OK;
}

static rtbpf_vm_status_t context_load(vm_t *vm, vm_reg_t *dst,
                                      int64_t offset, uint32_t width)
{
    if (!range_ok(offset, width, (uint32_t)sizeof(*vm->context))) {
        return RTBPF_VM_BAD_ACCESS;
    }
    if (offset == (int64_t)offsetof(rtbpf_net_md_v1_t, data) && width == 4u) {
        memset(dst, 0, sizeof(*dst));
        dst->tag = REG_PACKET;
        dst->base = (uint8_t *)(uintptr_t)vm->packet;
        dst->extent = vm->packet_length;
        return RTBPF_VM_OK;
    }
    if (offset == (int64_t)offsetof(rtbpf_net_md_v1_t, data_end) && width == 4u) {
        memset(dst, 0, sizeof(*dst));
        dst->tag = REG_PACKET_END;
        dst->base = (uint8_t *)(uintptr_t)vm->packet;
        dst->extent = vm->packet_length;
        dst->offset = vm->packet_length;
        return RTBPF_VM_OK;
    }

    const int scalar32 = width == 4u &&
        (offset == (int64_t)offsetof(rtbpf_net_md_v1_t, ingress_port) ||
         offset == (int64_t)offsetof(rtbpf_net_md_v1_t, link_type) ||
         offset == (int64_t)offsetof(rtbpf_net_md_v1_t, packet_length) ||
         offset == (int64_t)offsetof(rtbpf_net_md_v1_t, window_epoch) ||
         offset == (int64_t)offsetof(rtbpf_net_md_v1_t, rssi_dbm) ||
         offset == (int64_t)offsetof(rtbpf_net_md_v1_t, channel));
    const int scalar64 = width == 8u &&
        offset == (int64_t)offsetof(rtbpf_net_md_v1_t, timestamp_ns);
    if (!scalar32 && !scalar64) {
        return RTBPF_VM_BAD_ACCESS;
    }

    uint64_t value = 0u;
    memcpy(&value, ((const uint8_t *)vm->context) + offset, width);
    *dst = scalar_reg(value);
    return RTBPF_VM_OK;
}

static rtbpf_vm_status_t memory_load(vm_t *vm, vm_reg_t *dst,
                                     const vm_reg_t *src, int16_t insn_offset,
                                     uint32_t width)
{
    int64_t offset = 0;
    if (src->tag == REG_CONTEXT) {
        if (add_offset(src->offset, insn_offset, &offset) != 0) {
            return RTBPF_VM_BAD_ACCESS;
        }
        return context_load(vm, dst, offset, width);
    }

    if (src->tag != REG_PACKET && src->tag != REG_STACK &&
        src->tag != REG_MAP_VALUE) {
        return RTBPF_VM_BAD_ACCESS;
    }
    if (add_offset(src->offset, insn_offset, &offset) != 0 ||
        !range_ok(offset, width, src->extent)) {
        return RTBPF_VM_BAD_ACCESS;
    }

    uint64_t value = 0u;
    memcpy(&value, src->base + offset, width);
    *dst = scalar_reg(value);
    return RTBPF_VM_OK;
}

static rtbpf_vm_status_t memory_store(vm_t *vm, const vm_reg_t *dst,
                                      int16_t insn_offset, const void *value,
                                      uint32_t width)
{
    (void)vm;
    if (dst->tag != REG_STACK && dst->tag != REG_MAP_VALUE) {
        return RTBPF_VM_BAD_ACCESS;
    }
    if (dst->tag == REG_MAP_VALUE && dst->writable == 0u) {
        return RTBPF_VM_BAD_ACCESS;
    }

    int64_t offset = 0;
    if (add_offset(dst->offset, insn_offset, &offset) != 0 ||
        !range_ok(offset, width, dst->extent)) {
        return RTBPF_VM_BAD_ACCESS;
    }
    memcpy(dst->base + offset, value, width);
    return RTBPF_VM_OK;
}

static rtbpf_vm_status_t helper_map_lookup(vm_t *vm)
{
    vm_reg_t *map_reg = &vm->reg[1];
    vm_reg_t *key_reg = &vm->reg[2];
    if (map_reg->tag != REG_MAP_HANDLE || map_reg->map == NULL ||
        key_reg->tag != REG_STACK) {
        return RTBPF_VM_BAD_HELPER;
    }

    int64_t key_offset = key_reg->offset;
    if (!range_ok(key_offset, sizeof(uint32_t), key_reg->extent)) {
        return RTBPF_VM_BAD_HELPER;
    }
    uint32_t key = 0u;
    memcpy(&key, key_reg->base + key_offset, sizeof(key));
    uint8_t *value = rtbpf_map_lookup(map_reg->map, &key, sizeof(key));
    if (value == NULL) {
        vm->reg[0] = scalar_reg(0u);
        return RTBPF_VM_OK;
    }

    vm_reg_t result;
    memset(&result, 0, sizeof(result));
    result.tag = REG_MAP_VALUE;
    result.map = map_reg->map;
    result.base = value;
    result.extent = map_reg->map->value_size;
    result.writable = (map_reg->map->flags & RTBPF_MAP_F_PROGRAM_WRITE) != 0u;
    vm->reg[0] = result;
    return RTBPF_VM_OK;
}

static rtbpf_vm_status_t invoke_helper(vm_t *vm, uint32_t helper_id)
{
    if (helper_id == RTBPF_HELPER_MAP_LOOKUP) {
        return helper_map_lookup(vm);
    }
    return RTBPF_VM_BAD_HELPER;
}

void rtbpf_runner_init(rtbpf_runner_t *runner)
{
    if (runner != NULL) {
        memset(runner, 0, sizeof(*runner));
    }
}

rtbpf_vm_result_t rtbpf_interpret(rtbpf_runner_t *runner,
                                  const rtbpf_program_t *program,
                                  const rtbpf_net_md_v1_t *context,
                                  const uint8_t *packet,
                                  uint32_t packet_length)
{
    rtbpf_vm_result_t result = {RTBPF_VM_BAD_ARGUMENT, 0u, 0u};
    if (runner == NULL || program == NULL || context == NULL ||
        program->instructions == NULL || program->instruction_count == 0u ||
        program->maximum_executed_instructions == 0u ||
        program->stack_size == 0u || program->stack_size > RTBPF_MAX_STACK ||
        program->map_count > RTBPF_MAX_PROGRAM_MAPS ||
        (packet == NULL && packet_length != 0u)) {
        return result;
    }

    vm_t vm;
    memset(&vm, 0, sizeof(vm));
    memset(runner->stack, 0, sizeof(runner->stack));
    vm.runner = runner;
    vm.program = program;
    vm.context = context;
    vm.packet = packet;
    vm.packet_length = packet_length;
    vm.limit = program->maximum_executed_instructions;

    vm.reg[1].tag = REG_CONTEXT;
    vm.reg[1].base = (uint8_t *)(uintptr_t)context;
    vm.reg[1].extent = sizeof(*context);
    vm.reg[10].tag = REG_STACK;
    vm.reg[10].base = runner->stack;
    vm.reg[10].extent = program->stack_size;
    vm.reg[10].offset = program->stack_size;

    while (vm.executed < vm.limit) {
        if (vm.pc >= program->instruction_count) {
            result.status = RTBPF_VM_BAD_PC;
            break;
        }

        const rtbpf_insn_t insn = program->instructions[vm.pc++];
        const uint8_t dst_index = RTBPF_INSN_DST(&insn);
        const uint8_t src_index = RTBPF_INSN_SRC(&insn);
        vm.executed++;
        if (dst_index >= RTBPF_REGS || src_index >= RTBPF_REGS) {
            result.status = RTBPF_VM_BAD_REGISTER;
            break;
        }
        vm_reg_t *dst = &vm.reg[dst_index];
        vm_reg_t *src = &vm.reg[src_index];
        rtbpf_vm_status_t status = RTBPF_VM_OK;

        switch (insn.opcode) {
        case RTBPF_OP_MOV64_IMM:
            *dst = scalar_reg((uint64_t)(int64_t)insn.immediate);
            break;
        case RTBPF_OP_MOV64_REG:
            if (src->tag == REG_UNINIT) status = RTBPF_VM_BAD_ALU;
            else *dst = *src;
            break;
        case RTBPF_OP_ADD64_IMM:
            if (dst->tag == REG_SCALAR) {
                dst->scalar += (uint64_t)(int64_t)insn.immediate;
            } else if (dst->tag == REG_PACKET || dst->tag == REG_STACK ||
                       dst->tag == REG_MAP_VALUE) {
                if (add_offset(dst->offset, insn.immediate, &dst->offset) != 0)
                    status = RTBPF_VM_BAD_ALU;
            } else status = RTBPF_VM_BAD_ALU;
            break;
        case RTBPF_OP_ADD64_REG:
            if (dst->tag != REG_SCALAR || src->tag != REG_SCALAR)
                status = RTBPF_VM_BAD_ALU;
            else dst->scalar += src->scalar;
            break;
        case RTBPF_OP_LDX_B:
            status = memory_load(&vm, dst, src, insn.offset, 1u); break;
        case RTBPF_OP_LDX_H:
            status = memory_load(&vm, dst, src, insn.offset, 2u); break;
        case RTBPF_OP_LDX_W:
            status = memory_load(&vm, dst, src, insn.offset, 4u); break;
        case RTBPF_OP_LDX_DW:
            status = memory_load(&vm, dst, src, insn.offset, 8u); break;
        case RTBPF_OP_ST_W: {
            const uint32_t value = (uint32_t)insn.immediate;
            status = memory_store(&vm, dst, insn.offset, &value, sizeof(value));
            break;
        }
        case RTBPF_OP_STX_W: {
            if (src->tag != REG_SCALAR) { status = RTBPF_VM_BAD_ACCESS; break; }
            const uint32_t value = (uint32_t)src->scalar;
            status = memory_store(&vm, dst, insn.offset, &value, sizeof(value));
            break;
        }
        case RTBPF_OP_STX_DW: {
            if (src->tag != REG_SCALAR) { status = RTBPF_VM_BAD_ACCESS; break; }
            const uint64_t value = src->scalar;
            status = memory_store(&vm, dst, insn.offset, &value, sizeof(value));
            break;
        }
        case RTBPF_OP_JA:
            status = branch(&vm, insn.offset); break;
        case RTBPF_OP_JEQ_IMM:
        case RTBPF_OP_JNE_IMM: {
            int equal;
            if (dst->tag == REG_SCALAR) {
                equal = dst->scalar == (uint64_t)(int64_t)insn.immediate;
            } else if (dst->tag == REG_MAP_VALUE && insn.immediate == 0) {
                equal = 0;
            } else {
                status = RTBPF_VM_BAD_ALU; break;
            }
            if ((insn.opcode == RTBPF_OP_JEQ_IMM && equal) ||
                (insn.opcode == RTBPF_OP_JNE_IMM && !equal))
                status = branch(&vm, insn.offset);
            break;
        }
        case RTBPF_OP_JGT_REG:
            if ((dst->tag == REG_PACKET && src->tag == REG_PACKET_END &&
                 dst->base == src->base) ||
                (dst->tag == REG_PACKET_END && src->tag == REG_PACKET &&
                 dst->base == src->base)) {
                if (dst->offset > src->offset) status = branch(&vm, insn.offset);
            } else if (dst->tag == REG_SCALAR && src->tag == REG_SCALAR) {
                if (dst->scalar > src->scalar) status = branch(&vm, insn.offset);
            } else status = RTBPF_VM_BAD_ALU;
            break;
        case RTBPF_OP_LD_MAP:
            if (insn.immediate < 0 || (uint32_t)insn.immediate >= program->map_count ||
                program->maps[insn.immediate] == NULL) {
                status = RTBPF_VM_BAD_ACCESS;
            } else {
                memset(dst, 0, sizeof(*dst));
                dst->tag = REG_MAP_HANDLE;
                dst->map = program->maps[insn.immediate];
            }
            break;
        case RTBPF_OP_CALL:
            status = invoke_helper(&vm, (uint32_t)insn.immediate); break;
        case RTBPF_OP_EXIT:
            if (vm.reg[0].tag != REG_SCALAR) status = RTBPF_VM_BAD_PROGRAM;
            else {
                result.status = RTBPF_VM_OK;
                result.return_value = vm.reg[0].scalar;
                result.executed_instructions = vm.executed;
                return result;
            }
            break;
        default:
            status = RTBPF_VM_BAD_OPCODE;
            break;
        }

        if (status != RTBPF_VM_OK) {
            result.status = status;
            break;
        }
    }

    if (vm.executed >= vm.limit && result.status == RTBPF_VM_BAD_ARGUMENT) {
        result.status = RTBPF_VM_LIMIT_EXCEEDED;
    }
    result.executed_instructions = vm.executed;
    return result;
}

const char *rtbpf_vm_status_string(rtbpf_vm_status_t status)
{
    switch (status) {
    case RTBPF_VM_OK: return "ok";
    case RTBPF_VM_BAD_ARGUMENT: return "bad argument";
    case RTBPF_VM_BAD_PROGRAM: return "bad program";
    case RTBPF_VM_BAD_OPCODE: return "bad opcode";
    case RTBPF_VM_BAD_REGISTER: return "bad register";
    case RTBPF_VM_BAD_PC: return "bad pc";
    case RTBPF_VM_BAD_ACCESS: return "bad access";
    case RTBPF_VM_BAD_ALU: return "bad alu operation";
    case RTBPF_VM_BAD_HELPER: return "bad helper";
    case RTBPF_VM_LIMIT_EXCEEDED: return "instruction limit exceeded";
    default: return "unknown";
    }
}
