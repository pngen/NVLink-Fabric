#pragma once
#include "nvlinkfabric/enums.hpp"
#include "nvlinkfabric/identities.hpp"
#include "nvlinkfabric/evidence.hpp"
#include "nvlinkfabric/capability.hpp"
#include "nvlinkfabric/time.hpp"
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace nvlinkfabric {

//---------------------------------------------------------------------------
// Node kind: endpoints (accelerators) versus intermediary fabric elements.
//---------------------------------------------------------------------------
enum class NodeKind : std::uint8_t {
    ACCELERATOR,
    SWITCH,     // NVSwitch-class intermediary
    HOST,
    UNKNOWN
};

inline const char* node_kind_name(NodeKind k) noexcept {
    switch (k) {
        case NodeKind::ACCELERATOR: return "ACCELERATOR";
        case NodeKind::SWITCH:      return "SWITCH";
        case NodeKind::HOST:        return "HOST";
        case NodeKind::UNKNOWN:     return "UNKNOWN";
    }
    return "UNKNOWN";
}

//---------------------------------------------------------------------------
// DeviceRecord: stable logical identity plus static/dynamic knowledge.
// A CUDA ordinal is only ever process-local evidence of an ordinal; it is
// never used as a fabric identity (DeviceId is the durable identity).
//---------------------------------------------------------------------------
struct DeviceRecord {
    DeviceId id;
    std::string logical_name;
    NodeKind kind{NodeKind::UNKNOWN};

    std::string backend;             // backend subsystem that produced this record
    std::string backend_instance;    // e.g. "nvml:0"
    std::string pci_identity;        // bus:device.function (stable where available)
    std::optional<unsigned> cuda_ordinal;  // process-local only

    std::string vendor;
    std::string architecture;        // e.g. "Blackwell"
    std::string device_generation;   // e.g. "GB202"

    Capability capability;
    DeviceHealthState health{DeviceHealthState::UNKNOWN};
    Evidence evidence;

    DeviceGeneration generation;
    std::optional<WorkerId> owner_worker;
    std::optional<WorkerBootId> owner_boot;

    bool is_valid_edge_endpoint() const noexcept {
        return kind == NodeKind::ACCELERATOR || kind == NodeKind::HOST;
    }
};

//---------------------------------------------------------------------------
// LinkRecord: an oriented edge between two devices. Directionality is
// explicit; an undirected link is NOT assumed bidirectional unless the
// backend contract guarantees it.
//---------------------------------------------------------------------------
struct LinkRecord {
    LinkId id;
    DeviceId source;
    DeviceId target;
    bool directed{false};  // true: only source->target is valid

    std::string physical_identifier;  // e.g. "NVLink 0"
    LinkOperationalState state{LinkOperationalState::UNKNOWN};
    InterconnectTechnology technology{InterconnectTechnology::UNKNOWN};
    Capability capability;

    LinkGeneration generation;
    Evidence evidence;
    Timestamp last_observation;
    bool measurement_supported{false};
    bool degraded{false};
    std::string degradation_note;

    TopologyGeneration topology_generation;
};

//---------------------------------------------------------------------------
// PathElement / Path: a walk through devices (nodes) and links (edges).
//---------------------------------------------------------------------------
struct PathElement {
    enum class Kind : std::uint8_t { DEVICE, LINK };
    Kind kind{Kind::DEVICE};
    DeviceId device;
    LinkId link;
};

struct Path {
    PathId id;
    DeviceId source;
    DeviceId target;
    bool direct{false};
    bool switch_traversal{false};
    std::vector<PathElement> elements;
    std::size_t hop_count{0u};          // number of link segments
    std::size_t nvlink_segments{0u};

    std::string to_string() const {
        std::string out = "path(";
        bool first = true;
        for (const auto& e : elements) {
            if (!first) { out += " ->"; }
            first = false;
            if (e.kind == PathElement::Kind::DEVICE) {
                out += " dev:";
                out += e.device.to_string();
            } else {
                out += " link:";
                out += e.link.to_string();
            }
        }
        out += ")";
        return out;
    }
};

//---------------------------------------------------------------------------
// TopologySnapshot: an immutable, self-consistent view of the canonical
// topology at a specific topology generation.
//---------------------------------------------------------------------------
struct TopologySnapshot {
    TopologyGeneration generation;
    TopologySnapshotId snapshot_id;
    std::vector<DeviceRecord> devices;
    std::vector<LinkRecord> links;
    Timestamp created_at;
};

}  // namespace nvlinkfabric
