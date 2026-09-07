#pragma once
#include "nvlinkfabric/registry.hpp"
#include <deque>
#include <unordered_map>

namespace nvlinkfabric {

// Internal immutable state snapshot. This struct is private to the library
// implementation and is never installed as part of the public headers.
struct TopologyRegistry::State {
    TopologyGeneration topology_gen;
    std::unordered_map<DeviceId, DeviceRecord> devices;
    std::unordered_map<LinkId, LinkRecord> links;
    std::deque<MeasurementRecord> measurement_history;
    std::deque<Observation> observations;
};

}  // namespace nvlinkfabric
