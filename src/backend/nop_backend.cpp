#include "nvlinkfabric/backends/nop_backend.hpp"
#include <memory>

namespace nvlinkfabric {

namespace {
class NopBackend final : public Backend {
public:
    std::string name() const override { return "nop"; }
    Result<BackendCapabilities> describe_capabilities() const override {
        BackendCapabilities c;
        c.backend_name = "nop";
        c.device_discovery = false;
        c.link_discovery = false;
        c.measurement = false;
        c.nvlink_discovery = false;
        c.measurement_support = MeasurementSupport::UNSUPPORTED;
        c.evidence = Evidence{EvidenceClass::UNSUPPORTED, Provenance::NONE,
                              "nop", "no-op backend exposes no connectivity"};
        c.note = "portability no-op backend";
        return Result<BackendCapabilities>::ok(std::move(c));
    }
    Result<std::vector<DeviceRecord>> discover_devices() override {
        return Result<std::vector<DeviceRecord>>::ok(std::vector<DeviceRecord>{});
    }
    Result<std::vector<LinkRecord>> discover_links(const std::vector<DeviceRecord>&) override {
        return Result<std::vector<LinkRecord>>::ok(std::vector<LinkRecord>{});
    }
    Result<MeasurementRecord> measure(const MeasurementRequest&) override {
        return Result<MeasurementRecord>::err(ErrorCode::UNSUPPORTED, "no-op backend has no measurement");
    }
    bool supports_measurement() const override { return false; }
};
}  // namespace

std::unique_ptr<Backend> make_nop_backend() {
    return std::make_unique<NopBackend>();
}

}  // namespace nvlinkfabric
