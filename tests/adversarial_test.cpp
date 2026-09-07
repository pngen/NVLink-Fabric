#include "test_framework.hpp"
#include "nvlinkfabric/registry.hpp"
#include "nvlinkfabric/serialization.hpp"
#include "nvlinkfabric/backends/synthetic_backend.hpp"
#include <cmath>
#include <cstdlib>
#include <limits>

using namespace nvlinkfabric;

static DeviceRecord make_device(const std::string& name, DeviceId id, const std::string& genstr) {
    DeviceRecord d;
    d.id = id;
    d.logical_name = name;
    d.kind = NodeKind::ACCELERATOR;
    d.architecture = "SYNTHETIC";
    d.health = DeviceHealthState::PRESENT;
    d.evidence = Evidence{EvidenceClass::SYNTHETIC, Provenance::SYNTHETIC_FIXTURE, "adv", "synthetic"};
    d.generation = DeviceGeneration(std::strtoull(genstr.c_str(), nullptr, 10));
    d.capability.nominal_bandwidth_bps = 400000000000ULL;
    return d;
}

static void run_tests() {
    const DeviceId a(1u), b(2u), c(3u);

    // Self-link rejected.
    {
        TopologyRegistry reg;
        CHECK(reg.upsert_device(make_device("gpuA", a, "1")).has_value());
        LinkRecord l;
        l.id = fresh_id<LinkTag>(); l.source = a; l.target = a; l.generation = fresh_generation<LinkGenerationTag>();
        l.state = LinkOperationalState::UP; l.technology = InterconnectTechnology::NVLINK;
        l.evidence = Evidence{EvidenceClass::SYNTHETIC, Provenance::SYNTHETIC_FIXTURE, "adv", "synthetic"};
        auto r = reg.upsert_link(l);
        CHECK(!r.has_value());
        CHECK(r.error().code() == ErrorCode::INVALID_TOPOLOGY);
    }

    // Link referencing an unknown device rejected.
    {
        TopologyRegistry reg;
        CHECK(reg.upsert_device(make_device("gpuA", a, "1")).has_value());
        LinkRecord l;
        l.id = fresh_id<LinkTag>(); l.source = a; l.target = c; l.generation = fresh_generation<LinkGenerationTag>();
        l.state = LinkOperationalState::UP; l.technology = InterconnectTechnology::NVLINK;
        l.evidence = Evidence{EvidenceClass::SYNTHETIC, Provenance::SYNTHETIC_FIXTURE, "adv", "synthetic"};
        CHECK(!reg.upsert_link(l).has_value());
    }

    // Generation must not move backward; conflicting same-gen content rejected.
    {
        TopologyRegistry reg;
        CHECK(reg.upsert_device(make_device("gpuA", a, "5")).has_value());
        auto older = make_device("gpuA", a, "4");
        auto r = reg.upsert_device(older);
        CHECK(!r.has_value());
        CHECK(r.error().code() == ErrorCode::STALE_GENERATION);
        auto conflict = make_device("gpuB", a, "5");  // same gen, different name
        auto r2 = reg.upsert_device(conflict);
        CHECK(!r2.has_value());
        CHECK(r2.error().code() == ErrorCode::STALE_GENERATION);
        auto newer = make_device("gpuA", a, "9");
        CHECK(reg.upsert_device(newer).has_value());
    }

    // UNKNOWN state must not silently become usable => INSUFFICIENT_EVIDENCE.
    {
        TopologyRegistry reg;
        CHECK(reg.upsert_device(make_device("gpuA", a, "1")).has_value());
        CHECK(reg.upsert_device(make_device("gpuB", b, "1")).has_value());
        LinkRecord l;
        l.id = fresh_id<LinkTag>(); l.source = a; l.target = b; l.generation = fresh_generation<LinkGenerationTag>();
        l.state = LinkOperationalState::UNKNOWN; l.technology = InterconnectTechnology::UNKNOWN;
        l.evidence = Evidence{EvidenceClass::UNSUPPORTED, Provenance::NONE, "adv", "unknown"};
        CHECK(reg.upsert_link(l).has_value());
        auto paths = reg.enumerate_paths(a, b);
        CHECK(paths.has_value());
        CHECK_EQ(paths->size(), std::size_t{1});
        auto q = reg.compute_path_quality((*paths)[0]);
        CHECK(q.has_value());
        CHECK(q->outcome == PathQualityOutcome::INSUFFICIENT_EVIDENCE);
        auto dec = reg.decide_route(a, b, RoutePolicy::default_policy());
        CHECK(dec.has_value());
        CHECK(dec->outcome != RouteDecisionOutcome::ROUTE_ALLOWED);
    }

    // Path explosion bounded: fully-connected 8-device graph stays within limits.
    {
        SyntheticTopologySpec spec;
        for (std::uint64_t i = 1u; i <= 8u; ++i) {
            spec.devices.push_back({DeviceId(i), "gpu" + std::to_string(i), NodeKind::ACCELERATOR, "SYNTHETIC", 400000000000ULL, 1u, true});
        }
        for (std::uint64_t i = 1u; i <= 8u; ++i)
            for (std::uint64_t j = i + 1u; j <= 8u; ++j)
                spec.links.push_back({DeviceId(i), DeviceId(j), false, LinkOperationalState::UP, 400000000000ULL, 8u, "L", false, ""});
        auto backend = make_synthetic_backend(std::move(spec));
        TopologyRegistry reg;
        auto devs = backend->discover_devices();
        auto lks = backend->discover_links(*devs);
        for (const auto& d : *devs) reg.upsert_device(d);
        for (const auto& l : *lks) reg.upsert_link(l);
        auto paths = reg.enumerate_paths(DeviceId(1u), DeviceId(8u));
        CHECK(paths.has_value());
        CHECK(paths->size() <= reg.limits().max_candidate_paths);
    }

    // Absurd measurement values must not crash route selection.
    {
        TopologyRegistry reg;
        CHECK(reg.upsert_device(make_device("gpuA", a, "1")).has_value());
        CHECK(reg.upsert_device(make_device("gpuB", b, "1")).has_value());
        LinkRecord l;
        l.id = fresh_id<LinkTag>(); l.source = a; l.target = b; l.generation = fresh_generation<LinkGenerationTag>();
        l.state = LinkOperationalState::UP; l.technology = InterconnectTechnology::NVLINK;
        l.evidence = Evidence{EvidenceClass::SYNTHETIC, Provenance::SYNTHETIC_FIXTURE, "adv", "synthetic"};
        CHECK(reg.upsert_link(l).has_value());
        MeasurementRecord m;
        m.id = fresh_id<MeasurementTag>(); m.source = a; m.target = b;
        m.bandwidth_bps = std::numeric_limits<double>::infinity();
        m.latency_seconds = -1.0;
        m.valid = true;
        m.evidence = Evidence{EvidenceClass::SYNTHETIC, Provenance::BENCHMARK | Provenance::SYNTHETIC_FIXTURE, "adv", "synthetic"};
        CHECK(reg.publish_measurement(m).has_value());
        auto dec = reg.decide_route(a, b, RoutePolicy::default_policy());
        CHECK(dec.has_value());   // must not crash; result is a valid decision
    }

    // Huge serialized count rejected (integer-overflow safe decoding).
    {
        // Build a valid frame then corrupt the device-count field to a huge value.
        DurableState st;
        st.devices.push_back(make_device("gpuA", a, "1"));
        auto ser = serialize_durable_state(st);
        CHECK(ser.has_value());
        // Find and corrupt the device-count: layout is magic(4) ver(4) len(4)
        // topo_gen(8) saved_at(8+1) then device count u32.
        const std::size_t dev_count_off = 4u + 4u + 4u + 8u + 9u;
        if (ser->bytes.size() > dev_count_off + 4u) {
            for (int i = 0; i < 4; ++i) ser->bytes[dev_count_off + i] = 0xFFu;
            auto bad = deserialize_durable_state(*ser);
            CHECK(!bad.has_value());
        } else {
            CHECK_MSG(false, "offset out of range");
        }
    }
}

TEST_MAIN()
