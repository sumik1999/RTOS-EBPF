# Phase 3 implementation report

Date: 2026-09-23

Status: **software implementation complete; hardware gate pending**

## Objective

Port the verified static runtime toward STM32H563/FreeRTOS and place the `NET_RX` hook in the ST67W611M1 T02 receive path before lwIP while preserving exactly-once buffer ownership.

## Official source inspected

The implementation was checked against ST's public `x-cube-st67w61` repository at commit `572b72d` (X-CUBE-ST67W61 V1.3.0 layout):

```text
Projects/NUCLEO-H563ZI/Applications/ST67W6X/ST67W6X_CLI_LWIP/LWIP/App/lwip_netif.c
Projects/NUCLEO-H563ZI/Applications/ST67W6X/ST67W6X_CLI_LWIP/LWIP/App/lwip.c
Middlewares/ST/ST67W6X_Network_Driver/Core/w6x_netif.c
Middlewares/ST/ST67W6X_Network_Driver/Api/w6x_api.h
```

The source establishes that:

- T02 `W6X_Netif_input` retrieves station/AP network traffic directly from the SPI receive queue;
- the returned payload is passed unchanged to a `PBUF_RAW` custom pbuf;
- the interface is configured for Ethernet/ARP and feeds `tcpip_input`;
- `W6X_Netif_free` is the release operation for the internal SPI buffer;
- the receive processor runs in the deferred NET_IF FreeRTOS task, not an ISR.

This freezes the initial profile as `RTBPF_LINK_WIFI_ETHERNET` and the hook as the point immediately after successful `W6X_Netif_input` and before pbuf/lwIP handoff.

## Delivered

### Portable RX ownership adapter

Added `kernel/rx_adapter.c` and `include/rtbpf/rx_adapter.h`:

- allocation-free per-packet execution;
- explicit handoff result distinguishing ownership taken from ownership rejected;
- exactly one release on filter drop, handoff rejection, invalid frame, wrong execution context, disabled/reset state, or cache-preparation failure;
- no-program pass behavior inherited from the hook;
- VM abort detection and fail-closed behavior;
- ingress/link metadata construction;
- optional ISR, cache-preparation, and cycle callbacks;
- fixed counters for frames, bytes, results, releases, VM faults, and cycle distributions.

### ST67 station/AP adapter

Added `adapters/st67w61/rtbpf_st67_adapter.c`:

- station and soft-AP interface mapping;
- independent hooks/runners and callback contexts;
- logical ingress ports 1 and 2;
- normalized Wi-Fi Ethernet link profile;
- unknown-link rejection and release;
- common enable/disable control for reset sequencing.

### STM32H5 port

Added `ports/stm32h5/rtbpf_port_stm32h5.c`:

- DWT `CYCCNT` initialization and wrap-safe 32-bit samples;
- `IPSR` task/ISR detection;
- synchronization barrier before packet access;
- optional outward-rounded 32-byte D-cache invalidation controlled by `RTBPF_STM32_RX_NEEDS_DCACHE_INVALIDATE`.

Cache invalidation defaults off because the ST SPI driver must complete DMA/cache ownership before enqueueing the pointer. Hardware/source inspection must decide whether an extra invalidation is required.

### X-CUBE lwIP glue

Added `integration/x-cube-st67w61/rtbpf_st67_lwip_glue.c`:

- calls `W6X_Netif_input` in the existing NET_IF task;
- runs the verified hook before lwIP;
- uses a fixed 16-entry custom-pbuf wrapper pool instead of per-frame `pvPortMalloc`;
- retains the ST internal buffer for passed packets until lwIP's custom free callback;
- releases dropped/rejected packets immediately;
- returns a positive consumed length for filtered frames so the existing NET_IF task continues draining its queue;
- exposes adapter statistics and reset enable/disable controls.

The integration guide documents exact edits to the official `ST67W6X_CLI_LWIP` project.

The official V1.3.0 reference error path frees the W6 buffer directly and then invokes its custom-pbuf free routine, which frees the same W6 buffer again. The replacement uses one terminal release path and has a regression test for lwIP rejection.

## Host qualification

New tests use mock W6, FreeRTOS, lwIP, ISR, cache, cycle, and ownership interfaces while compiling the actual integration glue.

Covered behavior:

- pass and delayed lwIP release;
- filter drop;
- VM runtime fault after simulated code corruption;
- lwIP input rejection;
- pbuf-wrapper pool exhaustion;
- invalid/oversized frames;
- ISR-call rejection;
- cache-preparation failure;
- disabled/reset state;
- station/AP context mapping;
- unknown ST link;
- exactly one terminal transition for every owned mock buffer;
- cycle maximum accounting.

Results:

```text
Phase 1 tests:       40 checks, 0 failures
Verifier tests:      22 checks, 0 failures
RX adapter tests:    31 checks, 0 failures
ST67 glue tests:     50 checks, 0 failures
Packer tests:        14 checks, 0 failures
Total:              157 checks, 0 failures

ASan/UBSan: all C test suites passed
Verifier fuzz smoke: passed
```

## Hardware gate still open

No NUCLEO-H563ZI, X-NUCLEO-67W61M1, T02 firmware image, STM32CubeIDE project, or debug probe is available in the current environment. Consequently these Phase 3 exit requirements cannot honestly be marked complete:

1. Compile/link inside the exact X-CUBE V1.3.0 H563 project.
2. Capture and confirm real Ethernet frame start/length.
3. Prove SPI DMA/cache completion before the hook.
4. Flash and run constant pass/drop programs.
5. Instrument W6 buffers across disconnect, reset, queue full, and pbuf-pool exhaustion.
6. Measure DWT cycles under minimum-frame flood and concurrent RTOS interrupts.
7. Tune pbuf wrapper count and program budget from measurements.

## Phase decision

**CONDITIONAL PASS.** The portable adapter, STM32 port, exact X-CUBE glue, ownership model, and host conformance tests are implemented. Phase 4 application work can proceed on the host, but hardware claims and production attachment remain blocked until the Phase 3 hardware checklist passes.
