# V1 primitive contracts

Status: **Phase-2 implementation baseline**. A prototype host bundle exists, but dynamic target loading remains deferred to Phase 5.

## Execution

- One program runs synchronously per invocation.
- One runner serves one non-concurrent RX worker.
- The runner has eleven registers and a statically reserved 512-byte stack.
- `r1` begins as a context capability; `r10` begins one byte beyond the VM stack.
- Programs must return `RTBPF_NET_PASS` (2) or `RTBPF_NET_DROP` (1).
- Faults and unknown return values are accounted as `ABORTED`; the trusted hook applies its configured pass/drop fault action.
- `maximum_executed_instructions` is always enforced, including for built-in programs.

The interpreter retains its watchdog and can contain a loop in direct runtime tests. All attachment paths invoke the Phase-2 verifier, which rejects backward and self branches.

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

Phase 2 attaches an immutable `rtbpf_program_t` supplied by firmware only after semantic verification succeeds. Verification proves initialized values, context/packet/stack/map bounds, map-null refinement, helper typing, valid actions, acyclic control flow, and an instruction-path bound. Attach/detach is permitted only when the RX worker is quiescent. There is no atomic replacement or grace period yet. Phase 5 replaces this contract with immutable generation publication and reclamation.

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

The launchpad returns an action without exposing driver ownership to bytecode. The Phase 3 RX adapter implements:

```text
PASS + handoff taken       -> upper layer owns or has consumed the buffer
PASS + handoff rejected    -> adapter releases once
DROP / fail-closed ABORTED -> adapter releases once
invalid/ISR/disabled frame -> adapter releases once
no hook                    -> normal handoff path
```

A handoff callback must explicitly distinguish `RTBPF_HANDOFF_TAKEN` from `RTBPF_HANDOFF_REJECTED`. “Taken” includes an upper layer that rejected a pbuf but already consumed/released it internally; this prevents a second release by the adapter.

`private_buffer_handle` remains adapter-only and is never copied into the VM-visible context. The ST67 glue retains it only in a trusted fixed-pool custom-pbuf wrapper until lwIP finishes.
