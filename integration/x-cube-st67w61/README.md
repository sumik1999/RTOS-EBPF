# X-CUBE-ST67W61 T02 integration

This integration targets the official X-CUBE-ST67W61 **V1.3.0** layout inspected at Git commit `572b72d`, specifically:

```text
Projects/NUCLEO-H563ZI/Applications/ST67W6X/ST67W6X_CLI_LWIP/
Middlewares/ST/ST67W6X_Network_Driver/
```

The official T02 path proves the packet ABI:

1. `W6X_Netif_input(link_id, &buffer, &payload)` calls `BusIo_SPI_ReceivePtr`.
2. Its positive return value is the complete payload length.
3. `payload` is passed directly to `pbuf_alloced_custom(PBUF_RAW, ...)`.
4. The lwIP interface has `NETIF_FLAG_ETHARP` and `etharp_output`, so this payload is an Ethernet frame.
5. `W6X_Netif_free(buffer)` is the terminal release operation for the internal SPI RX buffer.

Therefore the hook location is immediately after a successful `W6X_Netif_input` and before pbuf construction / `netif->input`.

## Files

- `rtbpf_st67_lwip_glue.c/.h`: X-CUBE/FreeRTOS/lwIP glue.
- `../../ports/stm32h5/rtbpf_port_stm32h5.c`: DWT cycle clock, ISR detection, and optional D-cache preparation.
- `../../adapters/st67w61/rtbpf_st67_adapter.c`: portable station/AP mapping and ownership state machine.

The glue uses a fixed pbuf-wrapper pool rather than `pvPortMalloc` per passed frame. Packet bytes remain in the ST driver buffer and are released by the custom pbuf callback when lwIP finishes.

## Project changes

Add the RTBPF core, adapter, STM32 port, and glue sources to the `ST67W6X_CLI_LWIP` project. Add include paths for:

```text
rtbpf-net/include
rtbpf-net/examples                  # only for the built-in bring-up program
rtbpf-net/ports/stm32h5
rtbpf-net/integration/x-cube-st67w61
```

### 1. Initialize before the NET_IF task starts

In `LWIP/App/lwip.c`, after both lwIP interfaces have been added and before `net_if_init(&net_if_cb)` creates the NET_IF receive task:

```c
#include "rtbpf_st67_lwip_glue.h"
#include "static_programs.h"

/* ... both netifapi_netif_add calls and netif_set_up ... */
if (rtbpf_st67_lwip_init(netif_get_interface(NETIF_STA),
                         netif_get_interface(NETIF_AP),
                         &rtbpf_example_pass_program,
                         &rtbpf_example_pass_program) != 0)
{
    LogError("RTBPF-NET initialization failed\n");
    return -1;
}

if (net_if_init(&net_if_cb) != 0)
{
    /* existing error path */
}
```

Use the constant-pass program for first hardware bring-up. Replace it with the checked first-byte program only after frame captures prove byte zero is the Ethernet destination MAC.

### 2. Replace the original RX processor

In `LWIP/App/lwip_netif.c`, replace the original `netif_rx_process` body with:

```c
#include "rtbpf_st67_lwip_glue.h"

static int32_t netif_rx_process(uint32_t link_id)
{
    return rtbpf_st67_lwip_rx_process(link_id);
}
```

`NETIF_STA` and `NETIF_AP` in X-CUBE V1.3.0 are expected to map to RTBPF indexes 0 and 1. Add build-time assertions beside the replacement:

```c
_Static_assert(NETIF_STA == RTBPF_ST67_STA_INDEX, "STA link index changed");
_Static_assert(NETIF_AP == RTBPF_ST67_AP_INDEX, "AP link index changed");
```

Remove the now-unused original `netif_pbuf_t`, `netif_pbuf_alloc`, and `netif_pbuf_free` declarations/definitions. The RTBPF glue supplies the static pbuf-wrapper pool and free callback.

Do not place the hook in the SPI_RDY EXTI, SPI DMA callback, or W6X notification callback. Those paths only wake the existing NET_IF task.

### 3. Reset/disconnect sequencing

Before a module reset or NET_IF teardown:

```c
rtbpf_st67_lwip_set_enabled(0);
```

Drain/cancel the ST NET_IF queues according to the X-CUBE reset path. Re-enable only after W6X NET_IF and both lwIP interfaces are valid:

```c
rtbpf_st67_lwip_set_enabled(1);
```

Frames received while disabled are released exactly once and never reach lwIP.

## Ownership behavior

| Path | Terminal operation |
|---|---|
| Filter `DROP` or VM fault | Glue calls `W6X_Netif_free` once |
| `PASS`, pbuf wrapper unavailable | Adapter calls `W6X_Netif_free` once |
| `PASS`, lwIP accepts pbuf | Custom pbuf callback later calls `W6X_Netif_free` once |
| `PASS`, lwIP rejects pbuf | Glue calls `pbuf_free`; custom callback calls `W6X_Netif_free` once |
| Bad length, ISR execution, disabled adapter | Adapter calls `W6X_Netif_free` once |

The X-CUBE V1.3.0 reference `netif_rx_process` error path calls `W6X_Netif_free(buffer)` and then `netif_pbuf_free(pb)`, whose callback frees the same driver buffer again. The replacement glue intentionally avoids that double-release pattern.

## Cache and timing

`rtbpf_stm32h5_prepare_rx` executes after `W6X_Netif_input`. X-CUBE's SPI driver is responsible for completing DMA and its normal cache maintenance before placing a pointer in the NET_IF receive queue. Keep `RTBPF_STM32_RX_NEEDS_DCACHE_INVALIDATE=0` unless inspection and hardware testing show that the returned buffer remains cache-stale. If enabled, the port invalidates outward-rounded 32-byte cache lines.

DWT `CYCCNT` measures hook execution. Unsigned subtraction handles one 32-bit wrap. Read adapter counters through `rtbpf_st67_lwip_adapter()` from a management task; do not format logs in the RX path.

## Required hardware checks

This repository cannot complete these checks without the NUCLEO-H563ZI, X-NUCLEO-67W61M1, T02 firmware, and X-CUBE build environment:

1. Build against the exact X-CUBE package version.
2. Capture ARP/IPv4 frames before the hook and confirm Ethernet byte layout and lengths.
3. Confirm SPI DMA/cache completion before `W6X_Netif_input` returns.
4. Run constant PASS, constant DROP, malformed frame, queue-full, disconnect, and module-reset tests.
5. Instrument every W6 buffer and prove one terminal handoff/free transition.
6. Record no-hook, PASS, DROP, and maximum-program DWT cycle distributions under minimum-frame flood.
7. Size `RTBPF_ST67_PBUF_SLOTS` from measured simultaneous lwIP ownership; pool exhaustion must remain a clean release, not allocation.

Until these checks pass, Phase 3 is implemented and host-qualified but **hardware validation remains open**.
