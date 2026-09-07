#pragma once
#include "nvlinkfabric/enums.hpp"
#include "nvlinkfabric/identities.hpp"
#include <cstdint>
#include <string>

namespace nvlinkfabric {

//---------------------------------------------------------------------------
// Measurement support: what a backend/hardware can actually measure.
//---------------------------------------------------------------------------
enum class MeasurementSupport : std::uint8_t {
    UNKNOWN,
    UNSUPPORTED,     // no real measurement possible
    SYNTHETIC_ONLY,  // only synthetic fixtures
    REAL             // real, supported measurement
};

inline const char* measurement_support_name(MeasurementSupport s) noexcept {
    switch (s) {
        case MeasurementSupport::UNKNOWN:        return "UNKNOWN";
        case MeasurementSupport::UNSUPPORTED:    return "UNSUPPORTED";
        case MeasurementSupport::SYNTHETIC_ONLY: return "SYNTHETIC_ONLY";
        case MeasurementSupport::REAL:           return "REAL";
    }
    return "UNKNOWN";
}

//---------------------------------------------------------------------------
// Capability: static-ish hardware/interconnect knowledge. Separated from
// dynamic operational state (which lives on the link/device records).
//---------------------------------------------------------------------------
struct Capability {
    InterconnectTechnology interconnect{InterconnectTechnology::UNKNOWN};
    std::uint32_t link_count{0u};
    std::uint32_t nominal_lane_width{0u};
    std::uint64_t nominal_bandwidth_bps{0u};
    bool peer_access_supported{false};
    MeasurementSupport measurement_support{MeasurementSupport::UNKNOWN};

    // Supported telemetry/counter bitsets (0 = none known).
    std::uint32_t supported_counters{0u};
    bool telemetry_supported{false};

    bool equivalent(const Capability& o) const noexcept {
        return interconnect == o.interconnect &&
               link_count == o.link_count &&
               nominal_lane_width == o.nominal_lane_width &&
               nominal_bandwidth_bps == o.nominal_bandwidth_bps &&
               peer_access_supported == o.peer_access_supported &&
               measurement_support == o.measurement_support &&
               supported_counters == o.supported_counters &&
               telemetry_supported == o.telemetry_supported;
    }
};

}  // namespace nvlinkfabric
