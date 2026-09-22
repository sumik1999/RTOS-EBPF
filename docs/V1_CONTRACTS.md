# V1 primitive contracts

Status: **Phase-1 implementation baseline**. Dynamic bundles are not compatible until Phase 2/5 assign a versioned serialization.

## Execution

- One program runs synchronously per invocation.
- One runner serves one non-concurrent RX worker.
- The runner has eleven registers and a statically reserved 512-byte stack.
- `r1` begins as a context capability; `r10` begins one byte beyond the VM stack.
- Programs must return `RTBPF_NET_PASS` (2) or `RTBPF_NET_DROP` (1).
- Faults and unknown return values are accounted as `ABORTED`; the trusted hook applies its configured pass/drop fault action.
- `maximum_executed_instructions` is always enforced, including for built-in programs.

Phase 1 deliberately allows the runtime to encounter loops so the watchdog can be tested. Phase 2 load-time verification will reject backward and self branches.

## Memory capabilities

Registers carry runtime tags. An integer is never interpreted as an address.

| Capability | Read | Write | Lifetime |
|---|---:|---:|---|
| Context | allowlisted bounded loads | no | invocation |
| Packet | bounded loads | no | invocation |
| VM stack | bounded loads | bounded stores | invocation |
| Map handle | helper argument only | no | program generation |
| Map value | bounded loads | only with map write flag | invocation |

Packet and map-value capabilities cannot be retained after return. Phase 2 adds equivalent load-time proofs.

## NET_RX context

`rtbpf_net_md_v1_t` is 40 bytes:

| Offset | Width | Field |
|---:|---:|---|
| 0 | 4 | `data` packet capability |
| 4 | 4 | `data_end` packet-end capability |
| 8 | 4 | `ingress_port` |
| 12 | 4 | `link_type` |
| 16 | 4 | `packet_length` |
| 20 | 4 | `window_epoch` |
| 24 | 4 | `rssi_dbm` |
| 28 | 4 | `channel` |
| 32 | 8 | `timestamp_ns` |

The two pointer fields are capability tokens. Their stored integer representation is not a native pointer and must never be consumed as one.

Initial link values:

- 1: Wi-Fi-exported normalized Ethernet frame;
- 2: native/other normalized Ethernet frame;
- 3: raw IP packet.

Raw 802.11, radiotap, vendor transport framing, aggregates, and incomplete chained packets are not valid profiles.

## Static attachment

Phase 1 attaches an immutable `rtbpf_program_t` supplied by firmware. Attach/detach is permitted only when the RX worker is quiescent. There is no atomic replacement or grace period yet. Phase 5 replaces this contract with immutable generation publication and reclamation.

With no program attached, the hook passes directly and increments `no_program`.

## Maps

Phase 1 supports fixed ARRAY and CONFIG_ARRAY storage:

- keys are exactly one host-endian `uint32_t` index in the current in-memory API;
- storage is caller-provided and zeroed at initialization;
- lookup is constant time;
- no resize, allocation, eviction, or probing occurs;
- bytecode write access requires `RTBPF_MAP_F_PROGRAM_WRITE`.

The future bundle representation will define canonical little-endian serialized keys and initial values separately from this native in-memory API.

## Driver ownership boundary

The current host launchpad returns an action but does not own or release the driver buffer. A Phase 3 adapter must implement:

```text
PASS      -> exactly one normal lwIP handoff
DROP      -> exactly one driver release/requeue
ABORTED   -> trusted fault action, then exactly one terminal transition
no hook   -> exactly one normal lwIP handoff
```

`private_buffer_handle` remains adapter-only and is never copied into the VM-visible context.
