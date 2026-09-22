#include "rtbpf/verifier.h"

#include "rtbpf/helpers.h"
#include "rtbpf/interpreter.h"
#include "rtbpf/net.h"

#include <limits.h>
#include <stddef.h>
#include <string.h>

typedef enum {
    V_UNINIT = 0,
    V_SCALAR,
    V_CONTEXT,
    V_PACKET,
    V_PACKET_END,
    V_STACK,
    V_MAP_HANDLE,
    V_MAP_VALUE_OR_NULL,
    V_MAP_VALUE,
} vtag_t;

typedef struct {
    vtag_t tag;
    int32_t offset;
    uint16_t map_index;
    uint8_t known;
    uint64_t value;
} vreg_t;

typedef struct {
    uint32_t pc;
    uint32_t depth;
    uint32_t packet_safe_bytes;
    vreg_t reg[RTBPF_REGS];
    uint8_t stack_init[RTBPF_MAX_STACK / 8u];
} vstate_t;

typedef struct {
    const rtbpf_program_t *program;
    rtbpf_verify_report_t *report;
    vstate_t work[RTBPF_VERIFY_MAX_STATES];
    uint32_t work_count;
    uint32_t generated_states;
    uint32_t maximum_path;
} verifier_t;

static rtbpf_verify_status_t fail(verifier_t *v, rtbpf_verify_status_t status,
                                  uint32_t pc)
{
    v->report->status = status;
    v->report->instruction_index = pc;
    v->report->maximum_path_instructions = v->maximum_path;
    v->report->explored_states = v->generated_states;
    return status;
}

static int known_opcode(uint8_t opcode)
{
    switch (opcode) {
    case RTBPF_OP_ADD64_IMM: case RTBPF_OP_ADD64_REG:
    case RTBPF_OP_MOV64_IMM: case RTBPF_OP_MOV64_REG:
    case RTBPF_OP_LDX_B: case RTBPF_OP_LDX_H:
    case RTBPF_OP_LDX_W: case RTBPF_OP_LDX_DW:
    case RTBPF_OP_ST_W: case RTBPF_OP_STX_W: case RTBPF_OP_STX_DW:
    case RTBPF_OP_JA: case RTBPF_OP_JEQ_IMM: case RTBPF_OP_JNE_IMM:
    case RTBPF_OP_JGT_REG: case RTBPF_OP_CALL: case RTBPF_OP_EXIT:
    case RTBPF_OP_LD_MAP:
        return 1;
    default:
        return 0;
    }
}

static int is_jump(uint8_t opcode)
{
    return opcode == RTBPF_OP_JA || opcode == RTBPF_OP_JEQ_IMM ||
           opcode == RTBPF_OP_JNE_IMM || opcode == RTBPF_OP_JGT_REG;
}

static int jump_target(const rtbpf_program_t *program, uint32_t pc,
                       int16_t offset, uint32_t *target)
{
    const int64_t value = (int64_t)pc + 1 + (int64_t)offset;
    if (value < 0 || value >= (int64_t)program->instruction_count) return -1;
    *target = (uint32_t)value;
    return 0;
}

static rtbpf_verify_status_t structural_pass(verifier_t *v)
{
    const rtbpf_program_t *p = v->program;
    if (p == NULL || p->instructions == NULL || p->instruction_count == 0u ||
        p->maximum_executed_instructions == 0u || p->stack_size == 0u ||
        p->stack_size > RTBPF_MAX_STACK || p->map_count > RTBPF_MAX_PROGRAM_MAPS) {
        return fail(v, RTBPF_VERIFY_BAD_ARGUMENT, 0u);
    }
    if (p->instruction_count > RTBPF_VERIFY_MAX_INSNS) {
        return fail(v, RTBPF_VERIFY_TOO_LARGE, 0u);
    }

    for (uint32_t pc = 0; pc < p->instruction_count; ++pc) {
        const rtbpf_insn_t *insn = &p->instructions[pc];
        const uint8_t dst = RTBPF_INSN_DST(insn);
        const uint8_t src = RTBPF_INSN_SRC(insn);
        if (!known_opcode(insn->opcode)) return fail(v, RTBPF_VERIFY_BAD_OPCODE, pc);
        if (dst >= RTBPF_REGS || src >= RTBPF_REGS)
            return fail(v, RTBPF_VERIFY_BAD_REGISTER, pc);
        if (is_jump(insn->opcode)) {
            uint32_t target = 0u;
            if (jump_target(p, pc, insn->offset, &target) != 0)
                return fail(v, RTBPF_VERIFY_BAD_JUMP, pc);
            if (target <= pc) return fail(v, RTBPF_VERIFY_BACK_EDGE, pc);
        }
        if (insn->opcode == RTBPF_OP_CALL &&
            insn->immediate != RTBPF_HELPER_MAP_LOOKUP)
            return fail(v, RTBPF_VERIFY_BAD_HELPER, pc);
        if (insn->opcode == RTBPF_OP_LD_MAP &&
            (insn->immediate < 0 || (uint32_t)insn->immediate >= p->map_count ||
             p->maps[insn->immediate] == NULL))
            return fail(v, RTBPF_VERIFY_BAD_MAP, pc);
    }
    return RTBPF_VERIFY_OK;
}

static vreg_t scalar_unknown(void)
{
    vreg_t r;
    memset(&r, 0, sizeof(r));
    r.tag = V_SCALAR;
    return r;
}

static vreg_t scalar_known(uint64_t value)
{
    vreg_t r = scalar_unknown();
    r.known = 1u;
    r.value = value;
    return r;
}

static int push(verifier_t *v, const vstate_t *state)
{
    if (v->generated_states >= RTBPF_VERIFY_MAX_STEPS ||
        v->work_count >= RTBPF_VERIFY_MAX_STATES) return -1;
    v->work[v->work_count++] = *state;
    v->generated_states++;
    return 0;
}

static int stack_range(const rtbpf_program_t *p, int32_t offset, uint32_t width)
{
    return offset >= 0 && (uint32_t)offset <= p->stack_size &&
           width <= p->stack_size - (uint32_t)offset;
}

static int map_range(const rtbpf_map_t *map, int32_t offset, uint32_t width)
{
    return map != NULL && offset >= 0 && (uint32_t)offset <= map->value_size &&
           width <= map->value_size - (uint32_t)offset;
}

static int stack_is_initialized(const vstate_t *s, uint32_t offset, uint32_t width)
{
    for (uint32_t i = 0; i < width; ++i) {
        const uint32_t bit = offset + i;
        if ((s->stack_init[bit / 8u] & (uint8_t)(1u << (bit % 8u))) == 0u)
            return 0;
    }
    return 1;
}

static void stack_mark_initialized(vstate_t *s, uint32_t offset, uint32_t width)
{
    for (uint32_t i = 0; i < width; ++i) {
        const uint32_t bit = offset + i;
        s->stack_init[bit / 8u] |= (uint8_t)(1u << (bit % 8u));
    }
}

static rtbpf_verify_status_t verify_context_load(verifier_t *v, vstate_t *s,
                                                 uint32_t pc, vreg_t *dst,
                                                 int32_t offset, uint32_t width)
{
    if (offset == (int32_t)offsetof(rtbpf_net_md_v1_t, data) && width == 4u) {
        memset(dst, 0, sizeof(*dst)); dst->tag = V_PACKET; return RTBPF_VERIFY_OK;
    }
    if (offset == (int32_t)offsetof(rtbpf_net_md_v1_t, data_end) && width == 4u) {
        memset(dst, 0, sizeof(*dst)); dst->tag = V_PACKET_END; return RTBPF_VERIFY_OK;
    }
    const int scalar32 = width == 4u &&
        (offset == (int32_t)offsetof(rtbpf_net_md_v1_t, ingress_port) ||
         offset == (int32_t)offsetof(rtbpf_net_md_v1_t, link_type) ||
         offset == (int32_t)offsetof(rtbpf_net_md_v1_t, packet_length) ||
         offset == (int32_t)offsetof(rtbpf_net_md_v1_t, window_epoch) ||
         offset == (int32_t)offsetof(rtbpf_net_md_v1_t, rssi_dbm) ||
         offset == (int32_t)offsetof(rtbpf_net_md_v1_t, channel));
    const int scalar64 = width == 8u &&
        offset == (int32_t)offsetof(rtbpf_net_md_v1_t, timestamp_ns);
    if (!scalar32 && !scalar64)
        return fail(v, RTBPF_VERIFY_BAD_CONTEXT_ACCESS, pc);
    *dst = scalar_unknown();
    (void)s;
    return RTBPF_VERIFY_OK;
}

static rtbpf_verify_status_t verify_load(verifier_t *v, vstate_t *s, uint32_t pc,
                                         vreg_t *dst, const vreg_t *src,
                                         int16_t instruction_offset, uint32_t width)
{
    const int64_t effective64 = (int64_t)src->offset + instruction_offset;
    if (effective64 < INT32_MIN || effective64 > INT32_MAX)
        return fail(v, RTBPF_VERIFY_BAD_ALU, pc);
    const int32_t effective = (int32_t)effective64;

    switch (src->tag) {
    case V_CONTEXT:
        return verify_context_load(v, s, pc, dst, effective, width);
    case V_PACKET:
        if (effective < 0 || (uint64_t)(uint32_t)effective + width > s->packet_safe_bytes)
            return fail(v, RTBPF_VERIFY_PACKET_BOUNDS, pc);
        *dst = scalar_unknown(); return RTBPF_VERIFY_OK;
    case V_STACK:
        if (!stack_range(v->program, effective, width))
            return fail(v, RTBPF_VERIFY_STACK_BOUNDS, pc);
        if (!stack_is_initialized(s, (uint32_t)effective, width))
            return fail(v, RTBPF_VERIFY_STACK_UNINITIALIZED, pc);
        *dst = scalar_unknown(); return RTBPF_VERIFY_OK;
    case V_MAP_VALUE: {
        const rtbpf_map_t *map = v->program->maps[src->map_index];
        if (!map_range(map, effective, width))
            return fail(v, RTBPF_VERIFY_MAP_BOUNDS, pc);
        *dst = scalar_unknown(); return RTBPF_VERIFY_OK;
    }
    case V_MAP_VALUE_OR_NULL:
        return fail(v, RTBPF_VERIFY_BAD_MAP, pc);
    case V_UNINIT:
        return fail(v, RTBPF_VERIFY_UNINITIALIZED, pc);
    default:
        return fail(v, RTBPF_VERIFY_BAD_ALU, pc);
    }
}

static rtbpf_verify_status_t verify_store(verifier_t *v, vstate_t *s, uint32_t pc,
                                          const vreg_t *dst, int16_t insn_offset,
                                          uint32_t width)
{
    const int64_t effective64 = (int64_t)dst->offset + insn_offset;
    if (effective64 < INT32_MIN || effective64 > INT32_MAX)
        return fail(v, RTBPF_VERIFY_BAD_ALU, pc);
    const int32_t effective = (int32_t)effective64;
    if (dst->tag == V_STACK) {
        if (!stack_range(v->program, effective, width))
            return fail(v, RTBPF_VERIFY_STACK_BOUNDS, pc);
        stack_mark_initialized(s, (uint32_t)effective, width);
        return RTBPF_VERIFY_OK;
    }
    if (dst->tag == V_MAP_VALUE) {
        rtbpf_map_t *map = v->program->maps[dst->map_index];
        if (!map_range(map, effective, width))
            return fail(v, RTBPF_VERIFY_MAP_BOUNDS, pc);
        if ((map->flags & RTBPF_MAP_F_PROGRAM_WRITE) == 0u)
            return fail(v, RTBPF_VERIFY_MAP_READ_ONLY, pc);
        return RTBPF_VERIFY_OK;
    }
    if (dst->tag == V_MAP_VALUE_OR_NULL) return fail(v, RTBPF_VERIFY_BAD_MAP, pc);
    if (dst->tag == V_UNINIT) return fail(v, RTBPF_VERIFY_UNINITIALIZED, pc);
    return fail(v, RTBPF_VERIFY_BAD_ALU, pc);
}

static rtbpf_verify_status_t add_successor(verifier_t *v, vstate_t *s,
                                           uint32_t pc)
{
    s->pc = pc;
    if (push(v, s) != 0) return fail(v, RTBPF_VERIFY_STATE_LIMIT, pc);
    return RTBPF_VERIFY_OK;
}

rtbpf_verify_status_t rtbpf_verify_program(const rtbpf_program_t *program,
                                            rtbpf_verify_report_t *report)
{
    rtbpf_verify_report_t local_report;
    if (report == NULL) report = &local_report;
    memset(report, 0, sizeof(*report));
    verifier_t v;
    memset(&v, 0, sizeof(v));
    v.program = program;
    v.report = report;

    rtbpf_verify_status_t status = structural_pass(&v);
    if (status != RTBPF_VERIFY_OK) return status;

    vstate_t initial;
    memset(&initial, 0, sizeof(initial));
    initial.reg[1].tag = V_CONTEXT;
    initial.reg[10].tag = V_STACK;
    initial.reg[10].offset = program->stack_size;
    if (push(&v, &initial) != 0) return fail(&v, RTBPF_VERIFY_STATE_LIMIT, 0u);

    while (v.work_count != 0u) {
        vstate_t s = v.work[--v.work_count];
        const uint32_t pc = s.pc;
        if (pc >= program->instruction_count)
            return fail(&v, RTBPF_VERIFY_FALLTHROUGH, pc);
        const rtbpf_insn_t *insn = &program->instructions[pc];
        const uint8_t dst_index = RTBPF_INSN_DST(insn);
        const uint8_t src_index = RTBPF_INSN_SRC(insn);
        vreg_t *dst = &s.reg[dst_index];
        const vreg_t *src = &s.reg[src_index];
        s.depth++;
        if (s.depth > v.maximum_path) v.maximum_path = s.depth;
        uint32_t next = pc + 1u;

        switch (insn->opcode) {
        case RTBPF_OP_MOV64_IMM:
            *dst = scalar_known((uint64_t)(int64_t)insn->immediate); break;
        case RTBPF_OP_MOV64_REG:
            if (src->tag == V_UNINIT) return fail(&v, RTBPF_VERIFY_UNINITIALIZED, pc);
            *dst = *src; break;
        case RTBPF_OP_ADD64_IMM:
            if (dst->tag == V_SCALAR) {
                if (dst->known) dst->value += (uint64_t)(int64_t)insn->immediate;
            } else if (dst->tag == V_PACKET || dst->tag == V_STACK ||
                       dst->tag == V_MAP_VALUE) {
                const int64_t value = (int64_t)dst->offset + insn->immediate;
                if (value < INT32_MIN || value > INT32_MAX ||
                    (dst->tag == V_PACKET && value < 0))
                    return fail(&v, RTBPF_VERIFY_BAD_ALU, pc);
                dst->offset = (int32_t)value;
            } else return fail(&v, dst->tag == V_UNINIT ?
                                      RTBPF_VERIFY_UNINITIALIZED : RTBPF_VERIFY_BAD_ALU, pc);
            break;
        case RTBPF_OP_ADD64_REG:
            if (dst->tag == V_UNINIT || src->tag == V_UNINIT)
                return fail(&v, RTBPF_VERIFY_UNINITIALIZED, pc);
            if (dst->tag != V_SCALAR || src->tag != V_SCALAR)
                return fail(&v, RTBPF_VERIFY_BAD_ALU, pc);
            if (dst->known && src->known) dst->value += src->value;
            else dst->known = 0u;
            break;
        case RTBPF_OP_LDX_B:
            status = verify_load(&v, &s, pc, dst, src, insn->offset, 1u); break;
        case RTBPF_OP_LDX_H:
            status = verify_load(&v, &s, pc, dst, src, insn->offset, 2u); break;
        case RTBPF_OP_LDX_W:
            status = verify_load(&v, &s, pc, dst, src, insn->offset, 4u); break;
        case RTBPF_OP_LDX_DW:
            status = verify_load(&v, &s, pc, dst, src, insn->offset, 8u); break;
        case RTBPF_OP_ST_W:
            status = verify_store(&v, &s, pc, dst, insn->offset, 4u); break;
        case RTBPF_OP_STX_W:
        case RTBPF_OP_STX_DW:
            if (src->tag == V_UNINIT) return fail(&v, RTBPF_VERIFY_UNINITIALIZED, pc);
            if (src->tag != V_SCALAR) return fail(&v, RTBPF_VERIFY_BAD_ALU, pc);
            status = verify_store(&v, &s, pc, dst, insn->offset,
                                  insn->opcode == RTBPF_OP_STX_W ? 4u : 8u);
            break;
        case RTBPF_OP_LD_MAP:
            memset(dst, 0, sizeof(*dst));
            dst->tag = V_MAP_HANDLE;
            dst->map_index = (uint16_t)insn->immediate;
            break;
        case RTBPF_OP_CALL:
            if (s.reg[1].tag != V_MAP_HANDLE || s.reg[2].tag != V_STACK)
                return fail(&v, RTBPF_VERIFY_BAD_HELPER, pc);
            if (!stack_range(program, s.reg[2].offset, sizeof(uint32_t)) ||
                !stack_is_initialized(&s, (uint32_t)s.reg[2].offset, sizeof(uint32_t)))
                return fail(&v, RTBPF_VERIFY_BAD_HELPER, pc);
            memset(&s.reg[0], 0, sizeof(s.reg[0]));
            s.reg[0].tag = V_MAP_VALUE_OR_NULL;
            s.reg[0].map_index = s.reg[1].map_index;
            break;
        case RTBPF_OP_JA: {
            uint32_t target = 0u; (void)jump_target(program, pc, insn->offset, &target);
            status = add_successor(&v, &s, target);
            if (status != RTBPF_VERIFY_OK) return status;
            continue;
        }
        case RTBPF_OP_JEQ_IMM:
        case RTBPF_OP_JNE_IMM: {
            const int is_equal_jump = insn->opcode == RTBPF_OP_JEQ_IMM;
            uint32_t target = 0u; (void)jump_target(program, pc, insn->offset, &target);
            if (dst->tag == V_MAP_VALUE_OR_NULL && insn->immediate == 0) {
                vstate_t equal_state = s, nonnull_state = s;
                equal_state.reg[dst_index] = scalar_known(0u);
                nonnull_state.reg[dst_index].tag = V_MAP_VALUE;
                if (is_equal_jump) {
                    if (add_successor(&v, &nonnull_state, next) != RTBPF_VERIFY_OK ||
                        add_successor(&v, &equal_state, target) != RTBPF_VERIFY_OK)
                        return fail(&v, RTBPF_VERIFY_STATE_LIMIT, pc);
                } else {
                    if (add_successor(&v, &equal_state, next) != RTBPF_VERIFY_OK ||
                        add_successor(&v, &nonnull_state, target) != RTBPF_VERIFY_OK)
                        return fail(&v, RTBPF_VERIFY_STATE_LIMIT, pc);
                }
                continue;
            }
            if (dst->tag == V_UNINIT) return fail(&v, RTBPF_VERIFY_UNINITIALIZED, pc);
            if (dst->tag != V_SCALAR) return fail(&v, RTBPF_VERIFY_BAD_ALU, pc);
            if (dst->known) {
                const int equal = dst->value == (uint64_t)(int64_t)insn->immediate;
                const int take = is_equal_jump ? equal : !equal;
                status = add_successor(&v, &s, take ? target : next);
                if (status != RTBPF_VERIFY_OK) return status;
            } else {
                vstate_t branch_state = s;
                if (add_successor(&v, &s, next) != RTBPF_VERIFY_OK ||
                    add_successor(&v, &branch_state, target) != RTBPF_VERIFY_OK)
                    return fail(&v, RTBPF_VERIFY_STATE_LIMIT, pc);
            }
            continue;
        }
        case RTBPF_OP_JGT_REG: {
            if (dst->tag == V_PACKET && src->tag == V_PACKET_END) {
                uint32_t target = 0u; (void)jump_target(program, pc, insn->offset, &target);
                vstate_t false_state = s;
                if (dst->offset < 0) return fail(&v, RTBPF_VERIFY_BAD_ALU, pc);
                if ((uint32_t)dst->offset > false_state.packet_safe_bytes)
                    false_state.packet_safe_bytes = (uint32_t)dst->offset;
                if (add_successor(&v, &false_state, next) != RTBPF_VERIFY_OK ||
                    add_successor(&v, &s, target) != RTBPF_VERIFY_OK)
                    return fail(&v, RTBPF_VERIFY_STATE_LIMIT, pc);
                continue;
            }
            if (dst->tag == V_UNINIT || src->tag == V_UNINIT)
                return fail(&v, RTBPF_VERIFY_UNINITIALIZED, pc);
            if (dst->tag != V_SCALAR || src->tag != V_SCALAR)
                return fail(&v, RTBPF_VERIFY_BAD_ALU, pc);
            uint32_t target = 0u; (void)jump_target(program, pc, insn->offset, &target);
            if (dst->known && src->known) {
                status = add_successor(&v, &s, dst->value > src->value ? target : next);
                if (status != RTBPF_VERIFY_OK) return status;
            } else {
                vstate_t branch_state = s;
                if (add_successor(&v, &s, next) != RTBPF_VERIFY_OK ||
                    add_successor(&v, &branch_state, target) != RTBPF_VERIFY_OK)
                    return fail(&v, RTBPF_VERIFY_STATE_LIMIT, pc);
            }
            continue;
        }
        case RTBPF_OP_EXIT:
            if (s.reg[0].tag != V_SCALAR || !s.reg[0].known ||
                (s.reg[0].value != RTBPF_NET_PASS && s.reg[0].value != RTBPF_NET_DROP))
                return fail(&v, RTBPF_VERIFY_BAD_RETURN, pc);
            continue;
        default:
            return fail(&v, RTBPF_VERIFY_BAD_OPCODE, pc);
        }

        if (status != RTBPF_VERIFY_OK) return status;
        if (next >= program->instruction_count)
            return fail(&v, RTBPF_VERIFY_FALLTHROUGH, pc);
        s.pc = next;
        if (push(&v, &s) != 0) return fail(&v, RTBPF_VERIFY_STATE_LIMIT, pc);
    }

    if (v.maximum_path > program->maximum_executed_instructions)
        return fail(&v, RTBPF_VERIFY_BUDGET_TOO_SMALL, 0u);
    report->status = RTBPF_VERIFY_OK;
    report->instruction_index = 0u;
    report->maximum_path_instructions = v.maximum_path;
    report->explored_states = v.generated_states;
    return RTBPF_VERIFY_OK;
}

const char *rtbpf_verify_status_string(rtbpf_verify_status_t status)
{
    switch (status) {
    case RTBPF_VERIFY_OK: return "ok";
    case RTBPF_VERIFY_BAD_ARGUMENT: return "bad argument";
    case RTBPF_VERIFY_TOO_LARGE: return "program too large";
    case RTBPF_VERIFY_BAD_OPCODE: return "bad opcode";
    case RTBPF_VERIFY_BAD_REGISTER: return "bad register";
    case RTBPF_VERIFY_BAD_JUMP: return "bad jump";
    case RTBPF_VERIFY_BACK_EDGE: return "back edge";
    case RTBPF_VERIFY_FALLTHROUGH: return "fallthrough";
    case RTBPF_VERIFY_UNINITIALIZED: return "uninitialized value";
    case RTBPF_VERIFY_BAD_CONTEXT_ACCESS: return "bad context access";
    case RTBPF_VERIFY_PACKET_BOUNDS: return "packet bounds not proven";
    case RTBPF_VERIFY_STACK_BOUNDS: return "stack out of bounds";
    case RTBPF_VERIFY_STACK_UNINITIALIZED: return "uninitialized stack";
    case RTBPF_VERIFY_BAD_MAP: return "bad map access";
    case RTBPF_VERIFY_MAP_BOUNDS: return "map value out of bounds";
    case RTBPF_VERIFY_MAP_READ_ONLY: return "map is read only";
    case RTBPF_VERIFY_BAD_HELPER: return "bad helper call";
    case RTBPF_VERIFY_BAD_ALU: return "bad alu operation";
    case RTBPF_VERIFY_BAD_RETURN: return "bad return action";
    case RTBPF_VERIFY_STATE_LIMIT: return "verifier state limit";
    case RTBPF_VERIFY_BUDGET_TOO_SMALL: return "execution budget too small";
    default: return "unknown";
    }
}
