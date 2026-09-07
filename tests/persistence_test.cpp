#include "test_framework.hpp"
#include "nvlinkfabric/registry.hpp"
#include "nvlinkfabric/serialization.hpp"
#include "nvlinkfabric/persistence.hpp"
#include "nvlinkfabric/backends/synthetic_backend.hpp"
#include <filesystem>

using namespace nvlinkfabric;

static void run_tests() {
    std::filesystem::create_directories("tmp");
    const std::uint64_t BW = 400000000000ULL;
    SyntheticTopologySpec spec;
    auto d0 = fresh_id<DeviceTag>();
    auto d1 = fresh_id<DeviceTag>();
    spec.devices.push_back(SyntheticDeviceSpec{d0, "gpu0", NodeKind::ACCELERATOR, "SYNTHETIC", BW, 1u, true});
    spec.devices.push_back(SyntheticDeviceSpec{d1, "gpu1", NodeKind::ACCELERATOR, "SYNTHETIC", BW, 1u, true});
    spec.links.push_back(SyntheticLinkSpec{d0, d1, false, LinkOperationalState::UP, BW, 8u, "NVLink 0", false, ""});

    TopologyRegistry registry;
    auto backend = make_synthetic_backend(std::move(spec));
    auto devs = backend->discover_devices();
    auto lks = backend->discover_links(*devs);
    for (const auto& d : *devs) registry.upsert_device(d);
    for (const auto& l : *lks) registry.upsert_link(l);
    // Mark one device as worker-owned to exercise recovery semantics.
    auto d0rec = registry.device(d0);
    CHECK(d0rec.has_value());
    d0rec->owner_worker = WorkerId(7u);
    d0rec->owner_boot = WorkerBootId(11u);
    d0rec->generation = d0rec->generation.next();   // content changed -> new generation
    CHECK(registry.upsert_device(*d0rec).has_value());

    auto durable = registry.capture_durable_state();
    CHECK(durable.has_value());
    CHECK_EQ(durable->devices.size(), std::size_t{2});

    // Round trip serialize/deserialize.
    auto ser = serialize_durable_state(*durable);
    CHECK(ser.has_value());
    auto deser = deserialize_durable_state(*ser);
    CHECK(deser.has_value());
    CHECK_EQ(deser->devices.size(), std::size_t{2});
    CHECK_EQ(deser->links.size(), std::size_t{1});

    // Save to file, load, restore into a fresh registry.
    const std::string path = "tmp/state_nvf.nvfd";
    CHECK(save_durable_state(*durable, path, Limits{}).has_value());
    auto loaded = load_durable_state(path, Limits{});
    CHECK(loaded.has_value());

    if (loaded.has_value()) {
        TopologyRegistry restored;
        CHECK(restored.restore_durable_state(*loaded).has_value());
        CHECK_EQ(restored.device_count(), std::size_t{2});
        CHECK_EQ(restored.link_count(), std::size_t{1});
        // Dynamic link state does NOT recover fresh.
        auto lk = restored.link((*lks)[0].id);
        CHECK(lk.has_value());
        CHECK(lk->state == LinkOperationalState::REVALIDATION_REQUIRED);
        // Worker-owned device requires revalidation.
        auto rd = restored.device(d0);
        CHECK(rd.has_value());
        CHECK(rd->health == DeviceHealthState::REVALIDATION_REQUIRED);
    }

    // Corrupt the serialized bytes => integrity failure.
    auto corrupt = serialize_durable_state(*durable);
    corrupt->bytes[10] ^= 0xFFu;
    auto bad = deserialize_durable_state(*corrupt);
    CHECK(!bad.has_value());
    CHECK(bad.error().code() == ErrorCode::INTEGRITY_FAILURE);

    // Truncated => integrity failure.
    auto trunc = serialize_durable_state(*durable);
    trunc->bytes.resize(trunc->bytes.size() / 2u);
    auto bad2 = deserialize_durable_state(*trunc);
    CHECK(!bad2.has_value());

    // Trailing garbage => integrity failure.
    auto trail = serialize_durable_state(*durable);
    trail->bytes.push_back(0x00u);
    auto bad3 = deserialize_durable_state(*trail);
    CHECK(!bad3.has_value());
}

TEST_MAIN()
