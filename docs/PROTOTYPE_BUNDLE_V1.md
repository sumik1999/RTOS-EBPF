# Prototype compact bundle V1

Status: **host-tool prototype only**. The target must not load this format until the Phase 5 decoder, resource tables, relocations, authentication trailer, and transactional policy are implemented.

`tools/rtbpf_pack.py` accepts one ELF64 little-endian relocatable `EM_BPF` object containing exactly one `rtbpf/net_rx` PROGBITS section. Program relocations are rejected in Phase 2.

The output begins with this canonical 64-byte little-endian header:

| Offset | Width | Field |
|---:|---:|---|
| 0 | 8 | ASCII magic `RTBPFNET` |
| 8 | 2 | format version, currently 1 |
| 10 | 2 | header size, currently 64 |
| 12 | 4 | flags, currently zero |
| 16 | 4 | instruction count |
| 20 | 4 | instruction byte offset |
| 24 | 4 | instruction byte length |
| 28 | 4 | preliminary maximum-path value |
| 32 | 32 | SHA-256 of instruction bytes |
| 64 | N | packed eight-byte instructions |

The preliminary maximum-path field is not trusted and currently equals instruction count. A production packer must use the shared semantic verifier's report. The target must independently recompute verification and admission results.

Missing from this prototype:

- program type/context/link/action manifest;
- map definitions and initial data;
- map/helper relocations;
- stack and aggregate memory requirements;
- helper ABI versions and costs;
- bundle UUID and anti-rollback generation;
- signature/key metadata;
- canonical multi-program tables.

The format will change incompatibly before Phase 5. It exists now to test strict ELF inspection, canonical output, checked offsets, deterministic hashing, and malformed-input handling without putting an ELF parser on the STM32.
