#pragma once
#include <cstdint>
#include <string>

namespace nvlinkfabric {

//---------------------------------------------------------------------------
// Evidence classification. Every observation carries exactly one class.
//---------------------------------------------------------------------------
enum class EvidenceClass : std::uint8_t {
    REAL,        // observed on real, supported hardware
    SYNTHETIC,   // produced by an explicit synthetic fixture
    UNSUPPORTED  // not available on present hardware/runtime/backend
};

//---------------------------------------------------------------------------
// Provenance: which source subsystem produced the evidence. Bit flags.
//---------------------------------------------------------------------------
enum class Provenance : std::uint32_t {
    NONE                = 0u,
    NVML                = 1u << 0,
    CUDA_RUNTIME        = 1u << 1,
    CUDA_DRIVER         = 1u << 2,
    OPERATING_SYSTEM    = 1u << 3,
    BENCHMARK           = 1u << 4,
    PERSISTED           = 1u << 5,
    DERIVED             = 1u << 6,
    SYNTHETIC_FIXTURE   = 1u << 7
};

inline constexpr Provenance operator|(Provenance a, Provenance b) noexcept {
    return static_cast<Provenance>(static_cast<std::uint32_t>(a) | static_cast<std::uint32_t>(b));
}
inline constexpr Provenance operator&(Provenance a, Provenance b) noexcept {
    return static_cast<Provenance>(static_cast<std::uint32_t>(a) & static_cast<std::uint32_t>(b));
}
inline constexpr Provenance operator~(Provenance a) noexcept {
    return static_cast<Provenance>(~static_cast<std::uint32_t>(a));
}

inline const char* evidence_class_name(EvidenceClass c) noexcept {
    switch (c) {
        case EvidenceClass::REAL:       return "REAL";
        case EvidenceClass::SYNTHETIC:  return "SYNTHETIC";
        case EvidenceClass::UNSUPPORTED:return "UNSUPPORTED";
    }
    return "UNKNOWN";
}

inline std::string provenance_name(Provenance p) noexcept {
    std::string out;
    auto add = [&](Provenance bit, const char* name) {
        if ((static_cast<std::uint32_t>(p) & static_cast<std::uint32_t>(bit)) != 0u) {
            if (!out.empty()) out += '|';
            out += name;
        }
    };
    add(Provenance::NVML, "NVML");
    add(Provenance::CUDA_RUNTIME, "CUDA_RUNTIME");
    add(Provenance::CUDA_DRIVER, "CUDA_DRIVER");
    add(Provenance::OPERATING_SYSTEM, "OS");
    add(Provenance::BENCHMARK, "BENCHMARK");
    add(Provenance::PERSISTED, "PERSISTED");
    add(Provenance::DERIVED, "DERIVED");
    add(Provenance::SYNTHETIC_FIXTURE, "SYNTHETIC_FIXTURE");
    return out.empty() ? "NONE" : out;
}

//---------------------------------------------------------------------------
// Interconnect technology.
//---------------------------------------------------------------------------
enum class InterconnectTechnology : std::uint8_t {
    UNKNOWN,
    NONE,    // no accelerator interconnect
    NVLINK,  // NVIDIA NVLink
    PCIE,    // PCI Express
    CXL,     // not owned by NVLink Fabric; recorded only for classification
    OTHER
};

inline const char* interconnect_technology_name(InterconnectTechnology t) noexcept {
    switch (t) {
        case InterconnectTechnology::UNKNOWN: return "UNKNOWN";
        case InterconnectTechnology::NONE:    return "NONE";
        case InterconnectTechnology::NVLINK:  return "NVLink";
        case InterconnectTechnology::PCIE:    return "PCIe";
        case InterconnectTechnology::CXL:     return "CXL";
        case InterconnectTechnology::OTHER:   return "OTHER";
    }
    return "UNKNOWN";
}

//---------------------------------------------------------------------------
// Device health / availability.
//---------------------------------------------------------------------------
enum class DeviceHealthState : std::uint8_t {
    UNKNOWN,
    PRESENT,
    ABSENT,
    DEGRADED,
    REVALIDATION_REQUIRED,
    INCOMPATIBLE,
    UNREACHABLE
};

inline const char* device_health_name(DeviceHealthState s) noexcept {
    switch (s) {
        case DeviceHealthState::UNKNOWN:                return "UNKNOWN";
        case DeviceHealthState::PRESENT:                return "PRESENT";
        case DeviceHealthState::ABSENT:                 return "ABSENT";
        case DeviceHealthState::DEGRADED:               return "DEGRADED";
        case DeviceHealthState::REVALIDATION_REQUIRED:  return "REVALIDATION_REQUIRED";
        case DeviceHealthState::INCOMPATIBLE:           return "INCOMPATIBLE";
        case DeviceHealthState::UNREACHABLE:            return "UNREACHABLE";
    }
    return "UNKNOWN";
}

//---------------------------------------------------------------------------
// Link operational state. UNKNOWN is a real, distinct state and is NEVER
// silently collapsed into UP / PRESENT / SUPPORTED.
//---------------------------------------------------------------------------
enum class LinkOperationalState : std::uint8_t {
    UNKNOWN,
    PRESENT,
    ABSENT,
    SUPPORTED,
    UNSUPPORTED,
    UP,
    DOWN,
    DEGRADED,
    REVALIDATION_REQUIRED,
    STALE,
    INCOMPATIBLE,
    UNREACHABLE
};

inline const char* link_state_name(LinkOperationalState s) noexcept {
    switch (s) {
        case LinkOperationalState::UNKNOWN:               return "UNKNOWN";
        case LinkOperationalState::PRESENT:               return "PRESENT";
        case LinkOperationalState::ABSENT:                return "ABSENT";
        case LinkOperationalState::SUPPORTED:             return "SUPPORTED";
        case LinkOperationalState::UNSUPPORTED:           return "UNSUPPORTED";
        case LinkOperationalState::UP:                    return "UP";
        case LinkOperationalState::DOWN:                  return "DOWN";
        case LinkOperationalState::DEGRADED:              return "DEGRADED";
        case LinkOperationalState::REVALIDATION_REQUIRED: return "REVALIDATION_REQUIRED";
        case LinkOperationalState::STALE:                 return "STALE";
        case LinkOperationalState::INCOMPATIBLE:          return "INCOMPATIBLE";
        case LinkOperationalState::UNREACHABLE:           return "UNREACHABLE";
    }
    return "UNKNOWN";
}

//---------------------------------------------------------------------------
// Path / contention / governance decision outcomes.
//---------------------------------------------------------------------------
enum class PathQualityOutcome : std::uint8_t {
    HEALTHY,
    DEGRADED,
    CONTENDED,
    LINK_DOWN,
    NO_PATH,
    INSUFFICIENT_EVIDENCE,
    REVALIDATION_REQUIRED,
    UNSUPPORTED
};

inline const char* path_quality_outcome_name(PathQualityOutcome o) noexcept {
    switch (o) {
        case PathQualityOutcome::HEALTHY:                return "HEALTHY";
        case PathQualityOutcome::DEGRADED:               return "DEGRADED";
        case PathQualityOutcome::CONTENDED:              return "CONTENDED";
        case PathQualityOutcome::LINK_DOWN:              return "LINK_DOWN";
        case PathQualityOutcome::NO_PATH:                return "NO_PATH";
        case PathQualityOutcome::INSUFFICIENT_EVIDENCE:  return "INSUFFICIENT_EVIDENCE";
        case PathQualityOutcome::REVALIDATION_REQUIRED:  return "REVALIDATION_REQUIRED";
        case PathQualityOutcome::UNSUPPORTED:            return "UNSUPPORTED";
    }
    return "UNKNOWN";
}

enum class RouteDecisionOutcome : std::uint8_t {
    ROUTE_ALLOWED,
    ROUTE_ALLOWED_DEGRADED,
    NO_ROUTE,
    STALE_TOPOLOGY,
    REVALIDATION_REQUIRED,
    UNSUPPORTED,
    INSUFFICIENT_EVIDENCE,
    POLICY_REJECTED,
    ENDPOINT_STALE,
    LINK_STALE
};

inline const char* route_outcome_name(RouteDecisionOutcome o) noexcept {
    switch (o) {
        case RouteDecisionOutcome::ROUTE_ALLOWED:           return "ROUTE_ALLOWED";
        case RouteDecisionOutcome::ROUTE_ALLOWED_DEGRADED:  return "ROUTE_ALLOWED_DEGRADED";
        case RouteDecisionOutcome::NO_ROUTE:                return "NO_ROUTE";
        case RouteDecisionOutcome::STALE_TOPOLOGY:          return "STALE_TOPOLOGY";
        case RouteDecisionOutcome::REVALIDATION_REQUIRED:   return "REVALIDATION_REQUIRED";
        case RouteDecisionOutcome::UNSUPPORTED:             return "UNSUPPORTED";
        case RouteDecisionOutcome::INSUFFICIENT_EVIDENCE:   return "INSUFFICIENT_EVIDENCE";
        case RouteDecisionOutcome::POLICY_REJECTED:         return "POLICY_REJECTED";
        case RouteDecisionOutcome::ENDPOINT_STALE:          return "ENDPOINT_STALE";
        case RouteDecisionOutcome::LINK_STALE:              return "LINK_STALE";
    }
    return "UNKNOWN";
}

// Named reasons a candidate path was rejected. Kept as strings for the
// explanation surface; enumerated for determinism.
enum class RouteRejectionReason : std::uint8_t {
    NONE,
    ENDPOINT_UNKNOWN,
    ENDPOINT_ABSENT,
    ENDPOINT_STALE_GENERATION,
    LINK_UNSUPPORTED,
    LINK_DOWN,
    LINK_DEGRADED_BELOW_MINIMUM,
    LINK_STALE_GENERATION,
    TOPOLOGY_STALE,
    INSUFFICIENT_EVIDENCE,
    FRESHNESS_VIOLATION,
    MIN_CAPABILITY_UNMET,
    TECHNOLOGY_EXCLUDED,
    POLICY_EXCLUDED,
    PATH_TOO_DEEP,
    NOT_DIRECT
};

inline const char* rejection_reason_name(RouteRejectionReason r) noexcept {
    switch (r) {
        case RouteRejectionReason::NONE:                            return "NONE";
        case RouteRejectionReason::ENDPOINT_UNKNOWN:                return "ENDPOINT_UNKNOWN";
        case RouteRejectionReason::ENDPOINT_ABSENT:                 return "ENDPOINT_ABSENT";
        case RouteRejectionReason::ENDPOINT_STALE_GENERATION:       return "ENDPOINT_STALE_GENERATION";
        case RouteRejectionReason::LINK_UNSUPPORTED:                return "LINK_UNSUPPORTED";
        case RouteRejectionReason::LINK_DOWN:                       return "LINK_DOWN";
        case RouteRejectionReason::LINK_DEGRADED_BELOW_MINIMUM:     return "LINK_DEGRADED_BELOW_MINIMUM";
        case RouteRejectionReason::LINK_STALE_GENERATION:           return "LINK_STALE_GENERATION";
        case RouteRejectionReason::TOPOLOGY_STALE:                  return "TOPOLOGY_STALE";
        case RouteRejectionReason::INSUFFICIENT_EVIDENCE:           return "INSUFFICIENT_EVIDENCE";
        case RouteRejectionReason::FRESHNESS_VIOLATION:             return "FRESHNESS_VIOLATION";
        case RouteRejectionReason::MIN_CAPABILITY_UNMET:            return "MIN_CAPABILITY_UNMET";
        case RouteRejectionReason::TECHNOLOGY_EXCLUDED:             return "TECHNOLOGY_EXCLUDED";
        case RouteRejectionReason::POLICY_EXCLUDED:                 return "POLICY_EXCLUDED";
        case RouteRejectionReason::PATH_TOO_DEEP:                   return "PATH_TOO_DEEP";
        case RouteRejectionReason::NOT_DIRECT:                      return "NOT_DIRECT";
    }
    return "NONE";
}

}  // namespace nvlinkfabric
