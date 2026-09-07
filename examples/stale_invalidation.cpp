// Example: demonstrate stale-topology invalidation of a route decision.
#include "nvlinkfabric/registry.hpp"
#include "nvlinkfabric/backends/synthetic_backend.hpp"
#include <cstdio>

using namespace nvlinkfabric;

int main() {
    SyntheticTopologySpec spec;
    const DeviceId a(10u), b(11u);
    spec.devices = {
        {a, "gpuA", NodeKind::ACCELERATOR, "SYNTHETIC", 400000000000ULL, 1u, true},
        {b, "gpuB", NodeKind::ACCELERATOR, "SYNTHETIC", 400000000000ULL, 1u, true},
    };
    spec.links = {{a, b, false, LinkOperationalState::UP, 400000000000ULL, 8u, "NVLink 0", false, ""}};
    auto backend = make_synthetic_backend(std::move(spec));
    TopologyRegistry registry;
    auto devs = backend->discover_devices();
    auto lks = backend->discover_links(*devs);
    for (const auto& d : *devs) registry.upsert_device(d);
    for (const auto& l : *lks) registry.upsert_link(l);

    auto dec = registry.decide_route(a, b, RoutePolicy::default_policy());
    if (dec.has_value()) {
        std::printf("before topology change: outcome=%s executable=%d\n",
                    route_outcome_name(dec->outcome), dec->executable ? 1 : 0);
        std::printf("  is_executable=%d\n", registry.is_route_executable(*dec) ? 1 : 0);
    }

    // Advance the topology generation (a topology-changing event).
    registry.advance_topology(Evidence{EvidenceClass::REAL, Provenance::DERIVED, "example", "topology advanced"});

    if (dec.has_value()) {
        std::printf("after topology change: is_executable=%d (route must be revalidated/refreshed)\n",
                    registry.is_route_executable(*dec) ? 1 : 0);
        auto reval = registry.revalidate_route(*dec);
        if (reval.has_value())
            std::printf("revalidation outcome=%s\n", route_outcome_name(reval->outcome));
    }
    return 0;
}
