# Dynamically Programmable Packet-Filtering Framework for STM32/FreeRTOS

## 1. Purpose

This document specifies a reusable framework for loading, verifying, attaching, executing, replacing, and observing restricted packet-filter programs on STM32/FreeRTOS systems.

The framework is named **RTBPF-NET** in this document. It applies the rBPF/Femto-Container event-module model to packets delivered by a host network interface. It is not tied to one filtering algorithm. The reduced UDP/SYN Count-Min Sketch design in [STM32_SIMPLIFIED_DDOS_FILTER_PLAN.md](STM32_SIMPLIFIED_DDOS_FILTER_PLAN.md) is the first application built on the framework, not part of the trusted framework core.

The intended authoring and deployment flow is:

```text
restricted packet-filter C
    -> clang eBPF object on development host
    -> rtbpf-pack: compatibility checks, relocations, verification, manifest
    -> optional production signature
    -> compact .rtbpf bundle
    -> STM32 management task stages and verifies bundle
    -> atomic attachment to a packet hook
    -> bounded execution once per packet
```

The design prioritizes:

- deterministic packet-path latency;
- memory safety even for malformed or hostile packets;
- static or bounded memory use;
- dynamic replacement without rebooting the STM32;
- exact network-buffer ownership;
- a small, versioned ABI rather than Linux kernel compatibility;
- portability across Wi-Fi and Ethernet drivers that expose packets before lwIP.

## 2. Framework and application boundary

The framework provides the mechanism. A packet-filter application provides policy.

| Framework responsibility | Program/application responsibility |
|---|---|
| Hook registration and packet normalization | Parse the declared link/network format |
| VM interpreter and runtime watchdog | Return an allowed packet action |
| Verifier and resource admission | Stay within the restricted C/ISA profile |
| Map implementation and isolation | Define application map schemas and algorithms |
| Bundle parser, signature checks, loader | Declare required maps, helpers, ABI, and budget |
| Atomic attach, replacement, detach, rollback | Initialize policy defaults and configuration |
| Packet-buffer ownership and fault policy | Count/classify/filter packets |
| Fixed observability counters | Define application-specific telemetry maps |

The DDoS application uses one `NET_RX` program, ARRAY maps, an aggregate rate limiter, and a 2x128 CMS. Another program could implement a port allowlist, protocol accounting, malformed-packet policy, device isolation, DNS telemetry, or application-specific UDP admission without modifying the VM.

The trusted firmware must never contain algorithm-specific branches such as “if SYN flood.” It should know only program types, context fields, actions, helpers, maps, budgets, and driver ownership rules.

## 3. Scope

### 3.1 Version-1 capabilities

- One dynamically replaceable program per network interface `NET_RX` hook.
- Ethernet-like and raw-IPv4/raw-IP link profiles.
- Read-only packet access.
- `PASS`, `DROP`, and internal `ABORTED` results.
- Portable C interpreter with an eBPF-shaped instruction set.
- Loop-free verified programs with bounded forward branches.
- Fixed-size ARRAY and read-only CONFIG maps initially.
- Optional fixed-capacity HASH maps after bounded probing is implemented.
- A compact bundle loaded from a management task.
- Host and target verification.
- Signed production bundles, anti-rollback policy, atomic replacement, and recovery to a known-good program.
- Fixed, nonblocking telemetry.

### 3.2 Deferred capabilities

- A `NET_TX` hook using a separate context and action policy.
- Multiple programs in a bounded chain.
- Verifier-proven bounded loops.
- Packet mutation, head/tail adjustment, checksum replacement, `TX`, or redirect.
- Tail calls and program-to-program calls.
- JIT compilation.
- LRU maps or dynamically resizing maps.
- IPv6-specific convenience helpers; programs can parse bytes only after the corresponding profile is verified and tested.

### 3.3 Non-goals

- Loading arbitrary Linux eBPF ELF files directly on the STM32.
- Supporting all Linux helpers, map types, BTF, CO-RE, pinning, XDP metadata, or libbpf APIs.
- Executing bytecode in a hard ISR.
- Allocating, blocking, logging formatted text, writing flash, or invoking a network control operation from the packet path.
- Exposing native driver pointers, RTOS objects, peripheral registers, or unrestricted memory.
- Filtering traffic not exported by the Wi-Fi/network device. The framework cannot prevent RF airtime exhaustion or resource exhaustion inside a network coprocessor.

## 4. Reference platform and portability

The first integration target remains:

```text
Host                  NUCLEO-H563ZI / STM32H563ZI
RTOS                   FreeRTOS
Network stack          lwIP on the STM32
Wi-Fi                  X-NUCLEO-67W61M1 / ST67W611M1
Wi-Fi firmware         T02 LwIP-on-host mission firmware
Host interface         SPI with SPI_RDY
Initial hook           deferred NET_IF receive worker, before lwIP input
Initial link profile   RTBPF_LINK_WIFI_ETHERNET
```

The framework must not depend on ST67-specific framing. A small driver adapter converts each supported interface into the stable RTBPF-NET hook contract. Ports may later support Infineon WHD, ESP-Hosted-MCU, STM32 native Ethernet, or another interface if they can provide a complete bounded packet before the network stack.

A network adapter is eligible only when it can document:

1. where transport/vendor headers are removed;
2. whether the packet starts at Ethernet or IP;
3. whether aggregation has been removed;
4. packet contiguity or bounded segment access;
5. DMA and cache-completion ordering;
6. execution context and concurrency;
7. exact ownership transitions for pass, drop, fault, disconnect, and reset.

Socket-offload/AT interfaces are not eligible for `NET_RX`; they require a separate application-payload program type.

## 5. Architecture

```text
                         DEVELOPMENT HOST
  filter.c + SDK headers
          |
          v
  clang -target bpf -> ELF -> rtbpf-pack/shared verifier
          |                         |
          +------ compact bundle ---+-- signature
                                     |
======================= deployment boundary =======================
                                     |
                         STM32 management task
                  authenticate -> parse -> admit -> verify
                                     |
                         inactive program/map bank
                                     |
                              atomic publish
                                     |
  Wi-Fi/Ethernet RX worker           v
  complete normalized packet -> NET_RX launchpad -> interpreter
                                      | PASS       | DROP/ABORTED
                                      v            v
                                  lwIP input    driver recycle
```

### 5.1 Trusted computing base

The trusted computing base consists of:

- bundle decoder and cryptographic policy;
- verifier and resource-admission logic;
- interpreter and runtime region checks;
- map implementations and helper dispatcher;
- hook registry and replacement synchronization;
- driver adapter and packet ownership logic;
- RTOS/STM32 portability layer.

Filter bytecode, initial map data, runtime policy values, and received packets are untrusted.

### 5.2 Components

```text
rtbpf_core       instruction decoder, interpreter, verifier, runner
rtbpf_maps       bounded map implementations and registry
rtbpf_loader     bundle decoder, relocations, admission, transactions
rtbpf_security   hashes, signatures, key IDs, anti-rollback policy
rtbpf_hooks      hook registry, immutable attachments, grace periods
rtbpf_net        NET_RX ABI, actions, packet regions, base counters
rtbpf_port       RTOS locking, cycle clock, memory arenas, cache hooks
net_adapter_*    driver-specific normalization and buffer lifecycle
tools            compiler SDK, ELF packer, host verifier, signer, inspector
applications     independently versioned packet-filter sources and bundles
```

## 6. Stable NET_RX contract

### 6.1 Program identity

A program declares:

```text
program_type       RTBPF_PROG_NET_RX
context_abi        RTBPF_NET_MD_V1
required_link      one exact link profile or an allowed profile set
packet_access      read-only
allowed_actions    PASS | DROP
maximum_path       verifier-generated bound
helper_set         explicit IDs and ABI versions
map_set            explicit descriptors
```

The loader rejects attachment if the program type, context ABI, required link profile, helper versions, or resource limits do not match the hook.

### 6.2 VM-visible context ABI

Version 1 retains the context required by the DDoS application so that application can become an ordinary framework bundle:

```c
typedef struct __attribute__((packed)) {
    uint32_t data;              /* checked packet-pointer token */
    uint32_t data_end;          /* checked packet-end token */
    uint32_t ingress_port;      /* stable logical interface ID */
    uint32_t link_type;         /* one RTBPF_LINK_* value */
    uint32_t packet_length;     /* normalized bytes in packet region */
    uint32_t window_epoch;      /* host snapshot; normally one-second epoch */
    int32_t  rssi_dbm;          /* INT32_MIN when unavailable */
    uint32_t channel;           /* zero when unavailable */
    uint64_t timestamp_ns;      /* monotonic hook timestamp */
} rtbpf_net_md_v1_t;
```

Serialized offsets and widths are ABI constants and require compile-time assertions in the SDK and firmware. `data` and `data_end` are capability tokens interpreted by the verifier/runtime; bytecode cannot construct a native address from an integer. All other fields are scalars.

RSSI and channel are optional observations, not authentication evidence. A program must tolerate their unavailable values. New metadata is introduced through `RTBPF_NET_MD_V2`, never by silently changing V1.

### 6.3 Link profiles

```c
typedef enum {
    RTBPF_LINK_WIFI_ETHERNET = 1, /* byte 0 is Ethernet destination MAC */
    RTBPF_LINK_ETHERNET      = 2,
    RTBPF_LINK_RAW_IP        = 3, /* byte 0 contains the IP version nibble */
} rtbpf_link_type_t;
```

Raw 802.11, radiotap, module transport headers, and partial aggregate fragments are not V1 profiles. A port must not label an uncertain format as Ethernet. Link-profile behavior includes the malformed-frame and nonlinear-buffer policy enforced by the adapter before VM entry.

### 6.4 Actions

```c
typedef enum {
    RTBPF_NET_ABORTED = 0,
    RTBPF_NET_DROP    = 1,
    RTBPF_NET_PASS    = 2,
} rtbpf_net_action_t;
```

`ABORTED` is produced for interpreter faults, watchdog expiration, and unknown program returns. The program manifest cannot request it as a normal policy action. The hook has a trusted, configurable fault policy; the security-filter profile defaults to recycling the packet as a drop.

The no-program fast path is a direct `PASS`. Production firmware may instead attach a compiled-in recovery program according to deployment policy.

## 7. Driver adapter and ownership

The adapter calls the framework only from a deferred task after the full transfer and required cache maintenance. No SPI-ready, DMA, Ethernet, or Wi-Fi ISR executes bytecode.

Conceptual API:

```c
typedef struct {
    const uint8_t *data;
    uint32_t length;
    uint32_t ingress_port;
    uint32_t link_type;
    int32_t rssi_dbm;
    uint32_t channel;
    void *private_buffer_handle; /* trusted adapter only */
} rtbpf_net_packet_t;

rtbpf_net_action_t rtbpf_net_rx_run(const rtbpf_net_packet_t *packet);
```

`private_buffer_handle` is never copied into the VM context. The adapter remains the sole owner while the VM runs.

Required ownership table:

| Result | Adapter operation | Final owner |
|---|---|---|
| No program | Invoke normal lwIP handoff once | lwIP/driver contract |
| `PASS` | Invoke normal lwIP handoff once | lwIP/driver contract |
| `DROP` | Invoke normal release/requeue once | Driver pool |
| `ABORTED` | Apply trusted fault policy, normally release once | Driver pool |
| Adapter validation failure | Never invoke VM; release once | Driver pool |

A hook must not retain a packet pointer in a map or after return. V1 rejects packet stores and nonlinear/chained packets unless the adapter already linearizes them in bounded existing storage. Copying solely for the VM is permitted only if included in the interface's memory and latency budget.

Disconnect, module reset, queue flush, and reassociation must stop new invocations, wait for the current invocation if necessary, and reclaim each partial or complete buffer exactly once.

## 8. Programming model

### 8.1 Restricted C source

Program authors use a project SDK rather than Linux headers:

```c
#include <rtbpf/net.h>
#include <rtbpf/maps.h>
#include <rtbpf/helpers.h>

RTBPF_ARRAY(config, struct filter_config, 1);
RTBPF_ARRAY(stats, struct filter_stats, 1);

SEC("rtbpf/net_rx")
int filter(struct rtbpf_net_md_v1 *ctx)
{
    const uint8_t *p = rtbpf_packet_data(ctx);
    const uint8_t *end = rtbpf_packet_end(ctx);

    if (p + 14 > end)
        return RTBPF_NET_DROP;

    /* Application-specific parsing and policy. */
    return RTBPF_NET_PASS;
}
```

The host tool rejects unsupported sections, globals, relocations, instructions, map types, helper calls, and context accesses. Passing host checks does not bypass target verification.

### 8.2 Initial ISA profile

The portable interpreter supports only the operations required by practical bounded filters:

- 32/64-bit moves and ALU operations;
- signed and unsigned comparisons;
- forward conditional/unconditional branches;
- stack, context, packet, and map-value loads;
- stack and map-value stores;
- explicit endian conversion;
- allowlisted helper calls;
- exit.

Packet writes, indirect calls, floating point, atomics, backward jumps, and arbitrary native addresses are rejected initially. CMS iterations and short fixed parsers are unrolled by the compiler/source.

### 8.3 Execution state

- Eleven 64-bit eBPF-style registers.
- `r0` is the return action.
- `r1` starts as the context capability.
- `r10` is the read-only frame pointer.
- One 512-byte VM stack per concurrent runner.
- A verified maximum-path bound plus a runtime instruction watchdog.
- Runtime pointer tags or checked memory-region lookup before every memory operation.

Runner and VM stack memory are statically allocated per RX execution context; they are not placed on the FreeRTOS task stack and are never allocated per packet.

## 9. Verifier and safety model

Verification is mandatory for every executable image, including a correctly signed image. A signature identifies an authorized publisher; it does not prove memory safety.

### 9.1 Structural verification

Reject:

- invalid opcodes, registers, or malformed wide instructions;
- out-of-range jumps or jumps into continuation slots;
- backward/self jumps in V1;
- unreachable malformed sections according to canonical bundle policy;
- paths that do not terminate in `EXIT`;
- instruction, block, or state counts above policy.

### 9.2 Abstract interpretation

Track registers and spilled stack values as:

```text
UNINITIALIZED
SCALAR with signed/unsigned range
CONTEXT_POINTER
PACKET_POINTER with object identity, offsets, and proven accessible range
PACKET_END
STACK_POINTER with bounded offset
MAP_POINTER
MAP_VALUE_OR_NULL
MAP_VALUE_POINTER with map identity and bounds
```

Require:

- initialized registers and stack bytes before reads;
- an explicit `data_end` proof before every packet access;
- exact allowlisted context offsets and widths;
- null refinement after map lookup;
- map-value accesses within the declared value size;
- helper argument and return types matching the helper descriptor;
- understood pointer arithmetic that preserves provenance;
- read-only packet permissions.

The interpreter retains runtime region checks as defense in depth. It never directly dereferences a scalar supplied by bytecode.

### 9.3 Cost verification

For an acyclic program, calculate the longest instruction-cost path and add each helper's declared worst-case cost. Admission requires all of:

```text
instruction_count <= platform maximum
maximum_path_instructions <= hook maximum
verified_cycle_estimate <= hook policy
stack_bytes <= runner stack
map bytes <= program and system quotas
concurrent runner bytes <= interface reservation
```

Cycle estimates are conservative admission inputs, not a substitute for target measurements. DWT cycle samples establish the real maximum and alert when a program approaches its budget.

## 10. Maps and application state

### 10.1 Map descriptors

Each map declares:

```text
map type
map ABI version
key size and value size
maximum entries
access flags
initialization policy
persistence policy
memory bytes
```

V1 types:

- `ARRAY`: fixed index range and constant-time lookup;
- `CONFIG_ARRAY`: ARRAY writable by the management plane and read-mostly by the program;
- `PER_RUNNER_ARRAY`: independent values where multiple RX queues require lock-free counters;
- fixed-capacity `HASH`: later, only with a strict maximum probe count and no resizing.

The packet path never allocates. Counters should saturate where wrap could weaken a policy. Shared writable values use single-runner ownership, target-supported atomics, or short bounded critical sections explicitly declared by the map implementation.

### 10.2 Map isolation

A bundle references maps through relocation indexes. At load time these become capabilities scoped to that program or to an explicitly named trusted shared map. A program cannot enumerate or address another program's maps.

Map pointers are valid only for the current invocation and cannot be converted to scalars and reconstructed. Helpers validate key/value sizes from the resolved descriptor.

### 10.3 Replacement and persistence

Every map selects one policy:

- `FRESH`: allocate and initialize new state for each program generation;
- `PRESERVE_COMPATIBLE`: preserve only when type, sizes, entry count, schema ID, and owner match exactly;
- `SYSTEM_CONFIG`: framework-owned map updated through a trusted management API;
- `READ_ONLY_INIT`: immutable bundle data after attachment.

Default to `FRESH`. State migration never runs inside the packet hook. If migration is required, a management-task converter builds the inactive map bank and either commits the whole generation or discards it.

The DDoS application's thresholds belong in a `SYSTEM_CONFIG` or management-writable CONFIG_ARRAY; its current epoch counters and CMS normally use `FRESH`.

## 11. Helper API

Keep the helper surface small. Each helper descriptor contains ID, ABI version, permitted program types, typed arguments/result, context restrictions, side effects, and worst-case cost.

Initial helpers:

```text
map_lookup(map, key) -> nullable map value
map_update(map, key, value, flags) -> status   [if direct value stores are insufficient]
map_delete(map, key) -> status                 [HASH only]
get_epoch_1s() -> u32                          [optional; context epoch preferred]
get_prandom_u32() -> u32                       [optional, not security randomness]
```

Generic packet parsing is preferably implemented as checked byte access in the program, not as a large privileged parser helper. Helpers must not allocate, block, sleep, write flash, transmit packets, call lwIP, invoke driver control, issue AT commands, or expose native pointers. A debug trace helper, if present, is development-only and rate-limited into a fixed ring.

## 12. Compact bundle format

The STM32 does not parse general ELF. `rtbpf-pack` emits a canonical little-endian format with explicit lengths and checked offsets:

```text
Bundle header
  magic and format version
  total length and header length
  target architecture/profile
  bundle UUID and generation/version
  minimum framework version
  integrity algorithm and digest

Program table
  program name/ID and type
  context ABI and required link profile
  instruction offset/count
  declared stack and action mask
  verifier-generated instruction/cycle bounds

Map table
  type and ABI version
  key/value/max-entry dimensions
  flags, schema ID, persistence policy
  optional initial-data offset/length

Relocation table
  instruction index
  relocation kind
  map or helper index

Manifest
  aggregate RAM/flash requirements
  helper ABI requirements
  hook compatibility and fault-policy request
  optional human-readable build metadata outside the trusted decisions

Authentication trailer
  key ID, signature algorithm, anti-rollback generation, signature
```

All size arithmetic uses checked addition/multiplication. Sections may not overlap, reference bytes outside the bundle, or contain duplicate identities where uniqueness is required. Unknown required features reject the bundle; optional metadata can be skipped by length.

The digest covers the canonical executable content, map definitions/initial values, manifest, and generation. Production signatures cover the digest plus security-relevant header fields.

## 13. Dynamic loading and atomic replacement

### 13.1 Loader API

Conceptual management-plane API:

```c
int rtbpf_bundle_stage(const void *bytes, size_t length,
                       rtbpf_stage_id_t *stage);
int rtbpf_bundle_verify(rtbpf_stage_id_t stage,
                        rtbpf_verify_report_t *report);
int rtbpf_net_attach(uint32_t ingress_port,
                     rtbpf_stage_id_t stage,
                     uint32_t program_id);
int rtbpf_net_detach(uint32_t ingress_port);
int rtbpf_bundle_remove(rtbpf_bundle_id_t bundle);
```

These calls run only in a management task. The packet path sees one immutable attachment pointer and does not take a loader mutex.

### 13.2 Transaction

1. Receive a length-bounded bundle into inactive storage.
2. Validate framing, digest, signature, signer permissions, and anti-rollback generation.
3. Decode with checked arithmetic.
4. Check target, framework, context, link, helper, map, and action compatibility.
5. Reserve the complete inactive program/map/runner resource budget.
6. Resolve relocations into loader-owned immutable tables.
7. Run target structural and semantic verification.
8. Initialize maps in the inactive bank.
9. Persist the verified bundle and pending metadata if reboot survival is required.
10. Atomically publish one immutable attachment pointer.
11. Wait for a grace period after all old invocations exit.
12. Reclaim or retain the old generation according to rollback policy.

Any error before publication releases all inactive resources and leaves the active program unchanged.

### 13.3 Concurrency and grace periods

The initial STM32H563 target is single-core and invokes `NET_RX` from one worker. Publish the pointer in a short RTOS/CMSIS critical section and use an active-run counter or worker quiescence notification before reclaiming the old generation. Do not disable interrupts for verification, map initialization, flash writes, or grace-period waiting.

A future multi-queue/SMP port requires one runner per concurrent context and an RTOS-supported epoch/RCU mechanism or per-generation active counters.

### 13.4 Power-loss recovery

Use dual persistent bundle slots or a journaled metadata record:

```text
EMPTY -> WRITING -> VERIFIED -> ACTIVE -> RETIRED
```

Only a final atomic metadata update marks a verified slot active. At boot, ignore `WRITING` slots, validate the active slot again, and fall back to the previous known-good or compiled-in recovery program on failure. Map state need not survive reboot unless explicitly designed as persistent; executable authenticity must survive reboot.

## 14. Deployment security

### 14.1 Threats

Assume:

- attackers control packet bytes and packet rates;
- an untrusted or buggy filter may try to read/write arbitrary memory or run forever;
- management transports can deliver truncated, reordered, duplicated, or malicious bundles;
- a validly signed program can still contain a compiler or author bug;
- power can fail during update;
- rollback to an old vulnerable filter may be attempted.

### 14.2 Production policy

- Accept only approved signature algorithms and key IDs.
- Bind signer permissions to program type, interface set, map/memory limits, and action capability where needed.
- Enforce a monotonic generation or approved rollback token.
- Keep production public keys or key hashes in protected flash/OTP; use STM32 security features where available.
- Disable unsigned loading unless a physical development mode is asserted.
- Verify every bundle again on boot/attachment.
- Rate-limit deployment attempts and audit compact result records.
- Authenticate and authorize the transport carrying bundles; bundle signatures remain required even over TLS.

A lost management connection cannot leave a partially attached program. Packet processing continues with the prior generation throughout staging and verification.

## 15. Resource model

Example initial platform limits, to be finalized by measurement:

```c
#define RTBPF_MAX_PROGRAM_INSNS       512u
#define RTBPF_MAX_PATH_INSNS          512u
#define RTBPF_VM_STACK_BYTES          512u
#define RTBPF_MAX_MAPS_PER_PROGRAM      8u
#define RTBPF_MAX_MAP_BYTES         16384u
#define RTBPF_MAX_BUNDLE_BYTES      65536u
#define RTBPF_MAX_VERIFIER_STATES    4096u
#define RTBPF_MAX_RX_INTERFACES         4u
#define RTBPF_MAX_CONCURRENT_RUNNERS    1u
```

These are policy values, not ABI constants. Each board profile reserves:

- active and inactive program descriptor/instruction storage;
- active and inactive map arenas for transactional replacement;
- one VM state and stack per concurrent runner;
- verifier workspace used only by the management task;
- bundle staging/persistent slots;
- fixed telemetry and audit records.

The loader reports the exact requested and admitted bytes before commit. It must never overcommit based on expected map occupancy.

## 16. Determinism and overload policy

The filter executes after the module/bus transfer, so it can protect lwIP and application resources but cannot recover bus bandwidth already consumed. The hook budget must be derived from:

- maximum expected packet rate and minimum frame size;
- Wi-Fi/SPI or Ethernet delivery behavior;
- RX queue depth and time until exhaustion;
- worker priority and competing interrupt/task latency;
- baseline driver/lwIP costs;
- worst accepted program and helper costs.

Required controls:

- admission-time longest-path bound;
- runtime instruction watchdog;
- optional cycle watchdog where timer-read cost is acceptable;
- no per-packet allocation or blocking;
- no per-packet text logging;
- bounded map probes and critical sections;
- fault and repeated-abort counters;
- optional trusted automatic detach/recovery threshold.

Automatic detach policy is deployment-specific. Detaching a security program may fail open; retaining a repeatedly faulting program may fail closed and deny service. The production profile must choose explicitly and expose the state through telemetry.

## 17. Observability and management

Framework counters are fixed per interface/program generation:

- packets and bytes entering the hook;
- `PASS`, `DROP`, and `ABORTED` results;
- no-program fast-path packets;
- invalid program returns;
- instruction-limit, bad-access, bad-helper, and internal faults;
- maximum and sampled average cycles;
- map lookup/update failures;
- driver normalization failures and nonlinear bypass/drop;
- attach, detach, replacement, rollback, and signature failures.

Application telemetry remains in declared maps. The management task takes consistent snapshots without blocking the RX worker. It may exchange/reset inactive counter banks or use short target-appropriate critical sections.

Do not emit one event per packet or drop. A fixed diagnostic ring may carry rate-limited summary records containing program ID, generation, interface, reason code, count, and timestamp. Packet bytes are excluded by default.

Suggested read-only inspection commands/API operations:

```text
framework version and enabled features
hook/interface status and link profile
active bundle/program ID, hash, generation, signer
resource usage and verified bounds
framework counters and application map snapshots
last deployment/rollback result
```

## 18. DDoS filter as the first framework application

The existing simplified DDoS plan maps into RTBPF-NET as follows:

| DDoS design element | Generic framework object |
|---|---|
| Wi-Fi pre-lwIP callback | `NET_RX` launchpad and ST67 adapter |
| Ethernet/IP/UDP/TCP parser | Untrusted application bytecode |
| UDP/SYN decisions | Application policy |
| 2x128 CMS | Application ARRAY map |
| Aggregate UDP/SYN buckets | Application ARRAY map |
| Thresholds and malformed policy | CONFIG_ARRAY managed by trusted control plane |
| Statistics | Application telemetry ARRAY plus framework counters |
| Epoch | `window_epoch` context field |
| `PASS`/`DROP` | Generic NET_RX actions |
| VM fault handling | Trusted hook fault policy |
| Static compiled bytecode milestone | Framework bootstrap mode |
| Later OTA bundle | Normal dynamic deployment flow |

A representative DDoS manifest is:

```text
program_type        NET_RX
context_abi         RTBPF_NET_MD_V1
required_link       RTBPF_LINK_WIFI_ETHERNET
packet_access       READ_ONLY
allowed_actions     PASS | DROP
stack_bytes         512
maps                 CONFIG, GLOBAL_RATE, CMS, STATS
helpers              map_lookup (epoch read from context)
maximum_insns        <= 512, exact value generated by verifier
state_policy         fresh CMS/rate state; preserve compatible config
```

The application document continues to define its parser, algorithm, thresholds, packet corpus, CMS dimensions, and DDoS-specific evaluation. This framework document defines how that application is safely and dynamically hosted.

## 19. Suggested repository layout

```text
include/rtbpf/abi.h                 instruction and bundle ABI
include/rtbpf/api.h                 loader/registry public API
include/rtbpf/net.h                 NET_RX contexts, links, actions
include/rtbpf/maps.h                map descriptors and flags
include/rtbpf/helpers.h             helper IDs and SDK declarations

kernel/rtbpf/interpreter.c
kernel/rtbpf/verifier.c
kernel/rtbpf/loader.c
kernel/rtbpf/registry.c
kernel/rtbpf/maps_array.c
kernel/rtbpf/maps_hash.c             later
kernel/rtbpf/security.c
kernel/rtbpf/net_hook.c

ports/stm32/rtbpf_port.c
ports/stm32/rtbpf_crypto.c
ports/freertos/rtbpf_sync.c
adapters/st67w61/rtbpf_st67_rx.c
adapters/whd/rtbpf_whd_rx.c          optional second port

sdk/include/rtbpf_net_sdk.h
sdk/examples/drop_udp_port.c
sdk/examples/count_ethertypes.c
applications/ddos_cms/

tools/rtbpf-pack/
tools/rtbpf-sign/
tools/rtbpf-inspect/
tests/unit/
tests/fuzz/
tests/packet_corpus/
tests/hardware/
```

Keep application headers separate from trusted internal headers. The SDK must not expose native RTOS, driver, or map implementation structures.

## 20. Validation strategy

### 20.1 Interpreter and verifier

- Unit-test every accepted opcode and edge value.
- Reject bad registers, jumps, wide instructions, helpers, and exits.
- Test uninitialized stack/register use and partial pointer spills.
- Test bounds proofs at every packet length and offset.
- Test scalar-to-pointer and cross-map provenance attacks.
- Differentially execute accepted random programs in a host reference interpreter and target interpreter.
- Fuzz the verifier and require that every accepted program terminates within its bound.

### 20.2 Loader and deployment

- Fuzz every bundle length, offset, count, and relocation.
- Exercise arithmetic overflow, overlapping sections, duplicate IDs, and excessive resources.
- Test invalid hashes, signatures, key IDs, generations, and target profiles.
- Remove power at every persistent-slot state transition.
- Fail every allocation/reservation step and prove the active generation is unchanged.
- Replace/detach while packets are arriving and verify no use-after-free.

### 20.3 Network adapters

- Truncate frames at every relevant byte.
- Test minimum/maximum frames, malformed transport lengths, and chained buffers.
- Test DMA/cache boundaries and unaligned packet starts.
- Exercise queue full, SPI timeout, disconnect, reset, and reassociation.
- Instrument every buffer and prove exactly one final ownership transition.
- Compare no-hook, constant-pass, native-C, and maximum verified bytecode paths.

### 20.4 Application conformance

For each bundle, provide:

- valid and malformed packet corpus;
- expected action for every packet;
- map state before and after execution;
- configuration boundary tests;
- resource manifest and target timing distribution;
- independent native-C oracle where practical.

The DDoS application additionally retains its UDP flood, SYN flood, randomized-source, CMS collision, fragment, epoch-wrap, and counter-saturation tests.

## 21. Delivery roadmap

### Phase 0 — freeze contracts

- Confirm the ST67 T02 receive location and normalized packet format.
- Freeze `RTBPF_NET_MD_V1`, action values, link profiles, ownership table, and fault policy.
- Define board resource quotas and management/deployment transport.

**Exit:** constant `PASS` and `DROP` hooks operate without leaks on hardware.

### Phase 1 — static framework core

- Implement interpreter, runtime regions/tags, ARRAY maps, NET_RX launchpad, counters, and static runner.
- Execute small compiled-in generic programs.

**Exit:** host and STM32 interpreter tests agree; watchdog and region checks contain malformed bytecode.

### Phase 2 — verifier and host SDK

- Implement structural/type/bounds/cost verification.
- Add restricted SDK, clang build, ELF reader, relocations, and compact packer.

**Exit:** independently authored restricted-C programs can be packed and executed; negative corpus is rejected.

### Phase 3 — DDoS application

- Package the native-oracle-equivalent CMS filter as an ordinary bundle.
- Validate against the corpus and hardware budgets in the application plan.

**Exit:** the same framework binary runs the DDoS bundle without algorithm-specific firmware code.

### Phase 4 — dynamic loader

- Add inactive arenas, transactional loading, atomic replacement, grace periods, map policies, and management APIs.
- Stress replacement under receive load.

**Exit:** valid bundles replace each other without reboot, packet-buffer faults, partial publication, or stale accesses.

### Phase 5 — secure persistent deployment

- Add signatures, protected key policy, anti-rollback, dual persistent slots, boot recovery, and authenticated transport integration.

**Exit:** power-loss and adversarial-loader tests always select a complete authorized generation or known-good recovery program.

### Phase 6 — generic framework qualification

- Add at least two non-DDoS programs, such as UDP-port policy and EtherType/protocol accounting.
- Port to a second frame-exposing network adapter or native Ethernet.
- Publish measured resource envelopes and adapter conformance tests.

**Exit:** applications and adapters vary independently without modifying the VM core.

## 22. Initial acceptance criteria

RTBPF-NET is a usable dynamically programmable framework when all of the following hold:

1. A restricted-C program can be compiled, packed, verified, loaded, attached, replaced, and detached without rebuilding/rebooting firmware.
2. The target independently rejects malformed, incompatible, unsafe, over-budget, and unauthorized bundles.
3. Every accepted program has a finite verified bound and a runtime watchdog.
4. Bytecode can access only its context, read-only packet, stack, declared maps, and allowlisted helpers.
5. The RX path performs no allocation, blocking, bundle parsing, signature operation, or flash operation.
6. `PASS`, `DROP`, and every fault preserve exactly-once buffer ownership.
7. Program replacement cannot expose partially initialized code/maps or reclaim an active generation.
8. Power failure cannot promote an incomplete persistent bundle.
9. No-hook and maximum-program overhead are measured on STM32H563 under minimum-frame flood conditions.
10. The DDoS CMS application and at least two unrelated filters run on the unchanged framework.
11. The ST67 adapter and one second packet-source adapter pass the same conformance suite.
12. Framework and application telemetry identify active generation, actions, faults, resource use, and cycle maxima without per-packet logging.

## 23. Decisions to freeze before implementation

- Exact ST67W61 package version and NET_IF-to-lwIP handoff call site.
- Whether that handoff is Ethernet-like for every supported mode.
- Development and production bundle transports.
- Signature algorithm, key storage, signer permissions, and rollback authority.
- Static dual arenas versus a fixed-block loader pool.
- Fault policy: fail closed, fail open, or recovery program per interface.
- Maximum bundle/program/map/verifier sizes.
- Config-map update synchronization and authorization.
- Whether V1 permits only one RX worker and one attached program per interface.
- Persistent slot placement and flash erase/write interruption behavior.

Freeze these as board-profile policy rather than embedding them in packet-filter source code.

## 24. Relationship to existing documents

- [STM32_SIMPLIFIED_DDOS_FILTER_PLAN.md](STM32_SIMPLIFIED_DDOS_FILTER_PLAN.md): first packet-filter application, algorithm, maps, parser policy, and evaluation.
- [AIRPORT_EBPF_STM32_PORTING_PATH.md](AIRPORT_EBPF_STM32_PORTING_PATH.md): migration analysis for CMS, ElasticSketch, and RAPID concepts.
- [DESIGN.md](DESIGN.md) and [IMPLEMENTATION.md](IMPLEMENTATION.md): ESP8266-AT application-payload VM fallback; its socket payload ABI remains separate from `NET_RX`.

The packet framework may reuse one interpreter, verifier, map core, loader, and bundle format across multiple program types, but each program type has a distinct context ABI, helper allowlist, actions, execution budget, and attachment policy. A payload program must never be attachable to a packet hook merely because both use the same VM.
