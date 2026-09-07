// nvlink_fabric_inspect: inspect an NVLink Fabric topology, evidence, paths,
// and route decisions. Output always makes REAL / SYNTHETIC / UNSUPPORTED
// explicit, and optionally emits compact JSON with --json.
#include "nvlinkfabric/registry.hpp"
#include "nvlinkfabric/route.hpp"
#include "nvlinkfabric/policy.hpp"
#include "nvlinkfabric/backends/synthetic_backend.hpp"
#include "nvlinkfabric/backends/nop_backend.hpp"
#ifdef NVF_WITH_NVIDIA
#include "nvlinkfabric/backends/nvidia_backend.hpp"
#endif
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace nvlinkfabric;

namespace {

std::string jesc(const std::string& s) {
    std::string out;
    for (char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned>(c));
                    out += buf;
                } else out += c;
        }
    }
    return out;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::printf("usage: nvlink_fabric_inspect <cmd> [SRC TGT] [--backend synthetic|nvidia|nop] [--json]\n");
        std::printf("cmds: devices links topology paths SRC TGT route SRC TGT status\n");
        return 2;
    }
    std::string cmd = argv[1];
    std::string backend = "synthetic";
    bool json = false;
    std::vector<std::string> args;
    for (int i = 2; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--backend") { if (i + 1 < argc) backend = argv[++i]; }
        else if (a == "--json") json = true;
        else args.push_back(a);
    }

    TopologyRegistry registry;
    std::string backend_note = backend;

    if (backend == "synthetic") {
        SyntheticTopologySpec spec;
        const DeviceId d0(1000u), d1(1001u), d2(1002u), sw(2000u);
        spec.devices = {
            {d0, "gpu0", NodeKind::ACCELERATOR, "SYNTHETIC", 400000000000ULL, 1u, true},
            {d1, "gpu1", NodeKind::ACCELERATOR, "SYNTHETIC", 400000000000ULL, 1u, true},
            {d2, "gpu2", NodeKind::ACCELERATOR, "SYNTHETIC", 400000000000ULL, 1u, true},
            {sw, "switch0", NodeKind::SWITCH, "SYNTHETIC", 800000000000ULL, 2u, true},
        };
        spec.links = {
            {d0, d1, false, LinkOperationalState::UP, 400000000000ULL, 8u, "NVLink 0", false, ""},
            {d0, sw, false, LinkOperationalState::UP, 400000000000ULL, 8u, "NVLink 1", false, ""},
            {sw, d2, false, LinkOperationalState::UP, 400000000000ULL, 8u, "NVLink 2", false, ""},
            {d1, d2, false, LinkOperationalState::DEGRADED, 200000000000ULL, 8u, "NVLink 3", true, "degraded link"},
        };
        auto b = make_synthetic_backend(std::move(spec));
        auto devs = b->discover_devices();
        auto lks = b->discover_links(*devs);
        for (const auto& d : *devs) registry.upsert_device(d);
        for (const auto& l : *lks) registry.upsert_link(l);
        MeasurementRecord m1;
        m1.id = fresh_id<MeasurementTag>();
        m1.source = d0; m1.target = d1; m1.payload_bytes = 4u*1024u*1024u; m1.iterations = 8u;
        m1.warmup_iterations = 2u; m1.completed_transfers = 8u; m1.bandwidth_bps = 360.0e9;
        m1.latency_seconds = 7.0e-6; m1.integrity_verified = true; m1.valid = true;
        m1.evidence = Evidence{EvidenceClass::SYNTHETIC, Provenance::BENCHMARK | Provenance::SYNTHETIC_FIXTURE, "synthetic", "synthetic measurement"};
        registry.publish_measurement(m1);
    } else if (backend == "nop") {
        auto b = make_nop_backend();
        auto devs = b->discover_devices();
        for (const auto& d : *devs) registry.upsert_device(d);
#ifdef NVF_WITH_NVIDIA
    } else if (backend == "nvidia") {
        auto b = make_nvidia_backend();
        if (!b.has_value()) { std::printf("nvidia backend unavailable: %s\n", b.error().to_string().c_str()); return 3; }
        auto devs = (*b)->discover_devices();
        if (!devs.has_value()) { std::printf("nvidia device discovery failed: %s\n", devs.error().to_string().c_str()); return 3; }
        for (const auto& d : *devs) registry.upsert_device(d);
        auto lks = (*b)->discover_links(*devs);
        for (const auto& l : *lks) registry.upsert_link(l);
#endif
    } else {
        std::printf("unknown backend: %s\n", backend.c_str());
        return 2;
    }

    const TopologySnapshot snap = registry.snapshot();
    if (json) {
        std::string j = "{\"backend\":\"" + jesc(backend_note) + "\",\"topology_gen\":";
        j += std::to_string(registry.topology_generation().value());
        j += ",\"cmd\":\"" + jesc(cmd) + "\"";
        if (cmd == "devices") {
            for (const auto& d : snap.devices) {
                j += ",\"device\":{\"id\":" + std::to_string(d.id.value());
                j += ",\"name\":\"" + jesc(d.logical_name) + "\"";
                j += ",\"arch\":\"" + jesc(d.architecture) + "\"";
                j += ",\"health\":\"" + std::string(device_health_name(d.health)) + "\"";
                j += ",\"evidence\":\"" + std::string(evidence_class_name(d.evidence.evidence_class)) + "\"}";
            }
        } else if (cmd == "links") {
            for (const auto& l : snap.links) {
                j += ",\"link\":{\"id\":" + std::to_string(l.id.value());
                j += ",\"src\":" + std::to_string(l.source.value());
                j += ",\"tgt\":" + std::to_string(l.target.value());
                j += ",\"state\":\"" + std::string(link_state_name(l.state)) + "\"";
                j += ",\"tech\":\"" + std::string(interconnect_technology_name(l.technology)) + "\"";
                j += ",\"evidence\":\"" + std::string(evidence_class_name(l.evidence.evidence_class)) + "\"}";
            }
        } else if (cmd == "topology") {
            j += ",\"devices\":" + std::to_string(snap.devices.size());
            j += ",\"links\":" + std::to_string(snap.links.size());
        } else if (cmd == "paths" || cmd == "route") {
            if (args.size() < 2) { std::printf("paths/route requires SRC TGT\n"); return 2; }
            const DeviceId src(std::strtoull(args[0].c_str(), nullptr, 10));
            const DeviceId tgt(std::strtoull(args[1].c_str(), nullptr, 10));
            if (cmd == "paths") {
                auto paths = registry.enumerate_paths(src, tgt);
                if (!paths.has_value()) { std::printf("path enumeration error: %s\n", paths.error().to_string().c_str()); return 4; }
                for (const auto& p : *paths) {
                    j += ",\"path\":{\"id\":" + std::to_string(p.id.value());
                    j += ",\"hops\":" + std::to_string(p.hop_count);
                    j += ",\"direct\":" + std::to_string(p.direct ? 1 : 0) + "}";
                }
            } else {
                auto dec = registry.decide_route(src, tgt, RoutePolicy::default_policy());
                if (!dec.has_value()) { std::printf("route error: %s\n", dec.error().to_string().c_str()); return 4; }
                j += ",\"outcome\":\"" + std::string(route_outcome_name(dec->outcome)) + "\"";
                j += ",\"executable\":" + std::to_string(dec->executable ? 1 : 0);
                j += ",\"explanation\":\"" + jesc(dec->explanation) + "\"";
            }
        } else if (cmd == "status") {
            j += ",\"devices\":" + std::to_string(snap.devices.size());
            j += ",\"links\":" + std::to_string(snap.links.size());
            j += ",\"measurements\":" + std::to_string(registry.measurement_count());
        } else {
            std::printf("unknown command: %s\n", cmd.c_str());
            return 2;
        }
        std::printf("%s}\n", j.c_str());
    } else {
        std::printf("=== %s (backend=%s, topology_generation=%llu) ===\n", cmd.c_str(),
                    backend_note.c_str(), (unsigned long long)registry.topology_generation().value());
        if (cmd == "devices") {
            for (const auto& d : snap.devices)
                std::printf("  device id=%llu name=%s arch=%s health=%s evidence=%s\n",
                            (unsigned long long)d.id.value(), d.logical_name.c_str(), d.architecture.c_str(),
                            device_health_name(d.health), d.evidence.describe().c_str());
        } else if (cmd == "links") {
            for (const auto& l : snap.links)
                std::printf("  link id=%llu src=%llu tgt=%llu state=%s tech=%s evidence=%s\n",
                            (unsigned long long)l.id.value(), (unsigned long long)l.source.value(),
                            (unsigned long long)l.target.value(), link_state_name(l.state),
                            interconnect_technology_name(l.technology), l.evidence.describe().c_str());
        } else if (cmd == "topology") {
            std::printf("  devices=%zu links=%zu\n", snap.devices.size(), snap.links.size());
        } else if (cmd == "paths" || cmd == "route") {
            if (args.size() < 2) { std::printf("paths/route requires SRC TGT\n"); return 2; }
            const DeviceId src(std::strtoull(args[0].c_str(), nullptr, 10));
            const DeviceId tgt(std::strtoull(args[1].c_str(), nullptr, 10));
            if (cmd == "paths") {
                auto paths = registry.enumerate_paths(src, tgt);
                if (!paths.has_value()) { std::printf("path enumeration error: %s\n", paths.error().to_string().c_str()); return 4; }
                for (const auto& p : *paths) std::printf("  %s\n", registry.explain_path(p).c_str());
            } else {
                auto dec = registry.decide_route(src, tgt, RoutePolicy::default_policy());
                if (!dec.has_value()) { std::printf("route error: %s\n", dec.error().to_string().c_str()); return 4; }
                std::printf("  outcome=%s executable=%d\n", route_outcome_name(dec->outcome), dec->executable ? 1 : 0);
                std::printf("  explanation: %s\n", dec->explanation.c_str());
                for (const auto& rc : dec->rejected)
                    std::printf("  rejected path=%llu reason=%s (%s)\n",
                                (unsigned long long)rc.path.value(), rejection_reason_name(rc.reason), rc.detail.c_str());
            }
        } else if (cmd == "status") {
            std::printf("  topology_generation=%llu devices=%zu links=%zu measurements=%zu\n",
                        (unsigned long long)registry.topology_generation().value(),
                        snap.devices.size(), snap.links.size(), registry.measurement_count());
        } else {
            std::printf("unknown command: %s\n", cmd.c_str());
            return 2;
        }
    }
    return 0;
}
