#include "test_framework.hpp"
#include "nvlinkfabric/registry.hpp"
#include "nvlinkfabric/backends/synthetic_backend.hpp"

using namespace nvlinkfabric;

static void run_tests() {
    const std::uint64_t BW = 400000000000ULL;  // 400 GB/s synthetic nominal

    SyntheticTopologySpec spec;
    auto d0 = fresh_id<DeviceTag>();
    auto d1 = fresh_id<DeviceTag>();
    spec.devices.push_back(SyntheticDeviceSpec{d0, "gpu0", NodeKind::ACCELERATOR, "SYNTHETIC", BW, 1u, true});
    spec.devices.push_back(SyntheticDeviceSpec{d1, "gpu1", NodeKind::ACCELERATOR, "SYNTHETIC", BW, 1u, true});
    spec.links.push_back(SyntheticLinkSpec{d0, d1, false, LinkOperationalState::UP, BW, 8u, "NVLink 0", false, ""});

    auto backend = make_synthetic_backend(std::move(spec));
    auto devs = backend->discover_devices();
    CHECK(devs.has_value());
    CHECK(devs->size() == std::size_t{2});
    for (const auto& d : *devs) CHECK(d.evidence.is_synthetic());

    TopologyRegistry registry;
    for (const auto& d : *devs) CHECK(registry.upsert_device(d).has_value());
    auto lks = backend->discover_links(*devs);
    CHECK(lks.has_value());
    for (const auto& l : *lks) CHECK(registry.upsert_link(l).has_value());

    CHECK_EQ(registry.device_count(), std::size_t{2});
    CHECK_EQ(registry.link_count(), std::size_t{1});
    CHECK(registry.topology_generation().value() >= 1u);

    auto view = registry.consistent_view();
    CHECK_EQ(view.topology.devices.size(), std::size_t{2});
    CHECK_EQ(view.topology.links.size(), std::size_t{1});

    auto paths = registry.enumerate_paths(d0, d1);
    CHECK(paths.has_value());
    CHECK_EQ(paths->size(), std::size_t{1});
    CHECK((*paths)[0].direct);

    auto q = registry.compute_path_quality((*paths)[0]);
    CHECK(q.has_value());
    CHECK_EQ(q->hop_count, std::size_t{1});
    CHECK(q->evidence.is_synthetic());

    auto policy = RoutePolicy::default_policy();
    auto dec = registry.decide_route(d0, d1, policy);
    CHECK(dec.has_value());
    CHECK(dec->outcome == RouteDecisionOutcome::ROUTE_ALLOWED);
    CHECK(dec->selected.has_value());
    CHECK(dec->executable);
    CHECK_MSG(!dec->explanation.empty(), "explanation present");

    auto dec2 = registry.decide_route(d0, d1, policy);
    CHECK(dec2.has_value());
    CHECK(dec->selected == dec2->selected);

    CHECK(registry.is_route_executable(*dec));
    CHECK(registry.advance_topology(Evidence{EvidenceClass::REAL, Provenance::DERIVED, "test", "bump"}).has_value());
    CHECK(!registry.is_route_executable(*dec));
}

TEST_MAIN()
