#include "nvlinkfabric/backends/synthetic_backend.hpp"
#include "nvlinkfabric/identities.hpp"
#include <algorithm>
#include <memory>
#include <unordered_map>

namespace nvlinkfabric {

namespace {

class SyntheticBackend final : public Backend {
public:
    explicit SyntheticBackend(SyntheticTopologySpec spec) : spec_(std::move(spec)) {}

    std::string name() const override { return "synthetic"; }

    Result<BackendCapabilities> describe_capabilities() const override {
        BackendCapabilities c;
        c.backend_name = "synthetic";
        c.device_discovery = true;
        c.link_discovery = true;
        c.measurement = true;
        c.nvlink_discovery = false;
        c.measurement_support = MeasurementSupport::SYNTHETIC_ONLY;
        c.evidence = Evidence{EvidenceClass::SYNTHETIC, Provenance::SYNTHETIC_FIXTURE,
                              "synthetic", "synthetic fixture backend"};
        c.note = "synthetic fixture backend; never represents real hardware";
        return Result<BackendCapabilities>::ok(std::move(c));
    }

    Result<std::vector<DeviceRecord>> discover_devices() override {
        std::vector<DeviceRecord> out;
        out.reserve(spec_.devices.size());
        for (const auto& d : spec_.devices) {
            DeviceRecord rec;
            rec.id = d.id ? d.id : fresh_id<DeviceTag>();
            rec.logical_name = d.name;
            rec.kind = d.kind;
            rec.backend = "synthetic";
            rec.backend_instance = d.name;
            rec.vendor = "synthetic";
            rec.architecture = "synthetic";
            rec.device_generation = d.generation;
            rec.capability.interconnect = InterconnectTechnology::NVLINK;
            rec.capability.nominal_bandwidth_bps = d.nominal_bandwidth_bps;
            rec.capability.link_count = d.link_count;
            rec.capability.peer_access_supported = d.peer_access_supported;
            rec.capability.measurement_support = MeasurementSupport::SYNTHETIC_ONLY;
            rec.health = DeviceHealthState::PRESENT;
            rec.evidence = Evidence{EvidenceClass::SYNTHETIC, Provenance::SYNTHETIC_FIXTURE,
                                    "synthetic", "synthetic fixture device"};
            rec.generation = fresh_generation<DeviceGenerationTag>();
            out.push_back(std::move(rec));
        }
        return Result<std::vector<DeviceRecord>>::ok(std::move(out));
    }

    Result<std::vector<LinkRecord>> discover_links(const std::vector<DeviceRecord>&) override {
        std::vector<LinkRecord> out;
        out.reserve(spec_.links.size());
        for (const auto& l : spec_.links) {
            LinkRecord rec;
            rec.id = fresh_id<LinkTag>();
            rec.source = l.source;
            rec.target = l.target;
            rec.directed = l.directed;
            rec.physical_identifier = l.physical_identifier;
            rec.state = l.state;
            rec.technology = InterconnectTechnology::NVLINK;
            rec.capability.nominal_bandwidth_bps = l.nominal_bandwidth_bps;
            rec.capability.nominal_lane_width = l.lane_width;
            rec.capability.measurement_support = MeasurementSupport::SYNTHETIC_ONLY;
            rec.generation = fresh_generation<LinkGenerationTag>();
            rec.evidence = Evidence{EvidenceClass::SYNTHETIC, Provenance::SYNTHETIC_FIXTURE,
                                    "synthetic", "synthetic fixture link"};
            rec.last_observation = now_timestamp();
            rec.measurement_supported = true;
            rec.degraded = l.degraded;
            rec.degradation_note = l.degradation_note;
            out.push_back(std::move(rec));
        }
        return Result<std::vector<LinkRecord>>::ok(std::move(out));
    }

    Result<MeasurementRecord> measure(const MeasurementRequest& req) override {
        MeasurementRecord m;
        m.id = fresh_id<MeasurementTag>();
        m.source = req.source;
        m.target = req.target;
        m.payload_bytes = req.payload_bytes;
        m.iterations = req.iterations;
        m.warmup_iterations = req.warmup_iterations;
        m.completed_transfers = req.iterations;
        m.warmup_separated = req.warmup_iterations > 0u;
        m.integrity_verified = req.verify_integrity;
        m.method = "synthetic";
        m.bandwidth_bps = (spec_.synth_bandwidth_bps > 0.0) ? spec_.synth_bandwidth_bps
                                                            : default_bandwidth(req);
        m.latency_seconds = spec_.synth_latency_seconds;
        m.bytes_verified = req.payload_bytes * static_cast<std::uint64_t>(req.iterations);
        m.evidence = Evidence{EvidenceClass::SYNTHETIC, Provenance::SYNTHETIC_FIXTURE,
                              "synthetic", "synthetic measurement; not evidence of real NVLink"};
        m.measured_at = now_timestamp();
        m.valid = true;
        return Result<MeasurementRecord>::ok(std::move(m));
    }

    bool supports_measurement() const override { return true; }

private:
    double default_bandwidth(const MeasurementRequest& req) const {
        for (const auto& l : spec_.links) {
            if (l.nominal_bandwidth_bps > 0u) {
                const bool fwd = (l.source == req.source && l.target == req.target);
                const bool rev = (!l.directed && l.source == req.target && l.target == req.source);
                if (fwd || rev) return static_cast<double>(l.nominal_bandwidth_bps);
            }
        }
        return 100.0e9;
    }
    SyntheticTopologySpec spec_;
};

}  // namespace

std::unique_ptr<Backend> make_synthetic_backend(SyntheticTopologySpec spec) {
    return std::make_unique<SyntheticBackend>(std::move(spec));
}

}  // namespace nvlinkfabric
