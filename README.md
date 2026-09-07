# NVLink Fabric

**Which accelerator-to-accelerator NVLink-class connectivity exists now, what
topology and link capabilities does it expose, what path quality is supported
by evidence, what contention or degradation affects that path, and is the
evidence still authoritative enough to permit a route decision?**

NVLink Fabric is a C++20 runtime for discovering, modeling, measuring,
explaining, and governing NVLink-class accelerator interconnects. Its core
thesis is that *connectivity is not merely topology*: two devices may appear
connected while a link is unavailable, degraded, unsupported, stale in the
current process incarnation, below expected capability, inaccessible under
current topology, or represented only by synthetic evidence. A route is
therefore justified from *current authoritative evidence*, not inferred from
static device presence.

This is a complete repository: library, inspection CLI, examples, benchmarks,
and a multi-process coordinator/worker runtime. It is released as version 1.0.0.

---

## Precise systems boundary

NVLink Fabric **owns**:

- discovery of NVLink-class device-to-device connectivity;
- typed device/link/path identity;
- topology snapshots and generations;
- link-capability and link operational-state representation;
- evidence provenance and freshness;
- deterministic path enumeration and path-quality evidence;
- bandwidth/latency measurement where technically supportable;
- explicit REAL / SYNTHETIC / UNSUPPORTED evidence classification;
- deterministic path eligibility and route ranking;
- route explanation and stale-evidence rejection;
- topology-change invalidation;
- process-incarnation fencing for worker-supplied dynamic evidence;
- persisted durable topology/capability knowledge;
- conservative restart/revalidation semantics for dynamic observations;
- optional NVIDIA/NVML/CUDA backend integration;
- vendor-neutral core interfaces.

NVLink Fabric **does not own**: collective algorithms/scheduling, NCCL or MPI
replacement, RDMA, GPUDirect RDMA/Storage, generic PCIe governance, fabric-wide
bandwidth arbitration, workload/admission placement, compute or memory
scheduling, NVSwitch control-plane semantics beyond representing a traversed
path, cluster-wide network routing, DPU/NIC offload, CXL, generic topology
scheduling, arbitrary CUDA IPC, or application-level tensor transport. These
belong to neighboring runtimes (Topology Fabric, PCIe Fabric, Collective
Fabric, Fabric Scheduler, Bandwidth Governor, Congestion Fabric, NVSwitch
Fabric, GPU Direct Fabric, Resource Broker).

**NVLink Fabric does not replace NCCL.** It exposes narrow typed interfaces that
those systems may consume.

---

## Architecture

The library is layered:

- **Core** (nvlinkfabric): vendor-neutral topology, capability, path-quality,
  route governance, persistence, and the multiprocess coordinator/worker
  runtime. No CUDA/NVML dependency.
- **Backends** (vendor-neutral): synthetic and no-op backends always; the
  NVIDIA/CUDA/NVML backend is an optional, separately-exported target
  (nvlinkfabric_nvidia).

The registry is the central, thread-safe holder of canonical state. Readers
obtain an immutable snapshot and compute lock-free; writers serialize mutations
under a single lock and atomically swap a fresh immutable state
(copy-on-write). This precludes read-lock-followed-by-write, mutation during
enumeration, and inconsistent lock ordering by construction.

---

## Key concepts

### Strongly typed identities

Distinct types are never interchanged with raw integers:

DeviceId, DeviceGeneration, LinkId, LinkGeneration, PathId, TopologyGeneration,
TopologySnapshotId, WorkerId, WorkerBootId, CoordinatorEpoch, ObservationId,
MeasurementId, RouteDecisionId, RouteGeneration, PolicyId.

### Evidence classes

Every observation and result carries an explicit class: **REAL**, **SYNTHETIC**,
or **UNSUPPORTED**. Typed provenance is additive (e.g. NVML, CUDA_RUNTIME,
CUDA_DRIVER, OPERATING_SYSTEM, BENCHMARK, PERSISTED, DERIVED,
SYNTHETIC_FIXTURE). **UNKNOWN** is a real, distinct state and is never silently
collapsed into UP / PRESENT / SUPPORTED. A persisted measurement never silently
becomes a current dynamic observation after restart.

### Topology and generations

Topology is an explicit graph: devices are endpoints (plus optional switch
elements), links are directed-or-undirected edges. Parallel links, asymmetric
observation, disconnected components, and multiple candidate paths are
represented. Every device/link carries its own monotonically increasing
generation. The topology itself carries a TopologyGeneration that advances on
any canonical topology change, invalidating prior route decisions. Backward
generation movement, stale generation replay, and contradictory same-generation
updates are rejected.

### Capability vs. state

Static-ish capability (interconnect technology, nominal bandwidth, link count,
peer-access support, measurement support) is separated from dynamic
operational state (UP/DOWN/DEGRADED/REVALIDATION_REQUIRED, active link count,
measured bandwidth/latency, contention, freshness, worker ownership).

### Path quality

Path quality is explicit factorized evidence (hop count, NVLink segments, switch
traversal, directness, nominal/measured bandwidth, measured latency, active /
degraded / down link counts, freshness, support status), not a hidden scalar.
A composite ranking is a documented weighted sum of named factors with stable
deterministic tie-breaking.

### Route governance

A RouteDecision records source, target, topology generation, endpoint
generations, candidate paths considered, rejected candidates with named
reasons, the selected path, the policy, evidence references, an explanation, and
whether it is currently executable. Outcomes include ROUTE_ALLOWED,
ROUTE_ALLOWED_DEGRADED, NO_ROUTE, STALE_TOPOLOGY, REVALIDATION_REQUIRED,
UNSUPPORTED, INSUFFICIENT_EVIDENCE, POLICY_REJECTED, ENDPOINT_STALE, and
LINK_STALE. A decision created under topology generation N is not executable
after the topology changes until explicitly revalidated; stale decisions fail
closed. Same canonical state plus same policy always produces the same decision.

### Process incarnation and coordinator authority

Workers register with a WorkerId and a fresh WorkerBootId. The coordinator holds
a CoordinatorEpoch. Stale boot identities and stale epochs are rejected. A real
worker process death is detected via connection closure, and that worker's
dynamic evidence is marked REVALIDATION_REQUIRED. A new worker with a fresh boot
may restore eligibility; old route decisions do not silently regain authority.
Coordinator restart advances the epoch; durable topology/capability knowledge
recovers, while all dynamic evidence is conservatively marked for revalidation.

---

## Backends

### NVIDIA / CUDA / NVML backend

The NVIDIA backend discovers devices honestly via the CUDA runtime and NVML
(e.g. architecture, PCI identity, CUDA ordinal as process-local evidence only),
and reports NVLink link connectivity *only* when present. A runtime CUDA
self-check performs a genuine allocation, a real NVRTC-compiled kernel, H2D/D2H
transfers, CPU-reference parity, and verifies that device memory returns to its
baseline. **CUDA integration proves CUDA, not NVLink.** NVLink proof requires at
least two real NVLink-connected devices. Unsupported/missing NVML, unsupported
functions, and absent NVLink are reported as clean typed UNSUPPORTED results
rather than fabricated.

On the validation host (a single NVIDIA GeForce RTX 5090, Blackwell), device
discovery is REAL and NVLink connectivity is honestly **UNSUPPORTED** (this
consumer GPU exposes no NVLink).

### Synthetic backend

A rigorous synthetic fixture exercises semantics current hardware cannot
provide: direct links, parallel links, disconnected devices, degraded/failed
links, topology generation advance, endpoint restart, stale measurement, stale
topology replay, multiple candidate paths, direct vs. switch-mediated paths,
path bottleneck, asymmetric observation, route decision invalidation,
deterministic tie-breaking, bounded traversal, and synthetic contention
evidence. All synthetic records are permanently marked **SYNTHETIC** and never
masquerade as hardware topology.

### No-op backend

A portability backend that reports no devices/links and UNSUPPORTED
measurement.

---

## Build

Requirements: a C++20 compiler (MSVC 2022 17.x is validated), CMake >= 3.20,
and (optionally) a CUDA toolkit for the NVIDIA backend.

    cmake -S . -B build -G "Visual Studio 17 2022" -A x64
    cmake --build build --config Release
    ctest --test-dir build -C Release --output-on-failure

Key options: NVF_BUILD_TESTS, NVF_BUILD_EXAMPLES, NVF_BUILD_BENCHMARKS,
NVF_BUILD_CLI, NVF_ENABLE_CUDA, NVF_ENABLE_ASAN, NVF_INSTALL.

First-party code builds clean at MSVC /W4 /WX (no globally suppressed
warnings). An AddressSanitizer-instrumented CPU/core subset is provided and
validated; the CUDA backend is not ASan-instrumented (a CUDA-runtime conflict),
so ASan coverage is reported only for the vendor-neutral core.

No test uses a timeout; a hanging test is treated as a defect and fixed.

---

## Tests

The suite covers unit, property/randomized, race, and adversarial cases,
including:

- real CUDA/NVML/NVLink validation (honest UNSUPPORTED where no NVLink exists);
- persistence round-trip and corruption / truncation / trailing-garbage
  rejection;
- protocol frame validation (malformed header, absurd length, bad checksum,
  unknown version, invalid type, trailing garbage);
- multiprocess worker registration, real OS-process death, dynamic-evidence
  invalidation, fresh-boot recovery, and stale boot/epoch replay rejection;
- deterministic seeded randomized topology mutations with invariant checks
  (no dangling references, monotonic generations, bounded traversal, stable
  deterministic route rankings);
- concurrent mutation/query/measurement races;
- adversarial inputs (self-links, unknown endpoints, stale generations,
  conflicting same-generation updates, path explosion bounds, absurd
  measurement values, huge serialized counts).

---

## Examples

Buildable examples use only the public/installed API:

- nvf_example_synthetic — construct a synthetic topology and select a route;
- nvf_example_stale — show topology-change invalidation and revalidation;
- nvf_example_nvidia — NVIDIA backend discovery and honest UNSUPPORTED NVLink
  handling (built when CUDA is enabled).

---

## Inspection CLI

nvlink_fabric_inspect lists devices, links, topology, paths, and route
decisions, always making REAL / SYNTHETIC / UNSUPPORTED explicit, with optional
--json output.

    build/Release/nvlink_fabric_inspect devices --backend nvidia
    build/Release/nvlink_fabric_inspect route 1000 1002 --backend synthetic

---

## Installation and downstream use

    cmake --install build --config Release --prefix <prefix>

Then an independent consumer can find_package(NVLinkFabric) and link
nvlinkfabric::nvlinkfabric (and optionally nvlinkfabric::nvidia) from any build
tree that does not depend on the source tree.

---

## Hardware validation status

| Capability                                      | Result        |
|-------------------------------------------------|---------------|
| CUDA device discovery                           | REAL PASS     |
| real CUDA alloc / kernel / parity / free        | REAL PASS     |
| multiple physical GPUs                          | UNSUPPORTED   |
| CUDA peer access                                | UNSUPPORTED   |
| NVLink discovery                                | UNSUPPORTED   |
| real NVLink adjacency                           | UNSUPPORTED   |
| real NVLink bandwidth measurement               | UNSUPPORTED   |
| real NVLink latency measurement                 | UNSUPPORTED   |
| NVSwitch presence                               | UNSUPPORTED   |
| multi-node NVLink                               | UNSUPPORTED   |
| synthetic multi-GPU topology                    | SYNTHETIC PASS|
| synthetic route failure/recovery                | SYNTHETIC PASS|

The validation host has a single NVIDIA GeForce RTX 5090. It proves CUDA
integration with REAL evidence and honestly reports that NVLink connectivity is
**UNSUPPORTED on this hardware**. Synthetic multi-GPU topologies are synthetic.

---

## Genuine limitations

- No NVLink-capable multi-GPU hardware was available, so real NVLink
  bandwidth/latency has not been measured; the NVIDIA backend reports
  UNSUPPORTED for NVLink measurement rather than fabricating numbers.
- The runtime does not program NVLink hardware paths or reroute traffic; it
  governs whether a path is eligible and preferred. No hardware path
  programming is claimed.
- The coordinator/worker transport is a loopback TCP framed protocol; a real
  deployment would still be single-node for this release.
- ASan coverage is limited to the vendor-neutral CPU/core subset (the CUDA
  backend is not ASan-instrumented).
- Synthetic topologies are explicitly synthetic and cannot substitute for
  hardware proof.

## License

Apache License 2.0. Copyright 2026 Summon Software Labs. No telemetry transmission.
