# RTBPF-NET

RTBPF-NET is an MCU-oriented, XDP-like packet-filter runtime for STM32/FreeRTOS. Restricted programs execute at a deferred `NET_RX` hook before lwIP and return `PASS` or `DROP` under fixed memory and instruction budgets.

This repository implements the architecture in `../STM32_DYNAMIC_PACKET_FILTER_FRAMEWORK.md`. The CMS DDoS filter in `../STM32_SIMPLIFIED_DDOS_FILTER_PLAN.md` will be the first full application, but algorithm-specific policy does not belong in the trusted runtime.

## Current status

**Phase 1 complete: static host core.** The repository currently provides:

- stable eight-byte instruction and `RTBPF_NET_MD_V1` definitions;
- a portable interpreter with eleven registers and a 512-byte static runner stack;
- runtime pointer tags and bounds checks for context, packet, stack, and map values;
- an instruction watchdog;
- fixed ARRAY and CONFIG_ARRAY maps with no packet-path allocation;
- a single-runner static `NET_RX` launchpad with action/fault counters;
- compiled-in sample programs;
- host tests, including ASan/UBSan execution.

It does **not** yet include a semantic verifier, ELF packer, dynamic bundle loader, STM32 port, ST67 adapter, signature verification, or persistent update slots.

## Build and test

Requirements: a C11 compiler, `make`, and `ar`.

```sh
make test
make test-sanitize
```

The normal build also produces `build/librtbpf_net.a`.

## Repository layout

```text
include/rtbpf/     public ABI and runtime interfaces
kernel/            interpreter, maps, and NET_RX launchpad
examples/          compiled-in bytecode programs
tests/             host unit and containment tests
 docs/             phase plan, contracts, and completion reports
```

## Safety posture

Phase 1 treats all packet bytes and bytecode behavior as untrusted at runtime. It checks every memory access and enforces an execution limit. This is defense in depth, not load-time proof: only built-in test programs should be attached until the Phase 2 verifier is complete.

No bytecode runs in an ISR. No program receives a native driver pointer. Packet storage is read-only; stores are restricted to the VM stack and writable declared map values.

## License

No license has been selected yet. Do not redistribute until the project owner adds one.
