# Simplified STM32 Wi-Fi DDoS Filter with rBPF and an XDP-like Hook

## 1. Decision

Build a small, statically attached, verified rBPF container that runs in the deferred Wi-Fi RX worker before lwIP. The preferred version-1 Wi-Fi driver presents each decrypted/de-aggregated network packet to STM32 as a contiguous Ethernet-like frame. The filter parses Ethernet II + fixed-header IPv4, detects UDP and TCP SYN-without-ACK traffic, applies a global limiter and a small Count-Min Sketch (CMS), and returns only `PASS` or `DROP`.

```text
Wi-Fi radio/module firmware
    -> decrypt, de-aggregate and validate radio frame
    -> SPI/SDIO host transfer
    -> STM32 Wi-Fi RX worker strips vendor transport header
    -> RTBPF NET_RX hook
    -> PASS to lwIP or DROP back to driver pool
```

This protects STM32 CPU, host packet pools, and lwIP from excess packets exported by the Wi-Fi device. It cannot stop RF airtime saturation, deauthentication/association attacks, Wi-Fi firmware exhaustion, bus saturation, or traffic discarded internally by the module.

### Selected reference target

Use this exact evaluation platform for the first implementation:

```text
Host MCU/board       NUCLEO-H563ZI (STM32H563ZI)
Wi-Fi board/module   X-NUCLEO-67W61M1 / ST67W611M1
Module firmware      mission T02 (LwIP-on-host), never T01/offload
Host software        X-CUBE-ST67W61 + FreeRTOS + lwIP on STM32
Host bus             SPI with SPI_RDY, DMA where supported
Hook location        NET_IF RX worker after SPI framing, immediately before lwIP input
VM link type         RTBPF_LINK_WIFI_ETHERNET
```

This target is preferred because ST supplies the STM32 board integration, FreeRTOS middleware, SPI transport, and an explicit LwIP-on-host architecture. In T02, network data travels directly between the SPI interface and host network interface instead of through the module's socket API. STM32H563 provides substantially more CPU/RAM margin than a small F4 for the interpreter, verifier, Wi-Fi middleware, maps, and packet pools.

Use the `ST67W6X_CLI_LWIP` application as the driver/network baseline. First prove that the buffer handed toward lwIP begins at a normalized Ethernet header, then insert the launchpad at that single call site. If the delivered data is raw IP rather than Ethernet-like for a particular package version, freeze `RTBPF_LINK_WIFI_RAW_IP` and use the raw-IP parser profile instead of guessing.

**Do not use the T01 mission firmware.** T01 runs lwIP inside ST67W611M1 and reduces STM32 visibility to socket-level data, which defeats SYN/IP-header filtering.

### Alternatives

If ST67W611M1/T02 is unavailable, the second choice is an Infineon CYW43439-based hosted module using the Infineon Wi-Fi Host Driver (WHD) over SDIO. WHD explicitly transfers received Ethernet data through `whd_network_process_ethernet_data()` with the buffer positioned at the Ethernet header, making that callback a natural launchpad. This alternative has a larger driver-porting task.

An ESP32-C6 running ESP-Hosted-MCU over SPI/SDIO is an open-source research alternative and can carry host network data, but its non-ESP-host integration is more moving parts than the ST reference path. Keep it as a second port, not the first acceptance target.

### Required Wi-Fi mode

The Wi-Fi device must operate as a **host network interface** and expose complete Ethernet-like or raw-IP packets before STM32 lwIP. Freeze the exact module firmware, bus protocol, packet framing, and receive-buffer ownership.

ESP8266 AT firmware does not satisfy this gate: it owns TCP/IP and exports `+IPD` socket payloads. If ESP8266 AT remains mandatory, use the payload VM in `RTOS_Packet_VM_Implementation.pdf` for per-connection/application limits; it cannot implement SYN detection, IP-fragment policy, or packet-level CMS semantics. Replacing AT firmware with custom ESP8266 firmware would move the packet hook into the ESP8266 and is a separate target, not an STM32 XDP hook.

## 2. Version-1 threat and feature scope

### Detect and limit

- IPv4 UDP packet floods.
- IPv4 TCP initial SYN floods (`SYN=1`, `ACK=0`).
- A high aggregate packet rate, even when source addresses are randomized.
- Repeated high-rate flow keys using an approximate fixed-size CMS.

### Preserve

- ARP and required non-IPv4 Ethernet-like control traffic exported by the Wi-Fi driver.
- Ordinary IPv4 TCP traffic after connection establishment.
- Traffic below configured global and estimated-flow thresholds.

### Explicitly exclude initially

- IPv6.
- VLAN tags.
- Raw 802.11 headers, monitor mode, A-MPDU/A-MSDU parsing, and radiotap/vendor metadata.
- IPv4 options (`IHL != 5`).
- IPv4 fragments.
- TCP connection tracking.
- LRU maps and exact per-source state.
- Packet mutation, `TX`, and redirect.
- General loops, tail calls, JIT, and Linux helper compatibility.
- Automatic permanent blacklisting.

The unsupported IPv4 cases have a policy bit: pass during bring-up or drop in hardened deployment. Truncated or structurally invalid IPv4 frames should be counted and dropped. Non-IPv4 frames are passed. The Wi-Fi adapter, not bytecode, must remove bus/vendor headers and normalize de-aggregated frames.

## 3. Filter algorithm

Use fixed one-second epochs supplied by a host helper. Do not store nanoseconds in every map bucket.

```text
validate context link type is ETHERNET_LIKE
parse normalized Ethernet II frame
if non-IPv4: PASS
parse fixed 20-byte IPv4 header
if invalid/options/fragment: apply configured policy
parse UDP or TCP
if TCP and not initial SYN: PASS

increment aggregate bucket for UDP or SYN
if aggregate count > aggregate threshold: DROP

construct key = {source IPv4, destination IPv4, destination port, protocol}
for each of two unrolled CMS rows:
    index = remix(hash(key) XOR row_salt) & (width - 1)
    reset bucket if epoch changed
    increment count with saturation
estimate = minimum row count
if estimate > flow threshold: DROP
PASS
```

A global limiter is required because attackers can randomize source addresses and spread traffic across CMS buckets. An unconditional source-IP whitelist is unsafe on networks where addresses can be spoofed. The fixed one-second window can permit a burst around an epoch boundary; this is accepted for version 1 and must be covered by the global threshold and RX-buffer capacity.

## 4. Fixed memory profile

Recommended initial maps:

```c
#define DDOS_CMS_DEPTH 2
#define DDOS_CMS_WIDTH 128

struct ddos_bucket {
    uint32_t epoch;
    uint32_t count;
};
```

| Object | Approximate RAM |
|---|---:|
| CMS: `2 * 128 * 8` | 2,048 bytes |
| Aggregate UDP/SYN buckets | 32–64 bytes |
| Configuration ARRAY | 64–128 bytes |
| Telemetry counters | 128–256 bytes |
| VM bytecode stack | 512 bytes |
| VM registers/runner metadata | approximately 200–400 bytes |
| **Initial total excluding bytecode/driver** | **approximately 3–4 KiB** |

The original airport CMS dimensions would consume approximately 64 KiB because 4096 buckets containing a 64-bit timestamp are typically 16 bytes each. The reduced representation is intentional.

Use saturating counters so sustained attack traffic cannot wrap a counter and become permitted. All map storage is allocated statically.

## 5. Container contract

The filter is a Femto-Container-style event module, not a FreeRTOS thread:

```text
container type       NET_RX
hook                 Wi-Fi RX worker pre-lwIP NET_RX
context ABI          RTBPF_NET_MD_V1
allowed actions      PASS, DROP
packet permission    read-only
stack                512 bytes
maps                 CONFIG, GLOBAL_RATE, CMS, STATS
helpers              map_lookup, epoch_1s
loops                 none; CMS rows unrolled
maximum instructions target <= 256, hard cap <= 512
maximum cycles       Wi-Fi worker/driver policy
```

The trusted launchpad is compiled into firmware. The container can access only its stack, read-only packet region, declared maps, and allowlisted helpers. It cannot invoke drivers, allocate, log text, or access peripherals.

Start with a compiled-in bytecode array. Dynamic loading, signatures, OTA replacement, and persistent bundles are later milestones.

## 6. Network context and actions

Add a packet profile alongside the ESP8266 payload profile in the implementation guide:

```c
typedef struct __attribute__((packed)) {
    uint32_t data;              /* checked packet pointer token */
    uint32_t data_end;          /* checked packet-end token */
    uint32_t ingress_port;
    uint32_t link_type;         /* RTBPF_LINK_WIFI_ETHERNET */
    uint32_t packet_length;
    uint32_t window_epoch;
    int32_t  rssi_dbm;          /* INT32_MIN if unavailable */
    uint32_t channel;           /* zero if unavailable */
    uint64_t timestamp_ns;
} rtbpf_net_md_v1_t;

typedef enum {
    RTBPF_NET_ABORTED = 0,
    RTBPF_NET_DROP    = 1,
    RTBPF_NET_PASS    = 2,
} rtbpf_net_action_t;
```

`window_epoch` can be populated once by the host before execution, avoiding a helper call and ensuring every map update in one invocation uses the same epoch. RSSI and channel are observability metadata only; version 1 must not use them as authentication or an unlimited whitelist.

Unknown action values, interpreter faults, and instruction-limit faults become `ABORTED`; the DDoS hook applies a configured fail-closed `DROP` policy.

## 7. Wi-Fi driver hook additions

`RTOS_Packet_VM_Implementation.pdf` currently defines the VM, verifier, maps, loader, and ESP8266 payload integration. A true Wi-Fi packet container additionally needs:

1. A separate `NET_RX` program type and Wi-Fi packet-context ABI.
2. A host-interface driver contract declaring SPI/SDIO transport, normalized packet framing, deferred-worker execution, contiguous RX, and cycle budget.
3. A hook after a complete host packet transfer and DMA/cache completion, after removal of the vendor transport header, but before ownership transfer to lwIP.
4. Explicit rejection of socket/AT-only drivers at `NET_RX` registration.
5. Exact `PASS`, `DROP`, and fault ownership transitions.
6. A static runner per concurrent Wi-Fi RX queue.
7. Per-interface immutable program pointer and safe detach/replacement.
8. Counters for malformed module messages, hidden/unsupported framing, nonlinear bypass, limit drops, CMS drops, VM faults, and cycle maximum.

The adapter must document one of these packet ABIs:

- `RTBPF_LINK_WIFI_ETHERNET`: preferred version 1; byte zero is an Ethernet destination MAC.
- `RTBPF_LINK_WIFI_RAW_IP`: later profile; byte zero is the IP version nibble and Ethernet parsing is skipped.
- raw 802.11: unsupported in version 1.

The driver integration must prove:

```text
PASS     -> exactly one ownership transfer to lwIP
DROP     -> exactly one return/recycle operation
ABORTED  -> exactly one return/recycle operation
no hook  -> direct PASS fast path
```

Do not execute the interpreter in a module-ready, SPI/SDIO, or DMA ISR. Those paths only notify the Wi-Fi RX worker. If the driver supplies chained buffers, version 1 either linearizes in existing driver code or bypasses/drops according to a frozen policy; it must never expose only the first segment. On module disconnect, reset, or reassociation, the adapter must release partial buffers and prevent stale program invocations.

## 8. VM and verifier additions

The existing core design needs these capabilities for this filter:

- fixed-width context loads producing packet-pointer and packet-end types;
- read-only packet loads of 1, 2, and 4 bytes;
- safe unaligned access implemented through byte loads or `memcpy`;
- explicit big-endian conversion;
- 32-bit multiply, XOR, AND, shifts, add, and compare;
- stack storage for the flow key;
- map relocations for ARRAY maps;
- nullable map lookup refinement;
- direct bounded map-value updates;
- acyclic branches and exit;
- maximum-path instruction and cycle accounting.

Keep `IHL == 5` in version 1 so the verifier only needs constant packet offsets. Variable IPv4 IHL can be added after pointer-range refinement has dedicated tests.

The verifier must ensure that every accepted packet access has a complete `data_end` proof. Runtime region checks remain enabled as a second boundary.

## 9. Wi-Fi-exported packet parser rules

Use private wire structures and offsets, not platform/Linux structs or TCP bitfields. The driver must strip and validate all module/bus framing before setting `ctx.data`; a vendor message header is never part of the VM packet region.

Minimum checks for the initial `WIFI_ETHERNET` profile:

1. Context link type is exactly `RTBPF_LINK_WIFI_ETHERNET`.
2. Normalized Ethernet-like frame contains 14 bytes.
3. EtherType is read in network order.
4. IPv4 frame contains the fixed 20-byte header.
5. Version is 4 and IHL is 5.
6. IPv4 total length is at least 20 and does not exceed available bytes.
7. Fragment flags/offset match the configured no-fragment policy.
8. UDP frame contains 8 bytes before reading destination port.
9. TCP frame contains 20 bytes before reading flags/destination port.
10. SYN and ACK are read from TCP flags byte using masks.
11. Every offset addition is overflow checked.

The packet parser does not process Wi-Fi encryption headers, 802.11 QoS fields, aggregation, or FCS. Those remain the responsibility of Wi-Fi firmware/driver normalization.

Create separate policy counters for unsupported versus malformed traffic. This prevents silent ambiguity during evaluation.

## 10. Configuration and control plane

Use a single-entry CONFIG ARRAY populated by trusted firmware or the loader:

```c
struct ddos_config {
    uint32_t enabled;
    uint32_t udp_global_pps;
    uint32_t syn_global_pps;
    uint32_t udp_flow_pps;
    uint32_t syn_flow_pps;
    uint32_t unsupported_ipv4_action;
    uint32_t malformed_ipv4_action;
    uint32_t reserved;
};
```

The fast path must not parse configuration text or receive arbitrary threshold updates directly from the network. Validate ranges in the management task before atomically publishing a complete configuration snapshot.

Thresholds are device-specific. The `400 PPS` value in `airport-ebpf-mac` is not automatically valid for STM32. Establish thresholds from baseline camera/application traffic and target saturation measurements.

## 11. Observability additions

Use fixed per-port counters:

- total Wi-Fi-exported frames and bytes;
- module/bus receive errors, resets, and nonlinear bypasses;
- IPv4 UDP and TCP SYN candidates;
- global-limit drops;
- CMS-estimate drops;
- malformed drops;
- unsupported-policy drops/passes;
- map lookup/update failures;
- VM faults and instruction-limit faults;
- maximum and sampled average cycles.

Do not emit one event per dropped packet. Optionally emit a compact summary event at a bounded interval from a management task.

## 12. Required filter-specific tests

In addition to the VM tests in the implementation guide, add:

- malformed vendor transport headers/lengths before the VM and proof that none is visible in `ctx.data`;
- truncation at every offset from 0 through normalized Ethernet, IPv4, UDP, and TCP headers;
- non-IPv4 and ARP pass behavior;
- bad IPv4 version, IHL, total length, and fragments;
- all TCP SYN/ACK flag combinations;
- endian tests for EtherType, ports, and addresses;
- epoch transition and `uint32_t` epoch wrap;
- counter saturation;
- CMS collisions and independent row salts;
- randomized/spoofed sources exercising the aggregate limiter;
- map exhaustion impossible for ARRAY-only version 1;
- no-filter, native-C oracle, and rBPF differential results;
- sustained flood with maximum-size verified program, Wi-Fi bus DMA, and concurrent RTOS interrupts;
- module disconnect, reset, reassociation, bus timeout, and queue-full recovery;
- exactly-once Wi-Fi RX-buffer ownership for every action and VM fault;
- proof that RF/module saturation is reported as outside the filter's protection boundary.

Use packet captures plus generated packets. The rBPF result must match an independent native-C oracle for every test frame.

## 13. Delivery path

### Milestone A — Wi-Fi platform and native oracle (7–12 days)

- Bring up NUCLEO-H563ZI + X-NUCLEO-67W61M1 with the T02 LwIP-on-host firmware and the `ST67W6X_CLI_LWIP` baseline.
- Freeze ST67 SPI framing, NET_IF packet start/length, de-aggregation behavior, bus/DMA/cache sequence, buffer ownership, and the exact call before lwIP input.
- Implement native-C parser and reduced CMS on a host.
- Establish packet corpus and expected actions.

### Milestone B — static rBPF execution (12–18 days)

- Complete required interpreter opcodes and runtime packet regions.
- Add compiled-in container and ARRAY maps.
- Execute against host packet corpus and STM32 synthetic frames.

### Milestone C — verifier and cost policy (12–18 days)

- Implement structural/type/packet/map verification.
- Add instruction and cycle budgets.
- Fuzz verifier/interpreter and require differential agreement.

### Milestone D — Wi-Fi driver integration (9–15 days)

- Add the launchpad in the deferred Wi-Fi RX worker before lwIP.
- Validate vendor-header removal, complete-frame delivery, `PASS`, `DROP`, faults, bus DMA/cache behavior, disconnect/reset recovery, and ownership.
- Measure no-hook and hook overhead plus bus/ring headroom.

### Milestone E — DDoS evaluation and tuning (7–12 days)

- Run UDP, SYN, randomized-source, malformed, and collision workloads.
- Tune dimensions and thresholds.
- Record RAM, flash, cycles, latency, throughput, loss, and false positives.

### Milestone F — deployable containers (optional, 10–15 days)

- Add host ELF packer, compact bundle, manifest, transactional loader, signatures, atomic replacement, and rollback policy.

Expected effort from the current documentation-only state:

```text
compiled-in demonstrator       26–40 engineer-days
verified hardware prototype    45–70 engineer-days
production deployable version  60–90 engineer-days
```

Wi-Fi host-interface driver bring-up is additional if a working frame-exposing driver does not already exist. Budget an additional 10–30 engineer-days depending on module protocol quality, DMA/cache requirements, and existing STM32 support. ESP8266 AT integration does not count because it cannot expose the required packets.

## 14. Recommended immediate implementation order

1. Stop at the platform gate if ESP8266 AT is the only Wi-Fi path.
2. Obtain NUCLEO-H563ZI and X-NUCLEO-67W61M1; flash the ST67W611M1 T02 LwIP-on-host mission image.
3. Run `ST67W6X_CLI_LWIP`, capture the exact NET_IF-to-lwIP frame, and prove where SPI/vendor headers and aggregation are removed.
4. Implement the native-C oracle with the 2x128 CMS.
5. Add `RTBPF_LINK_WIFI_ETHERNET` context/program type without changing the ESP payload ABI.
6. Implement constant-offset normalized-Ethernet/IPv4/UDP/TCP verifier rules.
7. Run a compiled-in filter in the host harness.
8. Integrate `PASS` only, then `DROP`, in the deferred Wi-Fi RX worker.
9. Add CMS maps and thresholds.
10. Complete parser/driver fuzzing, disconnect/reset tests, and target timing.
11. Add dynamic Femto-Container deployment only after the static filter is safe.

## 15. Target-selection references

- ST `X-CUBE-ST67W61` architecture and T02 LwIP-on-host data path: <https://wiki.st.com/stm32mcu/wiki/Connectivity:X-CUBE-ST67W61_Architecture>
- ST67W611M1 hardware and T01/T02 mission images: <https://wiki.st.com/stm32mcu/wiki/Connectivity:Wi-Fi_MCU_Hardware_Setup>
- Infineon WHD Ethernet receive callback and porting interfaces: <https://infineon.github.io/wifi-host-driver/html/index.html>
- ESP-Hosted-MCU alternative: <https://github.com/espressif/esp-hosted-mcu>
