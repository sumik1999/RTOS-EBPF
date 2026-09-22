#ifndef RTBPF_ABI_H
#define RTBPF_ABI_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Eight-byte, eBPF-shaped instruction. The Phase-1 subset is intentionally small. */
typedef struct __attribute__((packed)) {
    uint8_t opcode;
    uint8_t dst_src; /* low nibble: dst, high nibble: src */
    int16_t offset;
    int32_t immediate;
} rtbpf_insn_t;

_Static_assert(sizeof(rtbpf_insn_t) == 8u, "RTBPF instructions must be eight bytes");

#define RTBPF_INSN_DST(i) ((uint8_t)((i)->dst_src & 0x0fu))
#define RTBPF_INSN_SRC(i) ((uint8_t)(((i)->dst_src >> 4u) & 0x0fu))
#define RTBPF_REGS 11u
#define RTBPF_FP_REG 10u

/* Values follow eBPF where practical. LD_MAP is an RTBPF-only resolved map index. */
enum rtbpf_opcode {
    RTBPF_OP_ADD64_IMM = 0x07,
    RTBPF_OP_ADD64_REG = 0x0f,
    RTBPF_OP_MOV64_IMM = 0xb7,
    RTBPF_OP_MOV64_REG = 0xbf,

    RTBPF_OP_LDX_B     = 0x71,
    RTBPF_OP_LDX_H     = 0x69,
    RTBPF_OP_LDX_W     = 0x61,
    RTBPF_OP_LDX_DW    = 0x79,
    RTBPF_OP_ST_W      = 0x62,
    RTBPF_OP_STX_W     = 0x63,
    RTBPF_OP_STX_DW    = 0x7b,

    RTBPF_OP_JA        = 0x05,
    RTBPF_OP_JEQ_IMM   = 0x15,
    RTBPF_OP_JNE_IMM   = 0x55,
    RTBPF_OP_JGT_REG   = 0x2d,
    RTBPF_OP_CALL      = 0x85,
    RTBPF_OP_EXIT      = 0x95,

    RTBPF_OP_LD_MAP    = 0xd0,
};

#define RTBPF_INSN(code, dst, src, off, imm) \
    ((rtbpf_insn_t){(uint8_t)(code), (uint8_t)(((src) << 4u) | (dst)), \
                    (int16_t)(off), (int32_t)(imm)})

#define RTBPF_MOV64_IMM(dst, imm) RTBPF_INSN(RTBPF_OP_MOV64_IMM, dst, 0, 0, imm)
#define RTBPF_MOV64_REG(dst, src) RTBPF_INSN(RTBPF_OP_MOV64_REG, dst, src, 0, 0)
#define RTBPF_ADD64_IMM(dst, imm) RTBPF_INSN(RTBPF_OP_ADD64_IMM, dst, 0, 0, imm)
#define RTBPF_ADD64_REG(dst, src) RTBPF_INSN(RTBPF_OP_ADD64_REG, dst, src, 0, 0)
#define RTBPF_LDX_B(dst, src, off) RTBPF_INSN(RTBPF_OP_LDX_B, dst, src, off, 0)
#define RTBPF_LDX_H(dst, src, off) RTBPF_INSN(RTBPF_OP_LDX_H, dst, src, off, 0)
#define RTBPF_LDX_W(dst, src, off) RTBPF_INSN(RTBPF_OP_LDX_W, dst, src, off, 0)
#define RTBPF_LDX_DW(dst, src, off) RTBPF_INSN(RTBPF_OP_LDX_DW, dst, src, off, 0)
#define RTBPF_ST_W(dst, off, imm) RTBPF_INSN(RTBPF_OP_ST_W, dst, 0, off, imm)
#define RTBPF_STX_W(dst, src, off) RTBPF_INSN(RTBPF_OP_STX_W, dst, src, off, 0)
#define RTBPF_STX_DW(dst, src, off) RTBPF_INSN(RTBPF_OP_STX_DW, dst, src, off, 0)
#define RTBPF_JA(off) RTBPF_INSN(RTBPF_OP_JA, 0, 0, off, 0)
#define RTBPF_JEQ_IMM(dst, imm, off) RTBPF_INSN(RTBPF_OP_JEQ_IMM, dst, 0, off, imm)
#define RTBPF_JNE_IMM(dst, imm, off) RTBPF_INSN(RTBPF_OP_JNE_IMM, dst, 0, off, imm)
#define RTBPF_JGT_REG(dst, src, off) RTBPF_INSN(RTBPF_OP_JGT_REG, dst, src, off, 0)
#define RTBPF_CALL(id) RTBPF_INSN(RTBPF_OP_CALL, 0, 0, 0, id)
#define RTBPF_EXIT() RTBPF_INSN(RTBPF_OP_EXIT, 0, 0, 0, 0)
#define RTBPF_LD_MAP(dst, index) RTBPF_INSN(RTBPF_OP_LD_MAP, dst, 0, 0, index)

#ifdef __cplusplus
}
#endif
#endif
