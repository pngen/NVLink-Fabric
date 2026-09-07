#include "nvlinkfabric/policy.hpp"
#include "nvlinkfabric/path_quality.hpp"
#include <algorithm>
#include <cmath>

namespace nvlinkfabric {
namespace detail {

namespace {
double clamp01(double v) {
    if (v < 0.0) return 0.0;
    if (v > 1.0) return 1.0;
    return v;
}
double confidence_value(const Evidence& ev) {
    switch (ev.evidence_class) {
        case EvidenceClass::REAL:       return 1.0;
        case EvidenceClass::SYNTHETIC:  return 0.5;
        case EvidenceClass::UNSUPPORTED:return 0.0;
    }
    return 0.0;
}
}  // namespace

double route_factor_value(RouteFactorKey key, const PathQuality& q, const FactorContext& ctx) {
    switch (key) {
        case RouteFactorKey::DIRECTNESS:
            return q.direct ? 1.0 : 0.0;
        case RouteFactorKey::HOP_COUNT:
            return 1.0 / static_cast<double>(q.hop_count + 1u);
        case RouteFactorKey::MEASURED_BANDWIDTH: {
            if (!q.measured_bandwidth_bps.has_value() || ctx.max_measured_bps <= 0.0) return 0.0;
            return clamp01(*q.measured_bandwidth_bps / ctx.max_measured_bps);
        }
        case RouteFactorKey::NOMINAL_BANDWIDTH: {
            if (ctx.max_nominal_bps <= 0.0) return 0.0;
            return clamp01(static_cast<double>(q.nominal_bandwidth_bps) / ctx.max_nominal_bps);
        }
        case RouteFactorKey::MEASURED_LATENCY: {
            if (!q.measured_latency_seconds.has_value()) return 0.0;
            return 1.0 / (1.0 + *q.measured_latency_seconds);
        }
        case RouteFactorKey::DEGRADATION:
            return q.degraded_links == 0u ? 1.0 : 0.0;
        case RouteFactorKey::CONTENTION:
            return q.outcome == PathQualityOutcome::CONTENDED ? 0.0 : 1.0;
        case RouteFactorKey::FRESHNESS:
            return q.stale ? 0.0 : 1.0;
        case RouteFactorKey::CONFIDENCE:
            return confidence_value(q.evidence);
        case RouteFactorKey::POLICY_PREFERENCE:
            return 0.5;
    }
    return 0.0;
}

double score_path(const RoutePolicy& policy, const PathQuality& q, const FactorContext& ctx) {
    double score = 0.0;
    for (const auto& factor : policy.ranking) {
        score += factor.weight * route_factor_value(factor.key, q, ctx);
    }
    return score;
}

}  // namespace detail
}  // namespace nvlinkfabric
