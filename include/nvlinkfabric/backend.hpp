#pragma once
#include "nvlinkfabric/enums.hpp"
#include "nvlinkfabric/identities.hpp"
#include "nvlinkfabric/evidence.hpp"
#include "nvlinkfabric/topology.hpp"
#include "nvlinkfabric/capability.hpp"
#include "nvlinkfabric/measurement.hpp"
#include "nvlinkfabric/result.hpp"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace nvlinkfabric {

//---------------------------------------------------------------------------
// BackendCapabilities: explicitly states what telemetry/measurement a backend
// actually exposes. Absence of a capability is never silently assumed.
//---------------------------------------------------------------------------
struct BackendCapabilities {
    std::string backend_name;
    bool device_discovery{false};
    bool link_discovery{false};
    bool measurement{false};
    bool nvlink_discovery{false};      // can this backend discover NVLink links?
    MeasurementSupport measurement_support{MeasurementSupport::UNKNOWN};
    Evidence evidence;                 // which evidence class this backend yields
    std::string note;
};

//---------------------------------------------------------------------------
// MeasurementRequest: a bounded request for a real (or explicitly synthetic)
// measurement of a source->target transfer.
//---------------------------------------------------------------------------
struct MeasurementRequest {
    DeviceId source;
    DeviceId target;
    std::uint64_t payload_bytes{1u << 22u};   // default 4 MiB
    std::uint32_t iterations{8u};
    std::uint32_t warmup_iterations{2u};
    bool verify_integrity{true};
};

//---------------------------------------------------------------------------
// Backend: vendor-neutral interface for a connectivity backend. Implementations
// return typed results carrying an explicit Evidence class. A backend never
// fabricates REAL evidence; unsupported features are reported UNSUPPORTED.
//---------------------------------------------------------------------------
class Backend {
public:
    virtual ~Backend() = default;
    virtual std::string name() const = 0;
    virtual Result<BackendCapabilities> describe_capabilities() const = 0;
    virtual Result<std::vector<DeviceRecord>> discover_devices() = 0;
    virtual Result<std::vector<LinkRecord>> discover_links(
        const std::vector<DeviceRecord>& devices) = 0;
    virtual Result<MeasurementRecord> measure(const MeasurementRequest& request) = 0;
    virtual bool supports_measurement() const = 0;
};

}  // namespace nvlinkfabric
