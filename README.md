# RTBPF-NET

RTBPF-NET is an MCU-oriented, XDP-like packet-filter runtime for STM32/FreeRTOS. Restricted programs execute at a deferred `NET_RX` hook before lwIP and return `PASS` or `DROP` under fixed memory and instruction budgets.

This repository implements [the RTBPF-NET architecture](docs/reference/STM32_DYNAMIC_PACKET_FILTER_FRAMEWORK.md). The [CMS DDoS filter plan](docs/reference/STM32_SIMPLIFIED_DDOS_FILTER_PLAN.md) will be the first full application, but algorithm-specific policy does not belong in the trusted runtime.

## Current status

**Phases 1 and 2 complete: static core, verifier, and authoring prototype.** The repository currently provides:

- stable eight-byte instruction and `RTBPF_NET_MD_V1` definitions;
- a portable interpreter with eleven registers and a 512-byte static runner stack;
- runtime pointer tags and bounds checks for context, packet, stack, and map values;
- an instruction watchdog;
- a load-time DAG verifier for register, stack, packet, map, helper, return, and path-budget safety;
- fixed ARRAY and CONFIG_ARRAY maps with no packet-path allocation;
- a single-runner static `NET_RX` launchpad with mandatory verification and fault counters;
- a restricted program SDK and example C filter;
- a strict ELF inspector and prototype compact bundle packer;
- negative, packer, sanitizer, and fuzz-smoke tests.

It does **not** yet include a dynamic target bundle loader, STM32 port, ST67 adapter, signature verification, or persistent update slots.

## Build and test

Requirements: a C11 compiler, `make`, and `ar`.

```sh
make test
make test-sanitize
make fuzz-smoke
```

The normal build also produces `build/librtbpf_net.a`. Inspect or pack a compatible BPF ELF object with:

```sh
python3 tools/rtbpf_pack.py inspect program.o
python3 tools/rtbpf_pack.py pack program.o -o program.rtbpf
```

The Phase-2 bundle is a host-tool prototype and is not yet accepted by target firmware.

## Repository layout

```text
include/rtbpf/     public ABI and runtime interfaces
kernel/            interpreter, verifier, maps, and NET_RX launchpad
examples/          compiled-in bytecode programs
sdk/               restricted program-facing headers and C examples
tools/             host ELF inspector and prototype bundle packer
tests/             unit, negative, sanitizer, packer, and fuzz entry points
docs/              phase plan, contracts, formats, and completion reports
```

## Safety posture

All packet bytes and bytecode behavior are treated as untrusted. Static attachment now requires load-time semantic verification. The interpreter still checks every memory access and enforces an execution limit as defense in depth. Dynamic external loading remains disabled until the transactional target loader is complete.

No bytecode runs in an ISR. No program receives a native driver pointer. Packet storage is read-only; stores are restricted to the VM stack and writable declared map values.

## License

No license has been selected yet. Do not redistribute until the project owner adds one.
