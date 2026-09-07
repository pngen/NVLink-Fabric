#pragma once
#include "nvlinkfabric/enums.hpp"
#include "nvlinkfabric/identities.hpp"
#include "nvlinkfabric/time.hpp"
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace nvlinkfabric {

//---------------------------------------------------------------------------
// RejectedCandidate: why a specific candidate path was not selected.
//---------------------------------------------------------------------------
struct RejectedCandidate {
    PathId path;
    RouteRejectionReason reason{RouteRejectionReason::NONE};
    std::string detail;
};

//---------------------------------------------------------------------------
// RouteDecision: the full, inspectable outcome of route governance.
// A decision created under topology generation N is not executable once the
// relevant topology generation changes until explicitly revalidated.
//---------------------------------------------------------------------------
struct RouteDecision {
    RouteDecisionId id;
    DeviceId source;
    DeviceId target;

    TopologyGeneration topology_generation;
    DeviceGeneration source_generation;
    DeviceGeneration target_generation;
    RouteGeneration route_generation;
    PolicyId policy;

    std::vector<PathId> candidates_considered;
    std::vector<RejectedCandidate> rejected;
    std::optional<PathId> selected;

    RouteDecisionOutcome outcome{RouteDecisionOutcome::NO_ROUTE};
    std::vector<ObservationId> evidence_references;
    std::string explanation;
    Timestamp created_at;

    // executable is true only for a ROUTE_ALLOWED/ROUTE_ALLOWED_DEGRADED
    // decision that is still current w.r.t. its required topology evidence.
    bool executable{false};
};

}  // namespace nvlinkfabric
