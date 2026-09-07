#pragma once
#include "nvlinkfabric/identities.hpp"
#include "nvlinkfabric/evidence.hpp"
#include "nvlinkfabric/time.hpp"
#include <cstdint>
#include <optional>
#include <string>

namespace nvlinkfabric {

//---------------------------------------------------------------------------
// MeasurementRecord: a completed measurement with explicit provenance.
// Completed-work semantics: transfers are complete and synchronized before a
// bandwidth/latency figure is recorded; latency measures a completed op.
//---------------------------------------------------------------------------
struct MeasurementRecord {
    MeasurementId id;
    DeviceId source;
    DeviceId target;

    std::uint64_t payload_bytes{0u};
    std::uint32_t iterations{0u};
    std::uint32_t warmup_iterations{0u};
    std::uint64_t completed_transfers{0u};
    std::uint64_t bytes_verified{0u};

    double bandwidth_bps{0.0};
    double latency_seconds{0.0};

    bool warmup_separated{true};
    bool integrity_verified{false};
    bool contended{false};

    std::string method;   // e.g. "peer-to-peer", "host-staged", "synthetic"
    std::string path_note;

    Evidence evidence;
    Timestamp measured_at;
    bool valid{false};
    bool stale{false};           // set when authority lapses (e.g. worker death)
    bool revalidation_required{false};

    std::optional<WorkerId> owner_worker;
    std::optional<WorkerBootId> owner_boot;
};

}  // namespace nvlinkfabric
