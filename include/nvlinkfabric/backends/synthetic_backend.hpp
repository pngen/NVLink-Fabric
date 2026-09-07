#pragma once
#include "nvlinkfabric/backend.hpp"
#include "nvlinkfabric/topology.hpp"
#include "nvlinkfabric/enums.hpp"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace nvlinkfabric {

//---------------------------------------------------------------------------
// SyntheticTopologySpec: a declarative, bounded fixture describing a topology.
// All records produced from a spec are marked SYNTHETIC / SYNTHETIC_FIXTURE.
//---------------------------------------------------------------------------
struct SyntheticDeviceSpec {
    DeviceId id;
    std::string name;
    NodeKind kind{NodeKind::ACCELERATOR};
    std::string generation{"SYNTHETIC"};
    std::uint64_t nominal_bandwidth_bps{0u};
    std::uint32_t link_count{0u};
    bool peer_access_supported{false};
};

struct SyntheticLinkSpec {
    DeviceId source;
    DeviceId target;
    bool directed{false};
    LinkOperationalState state{LinkOperationalState::UP};
    std::uint64_t nominal_bandwidth_bps{0u};
    std::uint32_t lane_width{0u};
    std::string physical_identifier;
    bool degraded{false};
    std::string degradation_note;
};

struct SyntheticTopologySpec {
    std::vector<SyntheticDeviceSpec> devices;
    std::vector<SyntheticLinkSpec> links;

    // If > 0, synthetic measurements report these fixed figures. Otherwise the
    // nominal bandwidth of the (first) direct link is used as a stand-in.
    double synth_bandwidth_bps{0.0};
    double synth_latency_seconds{0.0};
};

//---------------------------------------------------------------------------
// Constructs a SyntheticBackend from an explicit fixture spec. The resulting
// backend yields SYNTHETIC evidence only and is intended for semantics that
// current hardware cannot provide.
//---------------------------------------------------------------------------
std::unique_ptr<Backend> make_synthetic_backend(SyntheticTopologySpec spec);

}  // namespace nvlinkfabric
