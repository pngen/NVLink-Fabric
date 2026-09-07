#include "nvlinkfabric/registry.hpp"
#include "nvlinkfabric/path_quality.hpp"
#include "nvlinkfabric/policy.hpp"
#include "detail/state.hpp"
#include <algorithm>
#include <cstdint>
#include <deque>
#include <map>
#include <set>
#include <vector>

namespace nvlinkfabric {

namespace {

struct Edge { LinkId link; DeviceId to; };
using Adjacency = std::map<DeviceId, std::vector<Edge>>;

const LinkRecord* find_link(const TopologySnapshot& topo, LinkId id) {
    for (const auto& lk : topo.links) if (lk.id == id) return &lk;
    return nullptr;
}
const DeviceRecord* find_device(const TopologySnapshot& topo, DeviceId id) {
    for (const auto& d : topo.devices) if (d.id == id) return &d;
    return nullptr;
}

Adjacency build_adjacency(const TopologySnapshot& topo) {
    Adjacency adj;
    for (const auto& lk : topo.links) {
        adj[lk.source].push_back(Edge{lk.id, lk.target});
        if (!lk.directed) adj[lk.target].push_back(Edge{lk.id, lk.source});
    }
    for (auto& kv : adj) {
        std::sort(kv.second.begin(), kv.second.end(),
            [](const Edge& a, const Edge& b) {
                if (a.link.value() != b.link.value()) return a.link.value() < b.link.value();
                return a.to.value() < b.to.value();
            });
    }
    return adj;
}

std::uint64_t path_id(const DeviceId& src, const DeviceId& tgt,
                        const std::vector<PathElement>& elems) {
    std::uint64_t h = 1469598103934665603ULL;  // FNV-1a offset basis
    auto mix = [&h](std::uint64_t v) {
        for (int i = 0; i < 8; ++i) {
            h ^= (v & 0xffULL);
            h *= 1099511628211ULL;  // FNV-1a prime
            v >>= 8;
        }
    };
    mix(src.value());
    mix(tgt.value());
    for (const auto& e : elems) {
        if (e.kind == PathElement::Kind::LINK) mix(e.link.value());
    }
    return h;
}

void dfs(const DeviceId& cur, std::vector<PathElement>& elems, std::set<DeviceId>& visited,
         const Adjacency& adj, const TopologySnapshot& topo, const DeviceId& src,
         const DeviceId& target, const Limits& limits, std::vector<Path>& out) {
    if (out.size() >= limits.max_candidate_paths) return;
    auto it = adj.find(cur);
    if (it == adj.end()) return;
    for (const auto& e : it->second) {
        if (visited.count(e.to) != 0u) continue;
        if (out.size() >= limits.max_candidate_paths) return;

        PathElement le; le.kind = PathElement::Kind::LINK; le.link = e.link;
        PathElement de; de.kind = PathElement::Kind::DEVICE; de.device = e.to;
        elems.push_back(le);
        elems.push_back(de);

        if (e.to == target) {
            Path p;
            p.id = PathId(path_id(src, target, elems));
            p.source = src;
            p.target = target;
            p.elements = elems;
            p.hop_count = elems.size() / 2u;
            p.direct = (p.hop_count == 1u);
            for (const auto& el : elems) {
                if (el.kind != PathElement::Kind::LINK) continue;
                const LinkRecord* l = find_link(topo, el.link);
                if (l != nullptr && l->technology == InterconnectTechnology::NVLINK) ++p.nvlink_segments;
            }
            out.push_back(std::move(p));
            elems.pop_back();
            elems.pop_back();
            continue;
        }

        if ((elems.size() / 2u) < limits.max_path_depth) {
            visited.insert(e.to);
            dfs(e.to, elems, visited, adj, topo, src, target, limits, out);
            visited.erase(e.to);
        }
        elems.pop_back();
        elems.pop_back();
    }
}

std::vector<Path> enumerate_paths_impl(const TopologySnapshot& topo, DeviceId src,
                                       DeviceId tgt, const Limits& limits) {
    std::vector<Path> out;
    if (!src || !tgt || src == tgt) return out;
    const Adjacency adj = build_adjacency(topo);
    std::vector<PathElement> elems;
    std::set<DeviceId> visited;
    PathElement sd; sd.kind = PathElement::Kind::DEVICE; sd.device = src;
    elems.push_back(sd);
    visited.insert(src);
    dfs(src, elems, visited, adj, topo, src, tgt, limits, out);

    for (auto& p : out) {
        bool sw = false;
        for (const auto& el : p.elements) {
            if (el.kind != PathElement::Kind::DEVICE) continue;
            const DeviceRecord* rec = find_device(topo, el.device);
            if (rec != nullptr && rec->kind == NodeKind::SWITCH) sw = true;
        }
        p.switch_traversal = sw;
    }
    return out;
}

PathQuality compute_quality(const TopologySnapshot& topo,
                            const std::vector<MeasurementRecord>& measurements,
                            const Path& path) {
    PathQuality q;
    q.path = path.id;
    q.source = path.source;
    q.target = path.target;
    q.hop_count = path.hop_count;
    q.nvlink_segments = path.nvlink_segments;
    q.switch_traversal = path.switch_traversal;
    q.direct = path.direct;

    std::uint64_t nominal = 0u;
    bool have_nominal = false;
    bool any_down = false;
    bool any_unsupported = false;
    bool any_reval = false;
    bool any_degraded = false;
    bool any_up = false;
    bool any_synth = false;
    bool any_unsup_ev = false;
    bool any_unknown = false;
    bool has_capability = false;

    for (const auto& el : path.elements) {
        if (el.kind != PathElement::Kind::LINK) continue;
        const LinkRecord* l = find_link(topo, el.link);
        if (l == nullptr) { ++q.down_links; any_down = true; continue; }
        if (l->technology == InterconnectTechnology::NVLINK) ++q.nvlink_segments;
        if (l->state == LinkOperationalState::UP || l->state == LinkOperationalState::PRESENT ||
            l->state == LinkOperationalState::SUPPORTED) {
            ++q.active_links; any_up = true;
        }
        if (l->state == LinkOperationalState::UNKNOWN) any_unknown = true;
        if (l->degraded || l->state == LinkOperationalState::DEGRADED) { ++q.degraded_links; any_degraded = true; }
        if (l->state == LinkOperationalState::DOWN || l->state == LinkOperationalState::ABSENT ||
            l->state == LinkOperationalState::UNREACHABLE) { ++q.down_links; any_down = true; }
        if (l->state == LinkOperationalState::UNSUPPORTED) any_unsupported = true;
        if (l->state == LinkOperationalState::REVALIDATION_REQUIRED || l->state == LinkOperationalState::STALE) any_reval = true;
        if (l->capability.nominal_bandwidth_bps > 0u) {
            nominal = (nominal == 0u) ? l->capability.nominal_bandwidth_bps
                                      : std::min(nominal, l->capability.nominal_bandwidth_bps);
        }
        have_nominal = true;
        if (l->capability.nominal_bandwidth_bps > 0u || l->capability.interconnect != InterconnectTechnology::UNKNOWN)
            has_capability = true;
        if (l->evidence.evidence_class == EvidenceClass::SYNTHETIC) any_synth = true;
        if (l->evidence.evidence_class == EvidenceClass::UNSUPPORTED) any_unsup_ev = true;
        if (l->evidence.evidence_class == EvidenceClass::REAL) { /* keep */ }
    }
    q.nominal_bandwidth_bps = (have_nominal && nominal > 0u) ? nominal : 0u;

    const MeasurementRecord* best = nullptr;
    for (const auto& m : measurements) {
        if (!m.valid || m.stale) continue;
        if (m.source == path.source && m.target == path.target) {
            if (best == nullptr || m.measured_at.unix_ns > best->measured_at.unix_ns) best = &m;
        }
    }
    if (best != nullptr) {
        q.measured_bandwidth_bps = best->bandwidth_bps;
        q.measured_latency_seconds = best->latency_seconds;
        q.measurement_timestamp = best->measured_at;
        q.contended = best->contended;
        if (best->evidence.evidence_class == EvidenceClass::UNSUPPORTED) any_unsup_ev = true;
        if (best->evidence.evidence_class == EvidenceClass::SYNTHETIC) any_synth = true;
    }

    if (any_unsup_ev) q.evidence.evidence_class = EvidenceClass::UNSUPPORTED;
    else if (any_synth) q.evidence.evidence_class = EvidenceClass::SYNTHETIC;
    else q.evidence.evidence_class = EvidenceClass::REAL;
    q.evidence.source = "path"; 
    q.evidence.note = "aggregate of traversed link evidence";

    q.evidence_sufficient = any_up && (has_capability || best != nullptr);

    if (any_down || path.hop_count == 0u) q.outcome = PathQualityOutcome::LINK_DOWN;
    else if (any_unsupported) q.outcome = PathQualityOutcome::UNSUPPORTED;
    else if (any_reval) q.outcome = PathQualityOutcome::REVALIDATION_REQUIRED;
    else if (q.contended) q.outcome = PathQualityOutcome::CONTENDED;
    else if (any_degraded) q.outcome = PathQualityOutcome::DEGRADED;
    else if (any_unknown || !has_capability) q.outcome = PathQualityOutcome::INSUFFICIENT_EVIDENCE;
    else q.outcome = PathQualityOutcome::HEALTHY;

    q.stale = any_reval;
    if (best != nullptr && best->stale) q.stale = true;

    q.topology_generation = topo.generation;
    if (const DeviceRecord* sd = find_device(topo, path.source)) q.source_generation = sd->generation;
    if (const DeviceRecord* td = find_device(topo, path.target)) q.target_generation = td->generation;

    return q;
}

std::vector<ObservationId> collect_evidence(const std::deque<Observation>& obs,
                                            const Path& path) {
    std::deque<ObservationId> ids;
    for (const auto& el : path.elements) {
        if (el.kind != PathElement::Kind::DEVICE) continue;
        for (auto it = obs.rbegin(); it != obs.rend(); ++it) {
            if (it->source == el.device && it->kind == "device") { ids.push_back(it->id); break; }
        }
    }
    for (const auto& el : path.elements) {
        if (el.kind != PathElement::Kind::LINK) continue;
        const PathElement dummy;
        (void)dummy;
    }
    std::vector<ObservationId> out(ids.begin(), ids.end());
    return out;
}

bool is_supported_state(LinkOperationalState s) {
    return s == LinkOperationalState::UP || s == LinkOperationalState::PRESENT ||
           s == LinkOperationalState::SUPPORTED || s == LinkOperationalState::DEGRADED;
}

}  // namespace
// ---------------------------------------------------------------------------
// TopologyRegistry member implementations for path / route analysis.
// ---------------------------------------------------------------------------
Result<std::vector<Path>> TopologyRegistry::enumerate_paths(DeviceId source, DeviceId target) const {
    if (!source || !target)
        return Result<std::vector<Path>>::err(ErrorCode::INVALID_ARGUMENT, "endpoint id required");
    auto view = consistent_view();
    if (find_device(view.topology, source) == nullptr)
        return Result<std::vector<Path>>::err(ErrorCode::NOT_FOUND, "source device unknown");
    if (find_device(view.topology, target) == nullptr)
        return Result<std::vector<Path>>::err(ErrorCode::NOT_FOUND, "target device unknown");
    auto paths = enumerate_paths_impl(view.topology, source, target, limits_);
    return Result<std::vector<Path>>::ok(std::move(paths));
}

Result<PathQuality> TopologyRegistry::compute_path_quality(const Path& path) const {
    auto view = consistent_view();
    if (find_device(view.topology, path.source) == nullptr || find_device(view.topology, path.target) == nullptr)
        return Result<PathQuality>::err(ErrorCode::NOT_FOUND, "path endpoint unknown");
    return Result<PathQuality>::ok(compute_quality(view.topology, view.measurements, path));
}

namespace {
struct Eval {
    Path path;
    PathQuality quality;
    double score{0.0};
};

bool opt_bw_higher(const std::optional<double>& a, const std::optional<double>& b) {
    if (a.has_value() != b.has_value()) return a.has_value();
    if (!a.has_value()) return false;
    return *a > *b;
}
bool opt_lat_lower(const std::optional<double>& a, const std::optional<double>& b) {
    if (a.has_value() != b.has_value()) return a.has_value();
    if (!a.has_value()) return false;
    return *a < *b;
}

std::string join_rejections(const std::vector<RejectedCandidate>& rejected) {
    std::string out = "no eligible path; rejected: ";
    bool first = true;
    for (const auto& rc : rejected) {
        if (!first) out += "; ";
        first = false;
        out += std::string(rejection_reason_name(rc.reason)) + "(" + rc.path.to_string();
        if (!rc.detail.empty()) { out += ": " + rc.detail; }
        out += ")";
    }
    return out;
}
}  // namespace

Result<RouteDecision> TopologyRegistry::decide_route(DeviceId source, DeviceId target,
                                                     const RoutePolicy& policy) const {
    RouteDecision decision;
    decision.id = fresh_id<RouteDecisionTag>();
    decision.source = source;
    decision.target = target;
    decision.route_generation = fresh_generation<RouteGenerationTag>();
    decision.policy = policy.id;
    decision.created_at = now_timestamp();

    auto view = consistent_view();
    const DeviceRecord* sd = find_device(view.topology, source);
    const DeviceRecord* td = find_device(view.topology, target);
    if (sd == nullptr || td == nullptr) {
        decision.outcome = RouteDecisionOutcome::NO_ROUTE;
        decision.explanation = (sd == nullptr) ? "source endpoint not present in current topology"
                                               : "target endpoint not present in current topology";
        return Result<RouteDecision>::ok(std::move(decision));
    }
    decision.source_generation = sd->generation;
    decision.target_generation = td->generation;
    decision.topology_generation = view.topology.generation;

    auto paths = enumerate_paths_impl(view.topology, source, target, limits_);
    if (paths.empty()) {
        decision.outcome = RouteDecisionOutcome::NO_ROUTE;
        decision.explanation = "no path exists between endpoints in current topology";
        return Result<RouteDecision>::ok(std::move(decision));
    }
    for (const auto& p : paths) decision.candidates_considered.push_back(p.id);

    std::vector<Eval> evals;
    bool any_freshness = false;
    bool any_unsupported = false;
    for (const auto& p : paths) {
        PathQuality q = compute_quality(view.topology, view.measurements, p);
        bool eligible = true;
        std::string reason;
        RouteRejectionReason rr = RouteRejectionReason::NONE;

        if (policy.hard.require_supported_links) {
            for (const auto& el : p.elements) {
                if (el.kind != PathElement::Kind::LINK) continue;
                const LinkRecord* l = find_link(view.topology, el.link);
                if (l == nullptr) { eligible = false; rr = RouteRejectionReason::LINK_DOWN; reason = "link missing"; break; }
                if (!is_supported_state(l->state)) {
                    eligible = false;
                    if (l->state == LinkOperationalState::DOWN || l->state == LinkOperationalState::ABSENT ||
                        l->state == LinkOperationalState::UNREACHABLE) rr = RouteRejectionReason::LINK_DOWN;
                    else if (l->state == LinkOperationalState::UNSUPPORTED) rr = RouteRejectionReason::LINK_UNSUPPORTED;
                    else if (l->state == LinkOperationalState::STALE) rr = RouteRejectionReason::LINK_STALE_GENERATION;
                    else if (l->state == LinkOperationalState::REVALIDATION_REQUIRED) rr = RouteRejectionReason::FRESHNESS_VIOLATION;
                    else rr = RouteRejectionReason::LINK_DOWN;
                    reason = std::string(link_state_name(l->state));
                    break;
                }
            }
        }
        if (eligible && policy.hard.require_fresh) {
            if (q.outcome == PathQualityOutcome::REVALIDATION_REQUIRED) { eligible = false; rr = RouteRejectionReason::FRESHNESS_VIOLATION; reason = "revalidation required"; any_freshness = true; }
            else if (q.stale) { eligible = false; rr = RouteRejectionReason::FRESHNESS_VIOLATION; reason = "stale evidence"; any_freshness = true; }
        }
        if (eligible && policy.hard.require_direct && !p.direct) { eligible = false; rr = RouteRejectionReason::NOT_DIRECT; reason = "not direct"; }
        if (eligible && policy.hard.min_nominal_bandwidth_bps.has_value() &&
            q.nominal_bandwidth_bps < *policy.hard.min_nominal_bandwidth_bps) {
            eligible = false; rr = RouteRejectionReason::MIN_CAPABILITY_UNMET; reason = "nominal bandwidth below minimum";
        }
        if (eligible && policy.hard.min_measured_bandwidth_bps.has_value()) {
            if (!q.measured_bandwidth_bps.has_value()) { eligible = false; rr = RouteRejectionReason::INSUFFICIENT_EVIDENCE; reason = "no measured bandwidth"; }
            else if (*q.measured_bandwidth_bps < *policy.hard.min_measured_bandwidth_bps) { eligible = false; rr = RouteRejectionReason::MIN_CAPABILITY_UNMET; reason = "measured bandwidth below minimum"; }
        }
        if (eligible && policy.hard.required_technology.has_value()) {
            for (const auto& el : p.elements) {
                if (el.kind != PathElement::Kind::LINK) continue;
                const LinkRecord* l = find_link(view.topology, el.link);
                if (l == nullptr || l->technology != *policy.hard.required_technology) { eligible = false; rr = RouteRejectionReason::TECHNOLOGY_EXCLUDED; reason = "technology mismatch"; break; }
            }
        }
        if (eligible && policy.hard.max_hops.has_value() && p.hop_count > *policy.hard.max_hops) { eligible = false; rr = RouteRejectionReason::PATH_TOO_DEEP; reason = "too many hops"; }
        if (eligible && policy.hard.exclude_degraded && q.degraded_links > 0u) { eligible = false; rr = RouteRejectionReason::LINK_DEGRADED_BELOW_MINIMUM; reason = "degraded links present"; }
        if (eligible && policy.hard.exclude_down && q.down_links > 0u) { eligible = false; rr = RouteRejectionReason::LINK_DOWN; reason = "down links present"; }
        if (eligible && policy.hard.exclude_contended && q.contended) { eligible = false; rr = RouteRejectionReason::POLICY_EXCLUDED; reason = "contended"; }

        if (!eligible) {
            RejectedCandidate rc;
            rc.path = p.id;
            rc.reason = rr;
            rc.detail = reason.empty() ? "rejected by policy" : reason;
            if (q.outcome == PathQualityOutcome::UNSUPPORTED) any_unsupported = true;
            decision.rejected.push_back(std::move(rc));
        } else {
            Eval ev;
            ev.path = p;
            ev.quality = std::move(q);
            evals.push_back(std::move(ev));
        }
    }

    if (evals.empty()) {
        if (any_unsupported) decision.outcome = RouteDecisionOutcome::UNSUPPORTED;
        else if (any_freshness) decision.outcome = RouteDecisionOutcome::REVALIDATION_REQUIRED;
        else decision.outcome = RouteDecisionOutcome::POLICY_REJECTED;
        decision.explanation = join_rejections(decision.rejected);
        return Result<RouteDecision>::ok(std::move(decision));
    }

    detail::FactorContext ctx;
    ctx.max_nominal_bps = 1.0;
    ctx.max_measured_bps = 1.0;
    for (const auto& e : evals) {
        ctx.max_nominal_bps = std::max(ctx.max_nominal_bps, static_cast<double>(e.quality.nominal_bandwidth_bps));
        if (e.quality.measured_bandwidth_bps.has_value())
            ctx.max_measured_bps = std::max(ctx.max_measured_bps, *e.quality.measured_bandwidth_bps);
    }
    for (auto& e : evals) e.score = detail::score_path(policy, e.quality, ctx);

    std::stable_sort(evals.begin(), evals.end(), [](const Eval& a, const Eval& b) {
        if (a.score != b.score) return a.score > b.score;
        if (a.path.direct != b.path.direct) return a.path.direct;
        if (a.path.hop_count != b.path.hop_count) return a.path.hop_count < b.path.hop_count;
        if (a.quality.nominal_bandwidth_bps != b.quality.nominal_bandwidth_bps)
            return a.quality.nominal_bandwidth_bps > b.quality.nominal_bandwidth_bps;
        if (opt_bw_higher(a.quality.measured_bandwidth_bps, b.quality.measured_bandwidth_bps))
            return true;
        if (opt_bw_higher(b.quality.measured_bandwidth_bps, a.quality.measured_bandwidth_bps))
            return false;
        if (opt_lat_lower(a.quality.measured_latency_seconds, b.quality.measured_latency_seconds))
            return true;
        if (opt_lat_lower(b.quality.measured_latency_seconds, a.quality.measured_latency_seconds))
            return false;
        return a.path.id < b.path.id;
    });

    const Eval& best = evals.front();
    decision.selected = best.path.id;

    switch (best.quality.outcome) {
        case PathQualityOutcome::HEALTHY: decision.outcome = RouteDecisionOutcome::ROUTE_ALLOWED; break;
        case PathQualityOutcome::DEGRADED:
        case PathQualityOutcome::CONTENDED: decision.outcome = RouteDecisionOutcome::ROUTE_ALLOWED_DEGRADED; break;
        case PathQualityOutcome::UNSUPPORTED: decision.outcome = RouteDecisionOutcome::UNSUPPORTED; break;
        case PathQualityOutcome::REVALIDATION_REQUIRED: decision.outcome = RouteDecisionOutcome::REVALIDATION_REQUIRED; break;
        case PathQualityOutcome::INSUFFICIENT_EVIDENCE: decision.outcome = RouteDecisionOutcome::INSUFFICIENT_EVIDENCE; break;
        case PathQualityOutcome::LINK_DOWN: decision.outcome = RouteDecisionOutcome::NO_ROUTE; break;
        case PathQualityOutcome::NO_PATH: decision.outcome = RouteDecisionOutcome::NO_ROUTE; break;
    }
    decision.executable = (decision.outcome == RouteDecisionOutcome::ROUTE_ALLOWED ||
                           decision.outcome == RouteDecisionOutcome::ROUTE_ALLOWED_DEGRADED);

    std::string expl = "selected path " + best.path.id.to_string();
    expl += " hops=" + std::to_string(best.path.hop_count);
    expl += " directed=" + std::string(best.path.direct ? "true" : "false");
    expl += " outcome=" + std::string(path_quality_outcome_name(best.quality.outcome));
    expl += " evidence=" + std::string(evidence_class_name(best.quality.evidence.evidence_class));
    if (!decision.rejected.empty()) { expl += "; rejected=" + join_rejections(decision.rejected); }
    decision.explanation = expl;

    auto s = read_state();
    decision.evidence_references = collect_evidence(s->observations, best.path);

    return Result<RouteDecision>::ok(std::move(decision));
}

bool TopologyRegistry::is_route_executable(const RouteDecision& decision) const {
    if (!decision.executable) return false;
    auto view = consistent_view();
    if (view.topology.generation != decision.topology_generation) return false;
    const DeviceRecord* sd = find_device(view.topology, decision.source);
    const DeviceRecord* td = find_device(view.topology, decision.target);
    if (sd == nullptr || td == nullptr) return false;
    if (sd->generation != decision.source_generation) return false;
    if (td->generation != decision.target_generation) return false;
    return true;
}

Result<RouteDecision> TopologyRegistry::revalidate_route(const RouteDecision& decision) const {
    if (is_route_executable(decision)) return Result<RouteDecision>::ok(decision);
    RouteDecision nd;
    nd.id = fresh_id<RouteDecisionTag>();
    nd.source = decision.source;
    nd.target = decision.target;
    nd.route_generation = fresh_generation<RouteGenerationTag>();
    nd.policy = decision.policy;
    nd.created_at = now_timestamp();
    nd.outcome = RouteDecisionOutcome::REVALIDATION_REQUIRED;
    nd.explanation = "route decision is stale; fresh evidence or a new topology is required before routing may proceed";
    return Result<RouteDecision>::ok(std::move(nd));
}

std::string TopologyRegistry::explain_path(const Path& path) const {
    auto view = consistent_view();
    std::string out;
    out += "path " + path.id.to_string() + " from " + path.source.to_string() + " to " + path.target.to_string();
    out += " hops=" + std::to_string(path.hop_count);
    out += " direct=" + std::string(path.direct ? "true" : "false");
    for (const auto& el : path.elements) {
        if (el.kind == PathElement::Kind::DEVICE) {
            const DeviceRecord* d = find_device(view.topology, el.device);
            out += " dev:" + el.device.to_string();
            if (d != nullptr) {
                out += "(name=" + d->logical_name;
                out += " health=" + std::string(device_health_name(d->health));
                out += " ev=" + std::string(evidence_class_name(d->evidence.evidence_class));
                out += ")";
            }
        } else {
            const LinkRecord* l = find_link(view.topology, el.link);
            out += " link:" + el.link.to_string();
            if (l != nullptr) {
                out += "(state=" + std::string(link_state_name(l->state));
                out += " tech=" + std::string(interconnect_technology_name(l->technology));
                out += " ev=" + std::string(evidence_class_name(l->evidence.evidence_class));
                out += ")";
            }
        }
        out += " ";
    }
    return out;
}

std::vector<Observation> TopologyRegistry::observations_for(const RouteDecision& decision) const {
    std::vector<Observation> out;
    auto s = read_state();
    for (const auto& oid : decision.evidence_references) {
        for (auto it = s->observations.rbegin(); it != s->observations.rend(); ++it) {
            if (it->id == oid) { out.push_back(*it); break; }
        }
    }
    return out;
}

}  // namespace nvlinkfabric

