#pragma once
#include "nvlinkfabric/enums.hpp"
#include "nvlinkfabric/identities.hpp"
#include "nvlinkfabric/path_quality.hpp"
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace nvlinkfabric {

//---------------------------------------------------------------------------
// Named, deterministic ranking factors. A composite route score is always a
// documented weighted combination of these named factors.
//---------------------------------------------------------------------------
enum class RouteFactorKey : std::uint8_t {
    DIRECTNESS,
    HOP_COUNT,
    MEASURED_BANDWIDTH,
    NOMINAL_BANDWIDTH,
    MEASURED_LATENCY,
    DEGRADATION,
    CONTENTION,
    FRESHNESS,
    CONFIDENCE,
    POLICY_PREFERENCE
};

inline const char* route_factor_key_name(RouteFactorKey k) noexcept {
    switch (k) {
        case RouteFactorKey::DIRECTNESS:         return "DIRECTNESS";
        case RouteFactorKey::HOP_COUNT:          return "HOP_COUNT";
        case RouteFactorKey::MEASURED_BANDWIDTH: return "MEASURED_BANDWIDTH";
        case RouteFactorKey::NOMINAL_BANDWIDTH:  return "NOMINAL_BANDWIDTH";
        case RouteFactorKey::MEASURED_LATENCY:   return "MEASURED_LATENCY";
        case RouteFactorKey::DEGRADATION:        return "DEGRADATION";
        case RouteFactorKey::CONTENTION:         return "CONTENTION";
        case RouteFactorKey::FRESHNESS:          return "FRESHNESS";
        case RouteFactorKey::CONFIDENCE:         return "CONFIDENCE";
        case RouteFactorKey::POLICY_PREFERENCE:  return "POLICY_PREFERENCE";
    }
    return "UNKNOWN";
}

struct RouteFactor {
    RouteFactorKey key{RouteFactorKey::DIRECTNESS};
    double weight{1.0};
};

//---------------------------------------------------------------------------
// Hard constraints applied before any ranking. A candidate violating any
// applied hard constraint is rejected with a named reason.
//---------------------------------------------------------------------------
struct HardConstraint {
    bool require_supported_links{true};
    bool require_current_topology{true};
    bool require_fresh{true};

    std::optional<std::uint64_t> min_nominal_bandwidth_bps;
    std::optional<double> min_measured_bandwidth_bps;
    std::optional<InterconnectTechnology> required_technology;
    std::optional<std::size_t> max_hops;
    bool require_direct{false};
    bool exclude_degraded{false};
    bool exclude_down{true};
    bool exclude_contended{false};
};

//---------------------------------------------------------------------------
// RoutePolicy: deterministic, named policy governing eligibility and ranking.
//---------------------------------------------------------------------------
struct RoutePolicy {
    PolicyId id;
    std::string name;
    HardConstraint hard;
    std::vector<RouteFactor> ranking;

    static RoutePolicy default_policy() {
        RoutePolicy p;
        p.name = "default";
        p.ranking = {
            {RouteFactorKey::DIRECTNESS, 3.0},
            {RouteFactorKey::HOP_COUNT, 2.0},
            {RouteFactorKey::MEASURED_BANDWIDTH, 4.0},
            {RouteFactorKey::MEASURED_LATENCY, 1.0},
            {RouteFactorKey::DEGRADATION, 5.0},
            {RouteFactorKey::FRESHNESS, 1.0},
            {RouteFactorKey::CONFIDENCE, 1.0}
        };
        return p;
    }
};

namespace detail {
struct FactorContext {
    double max_nominal_bps{1.0};
    double max_measured_bps{1.0};
};
double route_factor_value(RouteFactorKey key, const PathQuality& q, const FactorContext& ctx);
double score_path(const RoutePolicy& policy, const PathQuality& q, const FactorContext& ctx);
}  // namespace detail

}  // namespace nvlinkfabric
