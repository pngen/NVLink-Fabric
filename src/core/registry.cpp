#include "nvlinkfabric/registry.hpp"
#include "detail/state.hpp"
#include <algorithm>
#include <mutex>

namespace nvlinkfabric {

namespace {
TopologySnapshot build_snapshot(const TopologyRegistry::State& s) {
    TopologySnapshot snap;
    snap.generation = s.topology_gen;
    snap.snapshot_id = fresh_id<TopologySnapshotTag>();
    snap.created_at = now_timestamp();
    snap.devices.reserve(s.devices.size());
    snap.links.reserve(s.links.size());
    for (const auto& kv : s.devices) snap.devices.push_back(kv.second);
    for (const auto& kv : s.links) snap.links.push_back(kv.second);
    std::sort(snap.devices.begin(), snap.devices.end(),
              [](const DeviceRecord& a, const DeviceRecord& b) { return a.id < b.id; });
    std::sort(snap.links.begin(), snap.links.end(),
              [](const LinkRecord& a, const LinkRecord& b) { return a.id < b.id; });
    return snap;
}

bool device_equal(const DeviceRecord& a, const DeviceRecord& b) {
    return a.id == b.id && a.logical_name == b.logical_name && a.kind == b.kind &&
           a.backend == b.backend && a.backend_instance == b.backend_instance &&
           a.pci_identity == b.pci_identity && a.cuda_ordinal == b.cuda_ordinal &&
           a.vendor == b.vendor && a.architecture == b.architecture &&
           a.device_generation == b.device_generation &&
           a.capability.equivalent(b.capability) && a.health == b.health &&
           a.evidence == b.evidence && a.generation == b.generation &&
           a.owner_worker == b.owner_worker && a.owner_boot == b.owner_boot;
}

bool link_equal(const LinkRecord& a, const LinkRecord& b) {
    return a.id == b.id && a.source == b.source && a.target == b.target &&
           a.directed == b.directed && a.physical_identifier == b.physical_identifier &&
           a.state == b.state && a.technology == b.technology &&
           a.capability.equivalent(b.capability) && a.generation == b.generation &&
           a.evidence == b.evidence && a.last_observation == b.last_observation &&
           a.measurement_supported == b.measurement_supported &&
           a.degraded == b.degraded && a.degradation_note == b.degradation_note &&
           a.topology_generation == b.topology_generation;
}

Observation make_observation(std::string kind, DeviceId src, DeviceId tgt,
                             const Evidence& ev, std::string summary) {
    Observation o;
    o.id = fresh_id<ObservationTag>();
    o.kind = std::move(kind);
    o.source = src;
    o.target = tgt;
    o.evidence = ev;
    o.observed_at = now_timestamp();
    o.summary = std::move(summary);
    return o;
}
}  // namespace

TopologyRegistry::TopologyRegistry(Limits limits) : limits_(limits) {
    auto s = std::make_shared<State>();
    s->topology_gen = Generation<TopologyTag>(1u);
    state_ = std::move(s);
}

TopologyRegistry::~TopologyRegistry() = default;

std::shared_ptr<const TopologyRegistry::State> TopologyRegistry::read_state() const {
    std::shared_lock lock(mu_);
    return state_;
}

TopologyGeneration TopologyRegistry::topology_generation() const {
    auto s = read_state();
    return s->topology_gen;
}

std::size_t TopologyRegistry::device_count() const {
    auto s = read_state();
    return s->devices.size();
}

std::size_t TopologyRegistry::link_count() const {
    auto s = read_state();
    return s->links.size();
}

std::size_t TopologyRegistry::measurement_count() const {
    auto s = read_state();
    return s->measurement_history.size();
}

TopologySnapshot TopologyRegistry::snapshot() const {
    auto s = read_state();
    return build_snapshot(*s);
}

ConsistentView TopologyRegistry::consistent_view() const {
    auto s = read_state();
    ConsistentView view;
    view.topology = build_snapshot(*s);
    view.measurements.assign(s->measurement_history.begin(), s->measurement_history.end());
    return view;
}

std::optional<DeviceRecord> TopologyRegistry::device(DeviceId id) const {
    auto s = read_state();
    auto it = s->devices.find(id);
    if (it == s->devices.end()) return std::nullopt;
    return it->second;
}

std::optional<LinkRecord> TopologyRegistry::link(LinkId id) const {
    auto s = read_state();
    auto it = s->links.find(id);
    if (it == s->links.end()) return std::nullopt;
    return it->second;
}

std::vector<MeasurementRecord> TopologyRegistry::recent_measurements(std::size_t max) const {
    auto s = read_state();
    std::vector<MeasurementRecord> out;
    if (max == 0u) return out;
    const std::size_t take = std::min(max, s->measurement_history.size());
    auto it = s->measurement_history.begin();
    std::advance(it, static_cast<std::ptrdiff_t>(s->measurement_history.size() - take));
    out.assign(it, s->measurement_history.end());
    return out;
}

Result<void> TopologyRegistry::upsert_device(DeviceRecord device) {
    std::unique_lock lock(mu_);
    State next = *state_;
    if (!device.id || !device.generation.valid())
        return Result<void>::err(ErrorCode::INVALID_ARGUMENT, "device requires a valid id and generation");
    auto it = next.devices.find(device.id);
    bool topology_changed = false;
    if (it == next.devices.end()) {
        if (next.devices.size() >= limits_.max_devices)
            return Result<void>::err(ErrorCode::RESOURCE_LIMIT, "max devices reached");
        next.devices.emplace(device.id, device);
        topology_changed = true;
    } else {
        const DeviceRecord& cur = it->second;
        if (device.generation < cur.generation)
            return Result<void>::err(ErrorCode::STALE_GENERATION, "device update is stale");
        if (device.generation == cur.generation) {
            if (!device_equal(device, cur))
                return Result<void>::err(ErrorCode::STALE_GENERATION,
                    "conflicting updates with identical device generation");
            return Result<void>::ok();
        }
        it->second = std::move(device);
        topology_changed = true;
    }
    next.observations.push_back(make_observation("device", device.id, device.id,
        device.evidence, "device " + device.logical_name));
    if (next.observations.size() > limits_.max_observations) next.observations.pop_front();

    if (topology_changed) {
        next.topology_gen = state_->topology_gen.next();
        for (auto& lk : next.links) lk.second.topology_generation = next.topology_gen;
    }
    state_ = std::make_shared<const State>(std::move(next));
    return Result<void>::ok();
}

Result<void> TopologyRegistry::remove_device(DeviceId device) {
    std::unique_lock lock(mu_);
    State next = *state_;
    auto it = next.devices.find(device);
    if (it == next.devices.end())
        return Result<void>::err(ErrorCode::NOT_FOUND, "device not present");
    // Remove any links that referenced the removed endpoint (no dangling edges).
    for (auto lit = next.links.begin(); lit != next.links.end();) {
        if (lit->second.source == device || lit->second.target == device)
            lit = next.links.erase(lit);
        else
            ++lit;
    }
    next.devices.erase(it);
    next.observations.push_back(make_observation("device", device, device,
        Evidence{EvidenceClass::UNSUPPORTED, Provenance::DERIVED, "registry", "device removed"},
        "device removed"));
    if (next.observations.size() > limits_.max_observations) next.observations.pop_front();

    next.topology_gen = state_->topology_gen.next();
    for (auto& lk : next.links) lk.second.topology_generation = next.topology_gen;
    state_ = std::make_shared<const State>(std::move(next));
    return Result<void>::ok();
}

Result<void> TopologyRegistry::upsert_link(LinkRecord link) {
    std::unique_lock lock(mu_);
    State next = *state_;
    if (!link.id || !link.generation.valid())
        return Result<void>::err(ErrorCode::INVALID_ARGUMENT, "link requires a valid id and generation");
    if (link.source == link.target)
        return Result<void>::err(ErrorCode::INVALID_TOPOLOGY, "self-link is not permitted");
    if (next.devices.find(link.source) == next.devices.end())
        return Result<void>::err(ErrorCode::INVALID_TOPOLOGY, "link source device is unknown");
    if (next.devices.find(link.target) == next.devices.end())
        return Result<void>::err(ErrorCode::INVALID_TOPOLOGY, "link target device is unknown");
    if (next.links.size() >= limits_.max_links)
        return Result<void>::err(ErrorCode::RESOURCE_LIMIT, "max links reached");

    // Enforce the per-pair parallel-link bound.
    std::size_t parallel = 0u;
    for (const auto& kv : next.links) {
        const bool same_unordered =
            (kv.second.source == link.source && kv.second.target == link.target) ||
            (kv.second.source == link.target && kv.second.target == link.source);
        if (same_unordered) ++parallel;
    }
    if (parallel >= limits_.max_parallel_links_per_pair)
        return Result<void>::err(ErrorCode::RESOURCE_LIMIT, "too many parallel links for pair");

    auto it = next.links.find(link.id);
    bool topology_changed = false;
    if (it == next.links.end()) {
        next.links.emplace(link.id, link);
        topology_changed = true;
    } else {
        const LinkRecord& cur = it->second;
        if (link.generation < cur.generation)
            return Result<void>::err(ErrorCode::STALE_GENERATION, "link update is stale");
        if (link.generation == cur.generation) {
            if (!link_equal(link, cur))
                return Result<void>::err(ErrorCode::STALE_GENERATION,
                    "conflicting updates with identical link generation");
            return Result<void>::ok();
        }
        it->second = std::move(link);
        topology_changed = true;
    }

    next.observations.push_back(make_observation("link", link.source, link.target,
        link.evidence, "link " + link.physical_identifier));
    if (next.observations.size() > limits_.max_observations) next.observations.pop_front();

    if (topology_changed) {
        next.topology_gen = state_->topology_gen.next();
        for (auto& lk : next.links) lk.second.topology_generation = next.topology_gen;
    }
    state_ = std::make_shared<const State>(std::move(next));
    return Result<void>::ok();
}

Result<void> TopologyRegistry::remove_link(LinkId link) {
    std::unique_lock lock(mu_);
    State next = *state_;
    auto it = next.links.find(link);
    if (it == next.links.end())
        return Result<void>::err(ErrorCode::NOT_FOUND, "link not present");
    const DeviceId src = it->second.source;
    const DeviceId tgt = it->second.target;
    next.links.erase(it);
    next.observations.push_back(make_observation("link", src, tgt,
        Evidence{EvidenceClass::UNSUPPORTED, Provenance::DERIVED, "registry", "link removed"},
        "link removed"));
    if (next.observations.size() > limits_.max_observations) next.observations.pop_front();

    next.topology_gen = state_->topology_gen.next();
    for (auto& lk : next.links) lk.second.topology_generation = next.topology_gen;
    state_ = std::make_shared<const State>(std::move(next));
    return Result<void>::ok();
}

Result<void> TopologyRegistry::update_link_state(LinkId link, LinkOperationalState state,
                                                 Evidence evidence, bool degraded,
                                                 std::string note) {
    std::unique_lock lock(mu_);
    State next = *state_;
    auto it = next.links.find(link);
    if (it == next.links.end())
        return Result<void>::err(ErrorCode::NOT_FOUND, "link not present");
    LinkRecord& lr = it->second;
    lr.state = state;
    lr.evidence = std::move(evidence);
    lr.degraded = degraded;
    lr.degradation_note = std::move(note);
    lr.generation = lr.generation.next();
    lr.last_observation = now_timestamp();

    next.observations.push_back(make_observation("link_state", lr.source, lr.target,
        lr.evidence, "link state=" + std::string(link_state_name(state))));
    if (next.observations.size() > limits_.max_observations) next.observations.pop_front();

    next.topology_gen = state_->topology_gen.next();
    for (auto& lk : next.links) lk.second.topology_generation = next.topology_gen;
    state_ = std::make_shared<const State>(std::move(next));
    return Result<void>::ok();
}

Result<void> TopologyRegistry::update_device_health(DeviceId device, DeviceHealthState health,
                                                    Evidence evidence) {
    std::unique_lock lock(mu_);
    State next = *state_;
    auto it = next.devices.find(device);
    if (it == next.devices.end())
        return Result<void>::err(ErrorCode::NOT_FOUND, "device not present");
    DeviceRecord& dr = it->second;
    dr.health = health;
    dr.evidence = std::move(evidence);
    dr.generation = dr.generation.next();

    next.observations.push_back(make_observation("device_health", device, device,
        dr.evidence, "health=" + std::string(device_health_name(health))));
    if (next.observations.size() > limits_.max_observations) next.observations.pop_front();

    next.topology_gen = state_->topology_gen.next();
    for (auto& lk : next.links) lk.second.topology_generation = next.topology_gen;
    state_ = std::make_shared<const State>(std::move(next));
    return Result<void>::ok();
}

Result<void> TopologyRegistry::advance_topology(Evidence evidence) {
    std::unique_lock lock(mu_);
    State next = *state_;
    next.observations.push_back(make_observation("topology", DeviceId{}, DeviceId{},
        evidence, "topology advanced"));
    if (next.observations.size() > limits_.max_observations) next.observations.pop_front();
    next.topology_gen = state_->topology_gen.next();
    for (auto& lk : next.links) lk.second.topology_generation = next.topology_gen;
    state_ = std::make_shared<const State>(std::move(next));
    return Result<void>::ok();
}

Result<void> TopologyRegistry::publish_observation(Observation observation) {
    std::unique_lock lock(mu_);
    State next = *state_;
    if (!observation.id) observation.id = fresh_id<ObservationTag>();
    observation.stale = false;
    next.observations.push_back(std::move(observation));
    if (next.observations.size() > limits_.max_observations) next.observations.pop_front();
    state_ = std::make_shared<const State>(std::move(next));
    return Result<void>::ok();
}

Result<void> TopologyRegistry::publish_measurement(MeasurementRecord measurement) {
    std::unique_lock lock(mu_);
    State next = *state_;
    if (!measurement.id) measurement.id = fresh_id<MeasurementTag>();
    measurement.stale = false;
    measurement.revalidation_required = false;
    next.measurement_history.push_back(std::move(measurement));
    if (next.measurement_history.size() > limits_.max_measurement_history)
        next.measurement_history.pop_front();
    state_ = std::make_shared<const State>(std::move(next));
    return Result<void>::ok();
}

void TopologyRegistry::mark_dynamic_evidence_revalidation_required() {
    std::unique_lock lock(mu_);
    State next = *state_;
    for (auto& kv : next.measurement_history) {
        kv.stale = true;
        kv.revalidation_required = true;
    }
    for (auto& kv : next.observations) kv.stale = true;
    for (auto& kv : next.links) {
        kv.second.state = LinkOperationalState::REVALIDATION_REQUIRED;
        kv.second.degraded = false;
        kv.second.degradation_note = "revalidation required after authority restart";
    }
    for (auto& kv : next.devices) {
        if (kv.second.owner_worker.has_value())
            kv.second.health = DeviceHealthState::REVALIDATION_REQUIRED;
    }
    next.observations.push_back(make_observation("recovery", DeviceId{}, DeviceId{},
        Evidence{EvidenceClass::UNSUPPORTED, Provenance::PERSISTED, "registry",
                 "dynamic evidence requires revalidation after restart"},
        "dynamic evidence marked for revalidation"));
    if (next.observations.size() > limits_.max_observations) next.observations.pop_front();
    next.topology_gen = state_->topology_gen.next();
    for (auto& lk : next.links) lk.second.topology_generation = next.topology_gen;
    state_ = std::make_shared<const State>(std::move(next));
}

Result<DurableState> TopologyRegistry::capture_durable_state() const {
    auto s = read_state();
    DurableState d;
    d.topology_generation = s->topology_gen;
    d.saved_at = now_timestamp();
    d.devices.reserve(s->devices.size());
    d.links.reserve(s->links.size());
    for (const auto& kv : s->devices) d.devices.push_back(kv.second);
    for (const auto& kv : s->links) d.links.push_back(kv.second);
    std::sort(d.devices.begin(), d.devices.end(),
              [](const DeviceRecord& a, const DeviceRecord& b) { return a.id < b.id; });
    std::sort(d.links.begin(), d.links.end(),
              [](const LinkRecord& a, const LinkRecord& b) { return a.id < b.id; });
    return Result<DurableState>::ok(std::move(d));
}

Result<void> TopologyRegistry::restore_durable_state(const DurableState& state) {
    if (state.format_version != "nvlinkfabric-durable/1.0")
        return Result<void>::err(ErrorCode::INTEGRITY_FAILURE, "unsupported durable state version");
    std::unique_lock lock(mu_);
    State next;
    next.topology_gen = (state.topology_generation.value() > state_->topology_gen.value())
                            ? state.topology_generation
                            : state_->topology_gen;
    for (const auto& d : state.devices) {
        if (next.devices.size() >= limits_.max_devices)
            return Result<void>::err(ErrorCode::RESOURCE_LIMIT, "too many devices in durable state");
        if (!d.id || !d.generation.valid())
            return Result<void>::err(ErrorCode::INTEGRITY_FAILURE, "invalid device in durable state");
        next.devices.emplace(d.id, d);
    }
    for (const auto& l : state.links) {
        if (next.links.size() >= limits_.max_links)
            return Result<void>::err(ErrorCode::RESOURCE_LIMIT, "too many links in durable state");
        if (!l.id || !l.generation.valid() || l.source == l.target)
            return Result<void>::err(ErrorCode::INTEGRITY_FAILURE, "invalid link in durable state");
        if (next.devices.find(l.source) == next.devices.end() ||
            next.devices.find(l.target) == next.devices.end())
            return Result<void>::err(ErrorCode::INTEGRITY_FAILURE, "link references unknown device");
        LinkRecord restored = l;
        // Dynamic operational state does not recover as fresh.
        restored.state = LinkOperationalState::REVALIDATION_REQUIRED;
        restored.degraded = false;
        restored.degradation_note = "revalidation required after durable-state restore";
        next.links.emplace(restored.id, std::move(restored));
    }
    // Worker-owned devices require revalidation after restore.
    for (auto& kv : next.devices) {
        if (kv.second.owner_worker.has_value() || kv.second.owner_boot.has_value())
            kv.second.health = DeviceHealthState::REVALIDATION_REQUIRED;
    }
    next.observations.push_back(make_observation("restore", DeviceId{}, DeviceId{},
        Evidence{EvidenceClass::UNSUPPORTED, Provenance::PERSISTED, "persistence",
                 "durable state restored; dynamic evidence requires revalidation"},
        "durable state restored"));
    next.topology_gen = next.topology_gen.next();
    for (auto& lk : next.links) lk.second.topology_generation = next.topology_gen;
    state_ = std::make_shared<const State>(std::move(next));
    return Result<void>::ok();
}

}  // namespace nvlinkfabric

