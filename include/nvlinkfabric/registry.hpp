#pragma once
#include "nvlinkfabric/enums.hpp"
#include "nvlinkfabric/identities.hpp"
#include "nvlinkfabric/evidence.hpp"
#include "nvlinkfabric/topology.hpp"
#include "nvlinkfabric/capability.hpp"
#include "nvlinkfabric/measurement.hpp"
#include "nvlinkfabric/path_quality.hpp"
#include "nvlinkfabric/policy.hpp"
#include "nvlinkfabric/route.hpp"
#include "nvlinkfabric/limits.hpp"
#include "nvlinkfabric/durable.hpp"
#include "nvlinkfabric/time.hpp"
#include "nvlinkfabric/result.hpp"
#include <cstddef>
#include <deque>
#include <memory>
#include <optional>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace nvlinkfabric {

//---------------------------------------------------------------------------
// Observation: an immutable, provenance-stamped record of a fact used by the
// fabric. Route decisions reference the observations that justified them so the
// decision remains inspectable after it is superseded.
//---------------------------------------------------------------------------
struct Observation {
    ObservationId id;
    std::string kind;        // e.g. "device", "link", "measurement", "topology"
    DeviceId source;
    DeviceId target;
    Evidence evidence;
    Timestamp observed_at;
    std::string summary;

    bool stale{false};       // recomputed by the registry when authority lapses
};

//---------------------------------------------------------------------------
// ConsistentView: a single immutable, self-consistent snapshot of the topology
// together with the bounded measurement history, captured under one lock.
// Route/path calculations run on this view without holding any lock.
//---------------------------------------------------------------------------
struct ConsistentView {
    TopologySnapshot topology;
    std::vector<MeasurementRecord> measurements;
};

//---------------------------------------------------------------------------
// TopologyRegistry: the canonical, thread-safe holder of topology, capability,
// dynamic evidence, measurements, and generated route decisions.
//
// Read path: callers obtain an immutable shared_ptr<const State> snapshot and
// operate on it WITHOUT holding a lock, so heavy enumeration never blocks a
// writer. Write path: under a unique lock a copy is made, mutated, and
// atomically swapped; readers keep their private immutable snapshot.
//
// This structure precludes read-lock-then-write on the same lock and topology
// mutation during enumeration, and gives deterministic, race-free results.
//---------------------------------------------------------------------------
class TopologyRegistry {
public:
    struct State;

    explicit TopologyRegistry(Limits limits = Limits{});
    ~TopologyRegistry();
    TopologyRegistry(const TopologyRegistry&) = delete;
    TopologyRegistry& operator=(const TopologyRegistry&) = delete;

    // ---- accessors ------------------------------------------------------
    TopologyGeneration topology_generation() const;
    std::size_t device_count() const;
    std::size_t link_count() const;
    std::size_t measurement_count() const;
    const Limits& limits() const noexcept { return limits_; }

    TopologySnapshot snapshot() const;
    ConsistentView consistent_view() const;
    std::optional<DeviceRecord> device(DeviceId id) const;
    std::optional<LinkRecord> link(LinkId id) const;
    std::vector<MeasurementRecord> recent_measurements(std::size_t max) const;

    // ---- mutations ------------------------------------------------------
    // Each mutation that alters canonical topology advances the topology
    // generation, invalidating prior route decisions.
    Result<void> upsert_device(DeviceRecord device);
    Result<void> remove_device(DeviceId device);
    Result<void> upsert_link(LinkRecord link);
    Result<void> remove_link(LinkId link);
    Result<void> update_link_state(LinkId link, LinkOperationalState state,
                                   Evidence evidence, bool degraded,
                                   std::string note);
    Result<void> update_device_health(DeviceId device, DeviceHealthState health,
                                      Evidence evidence);
    Result<void> advance_topology(Evidence evidence);

    // ---- evidence --------------------------------------------------------
    Result<void> publish_observation(Observation observation);
    Result<void> publish_measurement(MeasurementRecord measurement);

    // ---- path enumeration & quality -------------------------------------
    Result<std::vector<Path>> enumerate_paths(DeviceId source, DeviceId target) const;
    Result<PathQuality> compute_path_quality(const Path& path) const;

    // ---- route governance -------------------------------------------------
    Result<RouteDecision> decide_route(DeviceId source, DeviceId target,
                                       const RoutePolicy& policy) const;
    bool is_route_executable(const RouteDecision& decision) const;
    Result<RouteDecision> revalidate_route(const RouteDecision& decision) const;

    // ---- authority / recovery --------------------------------------------
    // Marks every dynamic (worker-owned) observation/measurement and any link
    // state that was not re-confirmed as REVALIDATION_REQUIRED. Used after a
    // coordinator restart so persisted dynamic evidence never recovers fresh.
    void mark_dynamic_evidence_revalidation_required();

    // ---- durable state ---------------------------------------------------
    // Captures the DURABLE hardware/capability knowledge. Dynamic evidence is
    // never captured here.
    Result<DurableState> capture_durable_state() const;
    // Restores durable knowledge and marks all dynamic evidence
    // REVALIDATION_REQUIRED. Persisted dynamic observations never recover fresh.
    Result<void> restore_durable_state(const DurableState& state);

    // ---- explanation -----------------------------------------------------
    std::string explain_path(const Path& path) const;
    std::vector<Observation> observations_for(const RouteDecision& decision) const;

private:
    // Returns the current immutable snapshot. Reading the shared_ptr happens
    // under a shared lock; the caller then uses the snapshot lock-free.
    std::shared_ptr<const State> read_state() const;

    mutable std::shared_mutex mu_;
    std::shared_ptr<const State> state_;
    Limits limits_;
};

}  // namespace nvlinkfabric
