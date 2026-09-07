// Example: construct a synthetic topology, register it, enumerate paths, and
// select a deterministic route. All evidence is SYNTHETIC.
#include "nvlinkfabric/registry.hpp"
#include "nvlinkfabric/backends/synthetic_backend.hpp"
#include <cstdio>

using namespace nvlinkfabric;

int main() {
    SyntheticTopologySpec spec;
    const DeviceId g0(1u), g1(2u), g2(3u), sw(4u);
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
    TopologyRegistry registry;
    auto devs = backend->discover_devices();
    auto lks = backend->discover_links(*devs);
    for (const auto& d : *devs) registry.upsert_device(d);
    for (const auto& l : *lks) registry.upsert_link(l);

    std::printf("devices=%zu links=%zu topology_generation=%llu\n",
                registry.device_count(), registry.link_count(),
                (unsigned long long)registry.topology_generation().value());

    auto paths = registry.enumerate_paths(g0, g2);
    if (paths.has_value()) {
        for (const auto& p : *paths)
            std::printf("path %s\n", registry.explain_path(p).c_str());
    }

    auto dec = registry.decide_route(g0, g2, RoutePolicy::default_policy());
    if (dec.has_value()) {
        std::printf("route outcome=%s executable=%d\n", route_outcome_name(dec->outcome), dec->executable ? 1 : 0);
        std::printf("explanation: %s\n", dec->explanation.c_str());
    }
    return 0;
}
