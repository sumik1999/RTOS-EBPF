# Phase 2 completion report

Date: 2026-09-23

## Objective

Add mandatory load-time safety verification and establish the first restricted-C/ELF/compact-bundle authoring path without trusting or parsing general ELF on the STM32.

## Delivered

### Semantic verifier

- Structural opcode/register validation.
- Checked branch targets and rejection of backward/self edges.
- Finite DAG exploration with queued-state and processing-step limits.
- Initialized register tracking.
- Byte-granular stack initialization and bounds tracking.
- Exact V1 context field/width allowlist.
- Packet capability provenance and `data_end` branch refinement.
- Map-handle identity, nullable lookup refinement, map-value bounds, and write permissions.
- Typed `map_lookup` helper validation.
- Known `PASS`/`DROP` return enforcement on every exit path.
- Longest explored instruction-path calculation and declared-budget admission.
- Machine-readable reports containing status, instruction index, path bound, and explored-state count.

Static attachment now invokes the verifier. Rejected programs never become active. Runtime tags, bounds checks, and the instruction watchdog remain enabled as defense in depth.

### Restricted SDK

- Added `sdk/include/rtbpf_net_sdk.h` with fixed-width V1 context, actions, link values, helper declaration, and map declaration shape.
- Added `sdk/examples/drop_first_ff.c` as a restricted-C packet-filter example.
- Kept RTOS, driver, and native runtime structures outside the program SDK.

### ELF inspector and packer prototype

- Added a dependency-free Python tool at `tools/rtbpf_pack.py`.
- Accepts only ELF64, little-endian, relocatable `EM_BPF` objects.
- Requires exactly one `rtbpf/net_rx` instruction section.
- Checks section bounds, instruction width/count, opcodes, registers, helper IDs, and forward branch targets.
- Rejects program relocations until map/helper relocation semantics are frozen.
- Emits a deterministic prototype `.rtbpf` image with a canonical 64-byte header and SHA-256 instruction digest.
- Documented the deliberately incomplete format in `docs/PROTOTYPE_BUNDLE_V1.md`.

The prototype bundle is not yet target-loadable or signed. That remains Phase 5/6 work.

### Negative corpus and fuzz entry point

Tests cover:

- bad/out-of-range/backward jumps;
- control-flow fallthrough;
- unknown helpers/opcodes;
- uninitialized registers and stack bytes;
- non-allowlisted context access;
- packet reads without a `data_end` proof;
- stack and map-value out-of-bounds access;
- nullable map access;
- writes to read-only maps;
- unknown return actions;
- insufficient execution budgets;
- malformed/truncated ELF and unsupported packed instructions.

`tests/fuzz_verifier.c` accepts arbitrary instruction bytes from standard input, enabling a smoke test now and libFuzzer/AFL integration later.

## Test evidence

```text
make test
  Phase 1 tests: 40 checks, 0 failures
  Verifier tests: 22 checks, 0 failures
  Packer tests: 14 checks, 0 failures

make test-sanitize
  Phase 1 ASan/UBSan: 40 checks, 0 failures
  Verifier ASan/UBSan: 22 checks, 0 failures

make fuzz-smoke
  completed without crash
```

All C builds use C11, warnings-as-errors, extra warnings, and pedantic diagnostics.

## Known limitations

- Abstract states are explored separately rather than merged; safe branch-heavy programs can hit the bounded state/step policy and be rejected.
- Scalar tracking is constant/unknown rather than full signed/unsigned intervals.
- Only the current Phase-1 ISA/helper subset is accepted.
- No bounded loops, pointer spills, packet writes, HASH maps, or multiple packet objects.
- The packer does not yet consume map/helper relocations or invoke the C semantic verifier directly.
- No target bundle decoder or dynamic loader exists.
- The SDK example requires a clang build with the BPF target, which is not installed/validated by this host environment.
- Target WCET still requires STM32 DWT measurements; Phase 2 calculates instruction paths only.

## Phase decision

**PASS.** Compiled-in programs must pass semantic verification before attachment, and the restricted authoring/packing boundary is established. Proceed to Phase 3: STM32H563/FreeRTOS portability and the ST67 T02 pre-lwIP adapter.
