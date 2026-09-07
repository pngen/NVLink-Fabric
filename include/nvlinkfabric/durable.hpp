#pragma once
#include "nvlinkfabric/identities.hpp"
#include "nvlinkfabric/topology.hpp"
#include "nvlinkfabric/time.hpp"
#include <string>
#include <vector>

namespace nvlinkfabric {

//---------------------------------------------------------------------------
// DurableState: the subset of canonical state whose durability is
// architecturally meaningful. This is DURABLE HARDWARE / CAPABILITY knowledge
// plus topology structure. Dynamic current evidence (link health, live
// measurements, worker authority) is deliberately excluded: it must be
// re-established after a restart and never silently recovers as fresh.
//---------------------------------------------------------------------------
struct DurableState {
    std::string format_version{"nvlinkfabric-durable/1.0"};
    TopologyGeneration topology_generation;
    std::vector<DeviceRecord> devices;
    std::vector<LinkRecord> links;
    Timestamp saved_at;
};

}  // namespace nvlinkfabric
