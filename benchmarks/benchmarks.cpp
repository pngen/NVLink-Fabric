// NVLink Fabric benchmarks. Measures completed, synchronized operations only.
// Reports topology size and candidate counts for context. No NVLink hardware
// bandwidth/latency is claimed from synthetic tests.
#include "nvlinkfabric/registry.hpp"
#include "nvlinkfabric/backends/synthetic_backend.hpp"
#include "nvlinkfabric/serialization.hpp"
#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

using namespace nvlinkfabric;
using Clock = std::chrono::steady_clock;

static double ms(Clock::time_point a, Clock::time_point b) {
    return std::chrono::duration<double, std::milli>(b - a).count();
}

int main() {
    // Build a moderately large synthetic topology: a chain + parallel links.
    const DeviceId n(60u);
    SyntheticTopologySpec spec;
    for (std::uint64_t i = 1u; i <= n.value(); ++i) {
        spec.devices.push_back({DeviceId(i), "gpu" + std::to_string(i), NodeKind::ACCELERATOR,
                                "SYNTHETIC", 400000000000ULL, 1u, true});
    }
    for (std::uint64_t i = 1u; i < n.value(); ++i) {
        spec.links.push_back({DeviceId(i), DeviceId(i + 1u), false, LinkOperationalState::UP,
                              400000000000ULL, 8u, "NVLink", false, ""});
        // one parallel link between each pair
        spec.links.push_back({DeviceId(i), DeviceId(i + 1u), false, LinkOperationalState::UP,
                              400000000000ULL, 8u, "NVLink-p", false, ""});
    }
    auto backend = make_synthetic_backend(std::move(spec));
    auto devs = backend->discover_devices();
    auto lks = backend->discover_links(*devs);

    std::printf("topology devices=%zu links=%zu\n", devs->size(), lks->size());

    // Registration.
    TopologyRegistry registry;
    auto t0 = Clock::now();
    for (const auto& d : *devs) registry.upsert_device(d);
    for (const auto& l : *lks) registry.upsert_link(l);
    auto t1 = Clock::now();
    std::printf("register %zu devices + %zu links: %.3f ms\n", devs->size(), lks->size(), ms(t0, t1));

    // Snapshot.
    t0 = Clock::now();
    const int snapshots = 2000;
    for (int i = 0; i < snapshots; ++i) (void)registry.snapshot();
    t1 = Clock::now();
    std::printf("snapshot x%d: %.3f ms (%.3f us/op)\n", snapshots, ms(t0, t1), ms(t0, t1) * 1000.0 / snapshots);

    // Consistent view.
    t0 = Clock::now();
    for (int i = 0; i < snapshots; ++i) (void)registry.consistent_view();
    t1 = Clock::now();
    std::printf("consistent_view x%d: %.3f ms (%.3f us/op)\n", snapshots, ms(t0, t1), ms(t0, t1) * 1000.0 / snapshots);

    // Path query.
    t0 = Clock::now();
    auto paths = registry.enumerate_paths(DeviceId(1u), DeviceId(n.value()));
    t1 = Clock::now();
    std::size_t pc = paths.has_value() ? paths->size() : 0u;
    std::printf("enumerate_paths 1..%llu returned %zu candidates: %.3f ms\n",
                (unsigned long long)n.value(), pc, ms(t0, t1));

    // Route selection. Path depth is bounded (see Limits), so a 60-node chain
    // yields 0 reachable candidates within the depth bound -- a correct bounded
    // traversal result, reported here for context.
    t0 = Clock::now();
    const int routes = 1000;
    RouteDecision last;
    for (int i = 0; i < routes; ++i) {
        auto dec = registry.decide_route(DeviceId(1u), DeviceId(n.value()), RoutePolicy::default_policy());
        if (dec.has_value()) last = *dec;
    }
    t1 = Clock::now();
    std::printf("decide_route x%d (returning %zu candidates): %.3f ms (%.3f us/op)\n",
                routes, pc, ms(t0, t1), ms(t0, t1) * 1000.0 / routes);
    (void)last;

    // Serialization / recovery.
    auto durable = registry.capture_durable_state();
    t0 = Clock::now();
    for (int i = 0; i < 2000; ++i) {
        auto ser = serialize_durable_state(*durable);
        if (ser.has_value()) (void)deserialize_durable_state(*ser);
    }
    t1 = Clock::now();
    std::printf("serialize+deserialize x2000: %.3f ms (%.3f us/op)\n", ms(t0, t1), ms(t0, t1) * 1000.0 / 2000);
    return 0;
}
