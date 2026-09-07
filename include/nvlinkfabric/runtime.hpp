#pragma once
#include "nvlinkfabric/registry.hpp"
#include "nvlinkfabric/route.hpp"
#include "nvlinkfabric/policy.hpp"
#include "nvlinkfabric/measurement.hpp"
#include "nvlinkfabric/result.hpp"
#include <cstdint>
#include <memory>
#include <string>

namespace nvlinkfabric {

//---------------------------------------------------------------------------
// Multiprocess coordinator and worker lifecycle.
//
// A coordinator owns canonical topology state and authority (a CoordinatorEpoch).
// Workers publish device/link/measurement evidence over a bounded framed TCP
// transport. Real OS-process death is detected when a worker connection closes;
// the coordinator then marks that dynamic evidence REVALIDATION_REQUIRED so it
// never remains freshly authoritative. A stale WorkerBootId or stale
// CoordinatorEpoch replay is rejected.
//---------------------------------------------------------------------------

struct WorkerDescriptor {
    WorkerId worker;
    WorkerBootId boot;
    CoordinatorEpoch epoch;
};

class CoordinatorServer {
public:
    explicit CoordinatorServer(Limits limits = Limits{});
    ~CoordinatorServer();
    CoordinatorServer(const CoordinatorServer&) = delete;
    CoordinatorServer& operator=(const CoordinatorServer&) = delete;

    // Binds a loopback TCP listener (0 => ephemeral). Returns the bound port.
    Result<std::uint16_t> start(std::uint16_t port = 0u);
    // Stops the listener, closes worker connections, and joins all threads.
    void stop();

    std::uint16_t port() const noexcept { return port_; }
    TopologyRegistry& registry() noexcept { return registry_; }
    CoordinatorEpoch epoch() const noexcept { return epoch_; }

    // Number of currently-registered live workers.
    std::size_t live_worker_count() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    TopologyRegistry registry_;
    CoordinatorEpoch epoch_;
    std::uint16_t port_{0u};
    // Connection handler; sock is a platform socket handle boxed as uintptr_t.
    void handle_connection(std::uintptr_t sock);
};

class WorkerClient {
public:
    explicit WorkerClient(std::uint16_t port);
    ~WorkerClient();
    WorkerClient(const WorkerClient&) = delete;
    WorkerClient& operator=(const WorkerClient&) = delete;

    Result<void> connect(const WorkerDescriptor& desc);
    Result<void> publish_device(const DeviceRecord& device);
    Result<void> publish_link(const LinkRecord& link);
    Result<void> publish_measurement(const MeasurementRecord& measurement);
    Result<void> signal_ready();
    Result<void> heartbeat();
    Result<RouteDecision> request_route(DeviceId source, DeviceId target, const RoutePolicy& policy);
    void disconnect();

private:
    std::uint16_t port_;
    std::uintptr_t sock_{0u};  // opaque platform socket handle
};

//---------------------------------------------------------------------------
// Process helpers (Windows: CreateProcess / TerminateProcess), used by tests to
// prove REAL multiprocess worker death rather than an in-process simulation.
//---------------------------------------------------------------------------
struct WorkerProcessHandle {
    void* handle{nullptr};
    bool valid() const noexcept { return handle != nullptr; }
};
// Spawns the worker executable; the worker connects to the coordinator and
// publishes the given device/link evidence, then stays alive until killed.
struct WorkerProcessSpec {
    std::string worker_exe;      // path to nvlink_worker executable
    std::uint16_t port{0u};
    WorkerId worker_id;
    WorkerBootId boot;
    CoordinatorEpoch epoch;
};
Result<WorkerProcessHandle> spawn_worker(const WorkerProcessSpec& spec);
Result<void> kill_worker(WorkerProcessHandle& handle);  // real OS termination
Result<void> wait_worker(WorkerProcessHandle& handle);

}  // namespace nvlinkfabric
