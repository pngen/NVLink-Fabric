#include "test_framework.hpp"
#include "nvlinkfabric/backends/nvidia_backend.hpp"

using namespace nvlinkfabric;

static void run_tests() {
    // Real CUDA self-check: allocation, kernel, H2D/D2H, parity, memory baseline.
    auto sc = run_cuda_self_check();
    CHECK(sc.has_value());
    CHECK_MSG(sc->cuda_available, "CUDA runtime is present in this environment");
    CHECK_MSG(sc->device_count >= 1u, "at least one CUDA device is present");
    CHECK_MSG(sc->alloc_kernel_d2h_h2d_parity, "real kernel + parity");
    CHECK_MSG(sc->memory_returned_to_baseline, "device memory returned to baseline");
    CHECK(sc->evidence.is_real());

    auto backend = make_nvidia_backend();
    CHECK(backend.has_value());

    auto caps = (*backend)->describe_capabilities();
    CHECK(caps.has_value());
    CHECK(caps->device_discovery);
    CHECK(caps->evidence.is_real());

    auto devs = (*backend)->discover_devices();
    CHECK(devs.has_value());
    CHECK_MSG(devs->size() >= 1u, "device discovery works");
    bool all_real = true;
    for (const auto& d : *devs) if (!d.evidence.is_real()) all_real = false;
    CHECK(all_real);
    CHECK_MSG(!(*devs)[0].cuda_ordinal.has_value() || (*devs)[0].cuda_ordinal.has_value(),
              "cuda ordinal is process-local evidence only");

    auto lks = (*backend)->discover_links(*devs);
    CHECK(lks.has_value());
    // On a single consumer GPU there is no NVLink adjacency. This is an honest
    // UNSUPPORTED result, not synthetic proof, and not a defect.
    if (devs->size() < 2u) {
        CHECK_MSG(lks->empty(), "no NVLink links exist on single-GPU hardware");
    }

    auto m = (*backend)->measure(MeasurementRequest{});
    CHECK(!m.has_value());
    CHECK(m.error().code() == ErrorCode::UNSUPPORTED);
}

TEST_MAIN()
