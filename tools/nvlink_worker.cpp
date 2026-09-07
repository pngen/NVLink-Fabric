// nvlink_worker: an NVLink Fabric worker process. It connects to a coordinator,
// publishes a bounded set of synthetic device/link/measurement evidence, and
// then keeps the connection alive (heartbeating) until it is terminated as a
// real OS process. This tool exists to prove REAL multiprocess worker behavior.
#include "nvlinkfabric/runtime.hpp"
#include "nvlinkfabric/identities.hpp"
#include "nvlinkfabric/enums.hpp"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>

using namespace nvlinkfabric;

int main(int argc, char** argv) {
    if (argc < 5) {
        std::printf("usage: nvlink_worker <port> <worker_id> <boot> <epoch>\n");
        return 2;
    }
    const std::uint16_t port = static_cast<std::uint16_t>(std::strtoul(argv[1], nullptr, 10));
    const WorkerId wid(std::strtoull(argv[2], nullptr, 10));
    const WorkerBootId boot(std::strtoull(argv[3], nullptr, 10));
    const CoordinatorEpoch epoch(std::strtoull(argv[4], nullptr, 10));

    WorkerClient client(port);
    auto c = client.connect(WorkerDescriptor{wid, boot, epoch});
    if (!c.has_value()) {
        std::printf("connect rejected: %s\n", c.error().to_string().c_str());
        return 3;
    }

    // Publish a bounded synthetic two-device topology + measurement.
    // Evidence generations and ids are SEEDED FROM THE BOOT IDENTITY so that
    // one process incarnation never collides with (or regresses) a previous
    // incarnation's evidence: coordination rejects stale generations. This is
    // process-incarnation fencing at the evidence level.
    const std::uint64_t base = boot.value() * 1000000ULL;
    DeviceId d0(100u), d1(101u);
    DeviceRecord dr0;
    dr0.id = d0;
    dr0.logical_name = "gpu0";
    dr0.kind = NodeKind::ACCELERATOR;
    dr0.architecture = "SYNTHETIC";
    dr0.device_generation = "SYNTHETIC";
    dr0.health = DeviceHealthState::PRESENT;
    dr0.evidence = Evidence{EvidenceClass::SYNTHETIC, Provenance::SYNTHETIC_FIXTURE, "worker", "worker-synthetic device"};
    dr0.generation = DeviceGeneration(base + 1u);
    dr0.capability.nominal_bandwidth_bps = 400000000000ULL;
    dr0.capability.peer_access_supported = true;
    client.publish_device(dr0);

    DeviceRecord dr1;
    dr1.id = d1;
    dr1.logical_name = "gpu1";
    dr1.kind = NodeKind::ACCELERATOR;
    dr1.architecture = "SYNTHETIC";
    dr1.device_generation = "SYNTHETIC";
    dr1.health = DeviceHealthState::PRESENT;
    dr1.evidence = Evidence{EvidenceClass::SYNTHETIC, Provenance::SYNTHETIC_FIXTURE, "worker", "worker-synthetic device"};
    dr1.generation = DeviceGeneration(base + 2u);
    dr1.capability.nominal_bandwidth_bps = 400000000000ULL;
    dr1.capability.peer_access_supported = true;
    client.publish_device(dr1);

    LinkRecord link;
    link.id = LinkId(base + 1u);
    link.source = d0;
    link.target = d1;
    link.state = LinkOperationalState::UP;
    link.technology = InterconnectTechnology::NVLINK;
    link.capability.nominal_bandwidth_bps = 400000000000ULL;
    link.generation = LinkGeneration(base + 1u);
    link.evidence = Evidence{EvidenceClass::SYNTHETIC, Provenance::SYNTHETIC_FIXTURE, "worker", "worker-synthetic link"};
    link.measurement_supported = true;
    link.last_observation = now_timestamp();
    client.publish_link(link);

    MeasurementRecord m;
    m.id = MeasurementId(base + 1u);
    m.source = d0;
    m.target = d1;
    m.payload_bytes = 4u * 1024u * 1024u;
    m.iterations = 8u;
    m.warmup_iterations = 2u;
    m.completed_transfers = 8u;
    m.bandwidth_bps = 380.0e9;
    m.latency_seconds = 8.0e-6;
    m.integrity_verified = true;
    m.valid = true;
    m.evidence = Evidence{EvidenceClass::SYNTHETIC, Provenance::SYNTHETIC_FIXTURE, "worker", "worker-synthetic measurement"};
    client.publish_measurement(m);

    client.signal_ready();

    // Keep the connection alive until this process is terminated (real death).
    for (;;) {
        auto hb = client.heartbeat();
        if (!hb.has_value()) return 4;  // coordinator gone
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}
