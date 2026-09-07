#pragma once
#include "nvlinkfabric/identities.hpp"
#include "nvlinkfabric/enums.hpp"
#include "nvlinkfabric/evidence.hpp"
#include "nvlinkfabric/time.hpp"
#include <cstdint>
#include <optional>
#include <string>

namespace nvlinkfabric {

//---------------------------------------------------------------------------
// PathQuality: explicit, factorized evidence about a path. A composite score
// is never hidden; all underlying named factors are exposed.
//---------------------------------------------------------------------------
struct PathQuality {
    PathId path;
    DeviceId source;
    DeviceId target;

    std::size_t hop_count{0u};
    std::size_t nvlink_segments{0u};
    bool switch_traversal{false};
    bool direct{false};

    std::uint64_t nominal_bandwidth_bps{0u};
    std::optional<double> measured_bandwidth_bps;   // nullopt if unmeasured
    std::optional<double> measured_latency_seconds; // nullopt if unmeasured
    Timestamp measurement_timestamp;

    std::uint32_t active_links{0u};
    std::uint32_t degraded_links{0u};
    std::uint32_t down_links{0u};

    bool evidence_sufficient{false};
    bool stale{false};
    bool contended{false};

    PathQualityOutcome outcome{PathQualityOutcome::INSUFFICIENT_EVIDENCE};
    Evidence evidence;

    TopologyGeneration topology_generation;
    DeviceGeneration source_generation;
    DeviceGeneration target_generation;

    std::string explanation;
};

}  // namespace nvlinkfabric
