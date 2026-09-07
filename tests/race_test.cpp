#include "test_framework.hpp"
#include "nvlinkfabric/registry.hpp"
#include "nvlinkfabric/backends/synthetic_backend.hpp"
#include <atomic>
#include <set>
#include <thread>
#include <vector>

using namespace nvlinkfabric;

static void run_tests() {
    // Build a modest synthetic topology.
    SyntheticTopologySpec spec;
    const DeviceId g0(100u), g1(101u), g2(102u), sw(200u);
    spec.devices = {
        {g0, "gpu0", NodeKind::ACCELERATOR, "SYNTHETIC", 400000000000ULL, 1u, true},
        {g1, "gpu1", NodeKind::ACCELERATOR, "SYNTHETIC", 400000000000ULL, 1u, true},
        {g2, "gpu2", NodeKind::ACCELERATOR, "SYNTHETIC", 400000000000ULL, 1u, true},
        {sw, "switch0", NodeKind::SWITCH, "SYNTHETIC", 800000000000ULL, 2u, true},
    };
    spec.links = {
        {g0, g1, false, LinkOperationalState::UP, 400000000000ULL, 8u, "NVLink 0", false, ""},
        {g0, sw, false, LinkOperationalState::UP, 400000000000ULL, 8u, "NVLink 1", false, ""},
        {sw, g2, false, LinkOperationalState::UP, 400000000000ULL, 8u, "NVLink 2", false, ""},
        {g1, g2, false, LinkOperationalState::DEGRADED, 200000000000ULL, 8u, "NVLink 3", true, "degraded"},
    };
    auto backend = make_synthetic_backend(std::move(spec));
    Limits lim;
    lim.max_observations = 128u;       // keep copy-on-write copies cheap
    lim.max_measurement_history = 128u;
    TopologyRegistry reg(lim);
    auto devs = backend->discover_devices();
    auto lks = backend->discover_links(*devs);
    for (const auto& d : *devs) reg.upsert_device(d);
    for (const auto& l : *lks) reg.upsert_link(l);

    std::atomic<bool> stop{false};
    std::atomic<int> errors{0};

    // Reader thread: enumerate paths + decide routes concurrently.
    std::thread reader([&]() {
        for (int i = 0; i < 5000 && !stop.load(); ++i) {
            auto paths = reg.enumerate_paths(g0, g2);
            if (paths.has_value()) {
                if (paths->size() > reg.limits().max_candidate_paths) ++errors;
            }
            // A fresh decision is executable at the instant it was created.
            // (We deliberately do NOT re-check is_route_executable here: the
            // mutator may legitimately invalidate it concurrently -- stale
            // decisions must fail closed, which is correct behavior.) 
            auto dec = reg.decide_route(g0, g2, RoutePolicy::default_policy());
            (void)dec;
        }
    });

    // Mutator thread: toggle link states and advance topology concurrently.
    std::thread mutator([&]() {
        auto snap = reg.snapshot();
        LinkId lid;
        if (!snap.links.empty()) lid = snap.links[0].id;
        for (int i = 0; i < 5000 && !stop.load(); ++i) {
            if (lid) {
                const bool up = (i % 2 == 0);
                const auto st = up ? LinkOperationalState::UP : LinkOperationalState::DEGRADED;
                reg.update_link_state(lid, st, Evidence{EvidenceClass::SYNTHETIC, Provenance::SYNTHETIC_FIXTURE, "race", "toggle"}, !up, "race");
            }
            if (i % 5 == 0) reg.advance_topology(Evidence{EvidenceClass::REAL, Provenance::DERIVED, "race", "advance"});
        }
    });

    // Measurement publisher thread.
    std::thread measurer([&]() {
        for (int i = 0; i < 5000 && !stop.load(); ++i) {
            MeasurementRecord m;
            m.id = fresh_id<MeasurementTag>();
            m.source = g0; m.target = g2;
            m.payload_bytes = 4u*1024u*1024u; m.iterations = 8u;
            m.bandwidth_bps = 250.0e9; m.latency_seconds = 9.0e-6; m.valid = true;
            m.evidence = Evidence{EvidenceClass::SYNTHETIC, Provenance::BENCHMARK | Provenance::SYNTHETIC_FIXTURE, "race", "synthetic"};
            reg.publish_measurement(m);
        }
    });

    reader.join();
    mutator.join();
    measurer.join();

    // Invariants after the race: topology generation still monotonic-reasonable,
    // and the registry remains internally consistent (no dangling references).
    const auto snap = reg.snapshot();
    std::set<DeviceId> devices;
    for (const auto& d : snap.devices) devices.insert(d.id);
    bool consistent = true;
    for (const auto& l : snap.links) {
        if (devices.count(l.source) == 0u || devices.count(l.target) == 0u) consistent = false;
        if (l.source == l.target) consistent = false;
    }
    CHECK(consistent);
    CHECK_EQ(errors.load(), 0);
    CHECK(reg.topology_generation().value() >= 1u);
}

TEST_MAIN()
