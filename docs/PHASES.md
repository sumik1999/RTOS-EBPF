# RTBPF-NET implementation phases

Each phase ends with an executable gate and a written report under `docs/`. Estimates assume two experienced engineers and exclude a major undocumented Wi-Fi-driver rewrite.

## Phase 1 — static core and frozen V1 primitives — complete

**Goal:** establish a testable, allocation-free runtime foundation before accepting external programs.

Deliverables:

- instruction, packet-context, link-profile, and action headers;
- portable C interpreter and static runner;
- runtime capability tags, checked memory access, and watchdog;
- ARRAY/CONFIG_ARRAY map storage;
- static NET_RX attachment and counters;
- compiled-in generic filters and host containment tests.

Gate: normal and sanitizer tests pass; malformed runtime actions cannot access memory outside registered regions or exceed their instruction limit.

## Phase 2 — load-time verifier and restricted SDK — complete

**Goal:** prove accepted programs safe before execution and establish the authoring path.

Deliverables:

- structural control-flow validation;
- abstract register/stack state;
- packet `data_end`, map-null, and map-value proofs;
- longest-path instruction/cost accounting;
- restricted C SDK headers;
- host ELF inspection and compact packer prototype;
- negative corpus and verifier fuzz target.

Gate: every accepted corpus/random program terminates within its bound and agrees with the runtime model.

## Phase 3 — STM32H563 and ST67 T02 packet adapter — software implemented; hardware validation pending

**Goal:** execute verified static programs at the real deferred receive hook.

Deliverables:

- FreeRTOS/CMSIS portability layer and static runner reservation;
- confirmed ST67 NET_IF-to-lwIP hook location and packet format;
- DMA/cache ordering and link-profile normalization;
- exact PASS/DROP/ABORTED buffer ownership;
- DWT cycle instrumentation and reset/disconnect tests.

Gate: constant pass/drop and a checked sample filter run on hardware without leaks, duplicate release, ISR execution, or cache corruption.

## Phase 4 — DDoS CMS application vertical slice

**Goal:** prove that a useful application is independent from trusted firmware.

Deliverables:

- native-C oracle and packet corpus;
- restricted-C Ethernet/IPv4/UDP/TCP parser;
- aggregate limiter and 2x128 CMS maps;
- CONFIG and telemetry maps;
- bytecode/oracle differential and hardware flood measurements.

Gate: bytecode and oracle actions/map transitions agree for the complete corpus within the hook budget.

## Phase 5 — dynamic transactional loading

**Goal:** load and replace programs without firmware rebuild or reboot.

Deliverables:

- canonical compact bundle decoder with checked arithmetic;
- resource admission, relocations, and inactive arenas;
- map initialization/persistence policy;
- atomic attachment publication and grace period;
- management-task API and replacement-under-load tests.

Gate: any pre-publication failure leaves the old generation unchanged; no old generation is reclaimed while active.

## Phase 6 — authenticated persistent deployment

**Goal:** make runtime updates suitable for production operations.

Deliverables:

- bundle hashes/signatures and signer policy;
- protected production key configuration;
- anti-rollback generation;
- dual persistent slots and boot recovery;
- authenticated transport integration boundary;
- power-loss and rollback test matrix.

Gate: reboot or interrupted update selects a complete authorized generation or known-good recovery program.

## Phase 7 — framework qualification

**Goal:** demonstrate genericity and publish a supportable V1 envelope.

Deliverables:

- DDoS filter plus two unrelated packet programs;
- second packet-source adapter or native Ethernet port;
- loader/verifier/adapter fuzz and stress results;
- measured flash, RAM, throughput, latency, and cycle maxima;
- release documentation and conformance suite.

Gate: all acceptance criteria in the framework specification pass with reproducible evidence.
