// Example: NVIDIA backend discovery and honest unsupported-NVLink handling.
// Builds only when the CUDA backend is enabled.
#include "nvlinkfabric/registry.hpp"
#include "nvlinkfabric/backends/nvidia_backend.hpp"
#include <cstdio>

using namespace nvlinkfabric;

int main() {
    auto backend = make_nvidia_backend();
    if (!backend.has_value()) {
        std::printf("nvidia backend unavailable: %s\n", backend.error().to_string().c_str());
        return 3;
    }
    auto caps = (*backend)->describe_capabilities();
    if (caps.has_value())
        std::printf("backend=%s device_discovery=%d link_discovery=%d measurement=%d\n",
                    caps->backend_name.c_str(), caps->device_discovery ? 1 : 0,
                    caps->link_discovery ? 1 : 0, caps->measurement ? 1 : 0);

    auto devs = (*backend)->discover_devices();
    if (devs.has_value()) {
        for (const auto& d : *devs)
            std::printf("device name=%s arch=%s pci=%s evidence=%s\n",
                        d.logical_name.c_str(), d.architecture.c_str(),
                        d.pci_identity.c_str(), d.evidence.describe().c_str());
        std::printf("device_count=%zu\n", devs->size());
    }

    auto lks = (*backend)->discover_links(*devs);
    if (lks.has_value())
        std::printf("nvlink_links=%zu (empty means no NVLink adjacency on this hardware)\n", lks->size());

    // Measurement is honestly UNSUPPORTED on single without a real NVLink path.
    auto m = (*backend)->measure(MeasurementRequest{});
    if (!m.has_value())
        std::printf("measurement unsupported: %s\n", m.error().to_string().c_str());
    return 0;
}
