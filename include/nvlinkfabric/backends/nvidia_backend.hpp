#pragma once
#include "nvlinkfabric/backend.hpp"
#include "nvlinkfabric/result.hpp"
#include <cstdint>
#include <memory>
#include <string>

namespace nvlinkfabric {

//---------------------------------------------------------------------------
// NVIDIA backend factory.
//
// The factory initializes NVML/CUDA availability and returns a typed result.
// If the NVIDIA runtime/device support is unavailable or unsupported for the
// installed hardware (e.g. a single consumer GPU with no NVLink), the backend
// is still returned, but its measured capabilities and links will report
// UNSUPPORTED / no NVLink connectivity rather than fabricating evidence.
//---------------------------------------------------------------------------
Result<std::unique_ptr<Backend>> make_nvidia_backend();

//---------------------------------------------------------------------------
// CudaValidationResult: the result of a real CUDA self-check that performs
// genuine device allocation, a kernel, host/device transfers, CPU-reference
// parity, and frees back to the memory baseline. This proves CUDA integration
// only; it does NOT prove NVLink connectivity.
//---------------------------------------------------------------------------
struct CudaValidationResult {
    bool cuda_available{false};
    unsigned device_count{0u};
    std::string device_name;
    std::string error;                 // non-empty if a step failed
    bool alloc_kernel_d2h_h2d_parity{false};
    std::uint64_t bytes_allocated{0u};
    std::uint64_t bytes_baseline{0u};
    std::uint64_t bytes_after_free{0u};
    bool memory_returned_to_baseline{false};
    Evidence evidence;
};

// Runs a real CUDA self-check (see CudaValidationResult). Available only when
// the CUDA backend is linked.
Result<CudaValidationResult> run_cuda_self_check();

}  // namespace nvlinkfabric
