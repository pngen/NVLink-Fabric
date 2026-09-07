#include "test_framework.hpp"
#include "nvlinkfabric/registry.hpp"
#include "nvlinkfabric/backends/synthetic_backend.hpp"
#include "nvlinkfabric/persistence.hpp"
#include <algorithm>
#include <cstdio>
#include <random>
#include <set>
#include <vector>

using namespace nvlinkfabric;

// Deterministic xorshift RNG (seeded; reproducible).
struct Rng {
    std::uint64_t s;
    explicit Rng(std::uint64_t seed) : s(seed ? seed : 0x9e3779b97f4a7c15ULL) {}
    std::uint64_t next() {
        std::uint64_t x = s;
        x ^= x >> 12; x ^= x << 25; x ^= x >> 27;
        s = x;
        return x * 0x2545F4914F6CDD1DULL;
    }
    std::uint64_t range(std::uint64_t lo, std::uint64_t hi) {  // [lo, hi]
        if (hi <= lo) return lo;
        return lo + (next() % (hi - lo + 1u));
    }
};

static const std::uint64_t kSeed = 0xC0FFEE123456u;

static void check_invariants(TopologyRegistry& reg, std::uint64_t step) {
    const auto snap = reg.snapshot();
    // Invariant: every path element must reference a real device/link.
    std::set<DeviceId> devices;
    std::set<LinkId> links;
    for (const auto& d : snap.devices) devices.insert(d.id);
    for (const auto& l : snap.links) links.insert(l.id);

    // Path queries on all device pairs.
    for (auto it = snap.devices.begin(); it != snap.devices.end(); ++it) {
        for (auto jt = snap.devices.begin(); jt != snap.devices.end(); ++jt) {
            if (it->id == jt->id) continue;
            auto paths = reg.enumerate_paths(it->id, jt->id);
            if (paths.has_value()) {
                for (const auto& p : *paths) {
                    nvltest::report(devices.count(p.source) != 0u, "path source exists", __FILE__, __LINE__);
                    nvltest::report(devices.count(p.target) != 0u, "path target exists", __FILE__, __LINE__);
                    for (const auto& el : p.elements) {
                        if (el.kind == PathElement::Kind::DEVICE)
                            nvltest::report(devices.count(el.device) != 0u, "path device element exists", __FILE__, __LINE__);
                        else
                            nvltest::report(links.count(el.link) != 0u, "path link element exists", __FILE__, __LINE__);
                    }
                    nvltest::report(p.hop_count <= reg.limits().max_path_depth, "path depth bounded", __FILE__, __LINE__);
                }
                nvltest::report(paths->size() <= reg.limits().max_candidate_paths, "candidate count bounded", __FILE__, __LINE__);
            }
        }
    }
    (void)step;
}

static void run_tests() {
    Rng rng(kSeed);

    // Build a bounded device pool.
    TopologyRegistry reg;
    Limits limits = reg.limits();
    std::vector<DeviceId> pool;
    for (std::uint64_t i = 0; i < 8u; ++i) {
        DeviceRecord d;
        d.id = fresh_id<DeviceTag>();
        d.logical_name = "gpu" + std::to_string(i);
        d.kind = NodeKind::ACCELERATOR;
        d.architecture = "SYNTHETIC";
        d.health = DeviceHealthState::PRESENT;
        d.evidence = Evidence{EvidenceClass::SYNTHETIC, Provenance::SYNTHETIC_FIXTURE, "prop", "synthetic"};
        d.generation = fresh_generation<DeviceGenerationTag>();
        d.capability.nominal_bandwidth_bps = 400000000000ULL;
        if (reg.upsert_device(d).has_value()) pool.push_back(d.id);
    }
    nvltest::report(pool.size() >= 8u, "device pool seeded", __FILE__, __LINE__);

    auto prev_topology = reg.topology_generation();
    for (std::uint64_t step = 0; step < 2000u; ++step) {
        const std::uint64_t op = rng.next() % 9u;
        switch (op) {
            case 0: {  // add a link between two random devices
                if (pool.size() >= 2u) {
                    const DeviceId a = pool[(std::size_t)rng.range(0u, (std::uint64_t)pool.size() - 1u)];
                    const DeviceId b = pool[(std::size_t)rng.range(0u, (std::uint64_t)pool.size() - 1u)];
                    if (a != b) {
                        LinkRecord l;
                        l.id = fresh_id<LinkTag>();
                        l.source = a; l.target = b;
                        l.state = static_cast<LinkOperationalState>(rng.range(1u, (std::uint64_t)LinkOperationalState::UNREACHABLE));
                        l.technology = InterconnectTechnology::NVLINK;
                        l.capability.nominal_bandwidth_bps = 400000000000ULL;
                        l.generation = fresh_generation<LinkGenerationTag>();
                        l.evidence = Evidence{EvidenceClass::SYNTHETIC, Provenance::SYNTHETIC_FIXTURE, "prop", "synthetic"};
                        reg.upsert_link(l);
                    }
                }
                break;
            }
            case 1: {  // random link state transition
                auto snap = reg.snapshot();
                if (!snap.links.empty()) {
                    const auto& l = snap.links[(std::size_t)rng.range(0u, (std::uint64_t)snap.links.size() - 1u)];
                    const auto st = static_cast<LinkOperationalState>(rng.range(0u, (std::uint64_t)LinkOperationalState::UNREACHABLE));
                    reg.update_link_state(l.id, st, Evidence{EvidenceClass::SYNTHETIC, Provenance::SYNTHETIC_FIXTURE, "prop", "state"}, rng.next() % 2u == 0u, "prop");
                }
                break;
            }
            case 2: { // advance topology
                reg.advance_topology(Evidence{EvidenceClass::REAL, Provenance::DERIVED, "prop", "advance"});
                break;
            }
            case 3: { // remove a random device
                auto snap = reg.snapshot();
                if (!snap.devices.empty()) {
                    const auto& d = snap.devices[(std::size_t)rng.range(0u, (std::uint64_t)snap.devices.size() - 1u)];
                    reg.remove_device(d.id);
                    pool.erase(std::remove(pool.begin(), pool.end(), d.id), pool.end());
                }
                break;
            }
            case 4: { // publish a measurement
                if (pool.size() >= 2u) {
                    const DeviceId a = pool[(std::size_t)rng.range(0u, (std::uint64_t)pool.size() - 1u)];
                    const DeviceId b = pool[(std::size_t)rng.range(0u, (std::uint64_t)pool.size() - 1u)];
                    MeasurementRecord m;
                    m.id = fresh_id<MeasurementTag>();
                    m.source = a; m.target = b;
                    m.payload_bytes = rng.range(1u, 16u) * 1024u * 1024u;
                    m.iterations = (std::uint32_t)rng.range(1u, 32u);
                    m.bandwidth_bps = (double)rng.range(1u, 400u) * 1e9;
                    m.latency_seconds = (double)rng.range(1u, 100u) * 1e-9;
                    m.valid = true;
                    m.evidence = Evidence{EvidenceClass::SYNTHETIC, Provenance::BENCHMARK | Provenance::SYNTHETIC_FIXTURE, "prop", "synthetic"};
                    reg.publish_measurement(m);
                }
                break;
            }
            case 6: { // decide route + determinism check
                if (pool.size() >= 2u) {
                    const DeviceId a = pool[(std::size_t)rng.range(0u, (std::uint64_t)pool.size() - 1u)];
                    const DeviceId b = pool[(std::size_t)rng.range(0u, (std::uint64_t)pool.size() - 1u)];
                    if (a != b) {
                        auto d1 = reg.decide_route(a, b, RoutePolicy::default_policy());
                        auto d2 = reg.decide_route(a, b, RoutePolicy::default_policy());
                        if (d1.has_value() && d2.has_value())
                            nvltest::report(d1->selected == d2->selected, "deterministic route selection", __FILE__, __LINE__);
                    }
                }
                break;
            }
            default: break;
        }

        // Invariants.
        const auto now = reg.topology_generation();
        nvltest::report(now.value() >= prev_topology.value(), "topology generation monotonic", __FILE__, __LINE__);
        prev_topology = now;
        check_invariants(reg, step);
    }

    // Persistence/reload under randomized mutations.
    auto durable = reg.capture_durable_state();
    CHECK(durable.has_value());
    const std::string path = "tmp/state_prop.nvfd";
    CHECK(save_durable_state(*durable, path, limits).has_value());
    auto loaded = load_durable_state(path, limits);
    CHECK(loaded.has_value());
    TopologyRegistry restored;
    if (loaded.has_value()) {
        CHECK(restored.restore_durable_state(*loaded).has_value());
        nvltest::report(restored.device_count() == reg.device_count(), "durable device count preserved", __FILE__, __LINE__);
        // Dynamic evidence must not recover fresh.
        for (const auto& l : restored.snapshot().links)
            nvltest::report(l.state == LinkOperationalState::REVALIDATION_REQUIRED, "restored links require revalidation", __FILE__, __LINE__);
    }
}

TEST_MAIN()
