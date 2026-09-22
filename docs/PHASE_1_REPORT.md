# Phase 1 completion report

Date: 2026-09-23

## Objective

Create an independent RTBPF-NET repository and implement the static, allocation-free host core needed before the verifier, dynamic loader, and STM32 adapter.

## Delivered

### Repository and build

- Initialized a standalone Git repository on branch `main`.
- Added a dependency-free C11 build using `make` and `ar`.
- Added normal and ASan/UBSan test targets.

### Stable primitives

- Eight-byte eBPF-shaped instruction representation.
- V1 link types and `PASS`, `DROP`, `ABORTED` values.
- Frozen 40-byte `rtbpf_net_md_v1_t` with compile-time size/offset assertions.
- Immutable static program descriptor with instruction and map budgets.

### Runtime

- Portable switch interpreter for the initial arithmetic, move, memory, jump, helper, map, and exit subset.
- Eleven tagged registers and one caller-owned 512-byte runner stack.
- Runtime capability checks for context, packet, stack, map handles, and map values.
- Read-only packet enforcement and map write-flag enforcement.
- Signed branch-target checks and execution watchdog.
- Status/fault reporting with no direct bytecode pointer dereference.

### Maps and hook

- Statically backed ARRAY and CONFIG_ARRAY maps.
- Constant-time indexed lookup with checked dimensions.
- `map_lookup` helper available to bytecode.
- Static single-runner NET_RX attach/detach/run interface.
- Packet/action/fault/instruction counters and link-profile rejection.
- No-program direct-pass behavior.

### Examples and tests

- Constant-pass built-in program.
- Bounds-checked first-byte filter.
- Bytecode map counter.
- Runtime containment tests for out-of-bounds packet access, read-only map writes, invalid opcode, wrong link profile, and an intentional infinite loop.

## Test evidence

Commands:

```text
make test
make test-sanitize
```

Result:

```text
Phase 1 tests: 40 checks, 0 failures
Phase 1 tests under AddressSanitizer/UndefinedBehaviorSanitizer: 40 checks, 0 failures
```

Compiler flags include C11, warnings-as-errors, extra warnings, and pedantic diagnostics.

## Limitations carried into Phase 2+

- No semantic verifier: only trusted compiled-in programs should be attached.
- No clang/ELF SDK pipeline or compact bundle parser.
- Static attachment is not concurrency-safe dynamic replacement.
- No STM32/FreeRTOS port or ST67 driver adapter exists yet.
- No cryptographic signature, persistent slot, or anti-rollback support.
- Instruction subset is sufficient for Phase 1 examples, not yet the full DDoS parser.
- Cycle accounting is instruction based; DWT measurements wait for the target port.

## Phase decision

**PASS.** The static host core meets the Phase 1 gate. Begin Phase 2 with structural verification before expanding the ISA or accepting external programs.
