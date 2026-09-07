#include "test_framework.hpp"
#include "nvlinkfabric/runtime.hpp"
#include "nvlinkfabric/enums.hpp"
#include <chrono>
#include <functional>
#include <thread>

using namespace nvlinkfabric;

#ifndef NVF_WORKER_EXE
#define NVF_WORKER_EXE "nvlink_worker.exe"
#endif

static void wait_until(std::function<bool()> pred, int tries, const char* expr) {
    bool ok = false;
    for (int i = 0; i < tries; ++i) {
        if (pred()) { ok = true; break; }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    nvltest::report(ok, expr, __FILE__, __LINE__);
}

static void run_tests() {
    CoordinatorServer server;
    auto started = server.start(0u);
    CHECK(started.has_value());
    const std::uint16_t port = *started;
    const WorkerId wid(5u);

    // Worker #1 publishes synthetic device/link/measurement evidence.
    WorkerProcessSpec s1;
    s1.worker_exe = NVF_WORKER_EXE;
    s1.port = port;
    s1.worker_id = wid;
    s1.boot = WorkerBootId(10u);
    s1.epoch = server.epoch();
    auto h1 = spawn_worker(s1);
    CHECK(h1.has_value());

    wait_until([&]() { return server.registry().device_count() >= 2u; }, 400, "worker published 2 devices");

    auto dec = server.registry().decide_route(DeviceId(100u), DeviceId(101u), RoutePolicy::default_policy());
    CHECK(dec.has_value());
    CHECK_MSG(dec->outcome == RouteDecisionOutcome::ROUTE_ALLOWED, "route allowed from worker evidence");
    CHECK(dec->executable);

    // Real OS-process death.
    CHECK(kill_worker(*h1).has_value());

    // Coordinator detects death: dynamic evidence becomes REVALIDATION_REQUIRED
    // and the stale route decision is no longer executable.
    wait_until([&]() {
        auto view = server.registry().consistent_view();
        if (view.topology.links.empty()) return false;
        return view.topology.links[0].state == LinkOperationalState::REVALIDATION_REQUIRED;
    }, 400, "worker death detected -> dynamic evidence REVALIDATION_REQUIRED");

    CHECK_MSG(!server.registry().is_route_executable(*dec), "stale route no longer executable after worker death");

    // Worker #2 with a fresh boot identity restores eligibility.
    WorkerProcessSpec s2;
    s2.worker_exe = NVF_WORKER_EXE;
    s2.port = port;
    s2.worker_id = wid;
    s2.boot = WorkerBootId(11u);
    s2.epoch = server.epoch();
    auto h2 = spawn_worker(s2);
    CHECK(h2.has_value());
    wait_until([&]() {
        auto view = server.registry().consistent_view();
        for (const auto& l : view.topology.links)
            if (l.state == LinkOperationalState::UP) return true;
        return false;
    }, 400, "fresh worker re-registers and publishes UP link");

    auto dec2 = server.registry().decide_route(DeviceId(100u), DeviceId(101u), RoutePolicy::default_policy());
    CHECK(dec2.has_value());
    CHECK_MSG(dec2->outcome == RouteDecisionOutcome::ROUTE_ALLOWED, "route allowed after fresh worker");
    CHECK(dec2->executable);

    // Stale WorkerBootId replay is rejected.
    WorkerClient replay(port);
    auto r1 = replay.connect(WorkerDescriptor{wid, WorkerBootId(10u), server.epoch()});
    CHECK_MSG(!r1.has_value(), "stale WorkerBootId replay rejected");
    CHECK(r1.error().code() == ErrorCode::STALE_BOOT);

    // Stale CoordinatorEpoch replay is rejected.
    WorkerClient staleepoch(port);
    auto r2 = staleepoch.connect(WorkerDescriptor{wid, WorkerBootId(12u), CoordinatorEpoch(server.epoch().value() - 1u)});
    CHECK_MSG(!r2.has_value(), "stale CoordinatorEpoch replay rejected");
    CHECK(r2.error().code() == ErrorCode::STALE_EPOCH);

    CHECK(kill_worker(*h2).has_value());
    server.stop();
}

TEST_MAIN()
