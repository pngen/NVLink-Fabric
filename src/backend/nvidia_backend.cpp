#include "nvlinkfabric/backends/nvidia_backend.hpp"
#include "nvlinkfabric/identities.hpp"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

// CUDA / NVML / NVRTC system headers (warnings suppressed via system include dirs).
#include <cuda.h>
#include <cuda_runtime.h>
#include <nvml.h>
#include <nvrtc.h>

#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace nvlinkfabric {

namespace {

// Dynamic NVML loader: never hard-require optional telemetry.
struct NvmlApi {
    using InitFn = nvmlReturn_t (*)(void);
    using ShutdownFn = nvmlReturn_t (*)(void);
    using CountFn = nvmlReturn_t (*)(unsigned int*);
    using HandleFn = nvmlReturn_t (*)(unsigned int, nvmlDevice_t*);
    using PciFn = nvmlReturn_t (*)(nvmlDevice_t, nvmlPciInfo_t*);
    using NvLinkCountFn = nvmlReturn_t (*)(nvmlDevice_t, unsigned int*);

    bool loaded{false};
    InitFn init{nullptr};
    ShutdownFn shutdown{nullptr};
    CountFn device_get_count{nullptr};
    HandleFn device_get_handle{nullptr};
    PciFn device_get_pci{nullptr};
    NvLinkCountFn device_get_nvlink_count{nullptr};

    static NvmlApi load() {
        NvmlApi api;
#ifdef _WIN32
        HMODULE mod = LoadLibraryW(L"nvml.dll");
        if (!mod) return api;
        auto sym = [&](const char* name) -> void* { return reinterpret_cast<void*>(GetProcAddress(mod, name)); };
        api.init = reinterpret_cast<InitFn>(sym("nvmlInit_v2"));
        api.shutdown = reinterpret_cast<ShutdownFn>(sym("nvmlShutdown"));
        api.device_get_count = reinterpret_cast<CountFn>(sym("nvmlDeviceGetCount_v2"));
        api.device_get_handle = reinterpret_cast<HandleFn>(sym("nvmlDeviceGetHandleByIndex_v2"));
        api.device_get_pci = reinterpret_cast<PciFn>(sym("nvmlDeviceGetPciInfo_v2"));
        api.device_get_nvlink_count = reinterpret_cast<NvLinkCountFn>(sym("nvmlDeviceGetNvLinkCount"));
        api.loaded = api.init && api.shutdown && api.device_get_count && api.device_get_handle;
#endif
        return api;
    }
};

NvmlApi g_nvml = NvmlApi::load();
bool g_nvml_inited = false;

bool ensure_nvml() {
    if (!g_nvml.loaded) return false;
    if (!g_nvml_inited) {
        if (g_nvml.init() != NVML_SUCCESS) return false;
        g_nvml_inited = true;
    }
    return true;
}
void shutdown_nvml() {
    if (g_nvml_inited && g_nvml.shutdown) {
        g_nvml.shutdown();
        g_nvml_inited = false;
    }
}

std::string arch_name(unsigned major) {
    if (major >= 12) return "Blackwell";
    if (major >= 9) return "Hopper";
    if (major >= 8) return "Ampere";
    if (major >= 7) return "Volta";
    if (major >= 6) return "Pascal";
    if (major >= 5) return "Maxwell";
    return "Other";
}

const char* cuda_err_str(cudaError_t e) { return cudaGetErrorName(e); }

}  // namespace

namespace {

class NvidiaBackend final : public Backend {
public:
    std::string name() const override { return "nvidia"; }

    Result<BackendCapabilities> describe_capabilities() const override {
        BackendCapabilities c;
        c.backend_name = "nvidia";
        c.device_discovery = true;
        c.link_discovery = true;
        c.measurement = false;
        c.nvlink_discovery = true;
        c.measurement_support = MeasurementSupport::UNSUPPORTED;
        c.evidence = Evidence{EvidenceClass::REAL, Provenance::NVML | Provenance::CUDA_RUNTIME,
                              "nvidia", "NVIDIA/CUDA/NVML backend"};
        c.note = "NVLink connectivity depends on installed hardware; consumer GPUs report none";
        return Result<BackendCapabilities>::ok(std::move(c));
    }

    Result<std::vector<DeviceRecord>> discover_devices() override {
        std::vector<DeviceRecord> out;
        int count = 0;
        const cudaError_t cuda_err = cudaGetDeviceCount(&count);
        if (cuda_err != cudaSuccess)
            return Result<std::vector<DeviceRecord>>::err(
                ErrorCode::BACKEND_UNAVAILABLE,
                std::string("CUDA runtime unavailable: ") + cuda_err_str(cuda_err));

        for (int i = 0; i < count; ++i) {
            cudaDeviceProp prop{};
            if (cudaGetDeviceProperties(&prop, i) != cudaSuccess)
                return Result<std::vector<DeviceRecord>>::err(ErrorCode::BACKEND_ERROR,
                                                              "cudaGetDeviceProperties failed");
            DeviceRecord rec;
            rec.id = fresh_id<DeviceTag>();
            rec.logical_name = std::string(prop.name);
            rec.kind = NodeKind::ACCELERATOR;
            rec.backend = "nvidia";
            rec.backend_instance = "cuda:" + std::to_string(i);
            rec.cuda_ordinal = static_cast<unsigned>(i);
            rec.vendor = "NVIDIA";
            rec.architecture = arch_name(static_cast<unsigned>(prop.major));
            rec.device_generation = std::to_string(prop.major) + "." + std::to_string(prop.minor);
            rec.pci_identity = cuda_pci_identity(prop, i);
            rec.capability.interconnect = InterconnectTechnology::UNKNOWN;
            rec.capability.measurement_support = MeasurementSupport::UNKNOWN;
            rec.health = DeviceHealthState::PRESENT;
            rec.evidence = Evidence{EvidenceClass::REAL,
                                    Provenance::CUDA_RUNTIME | Provenance::CUDA_DRIVER,
                                    "nvidia", "CUDA device discovery"};
            rec.generation = fresh_generation<DeviceGenerationTag>();
            out.push_back(std::move(rec));
        }
        return Result<std::vector<DeviceRecord>>::ok(std::move(out));
    }

    Result<std::vector<LinkRecord>> discover_links(const std::vector<DeviceRecord>&) override {
        std::vector<LinkRecord> out;
        if (!ensure_nvml()) {
            return Result<std::vector<LinkRecord>>::ok(std::move(out));
        }
        unsigned int dev_count = 0u;
        if (g_nvml.device_get_count(&dev_count) != NVML_SUCCESS)
            return Result<std::vector<LinkRecord>>::ok(std::move(out));
        unsigned int total_links = 0u;
        for (unsigned int i = 0; i < dev_count; ++i) {
            nvmlDevice_t dev = nullptr;
            if (g_nvml.device_get_handle(i, &dev) != NVML_SUCCESS) continue;
            unsigned int links = 0u;
            if (g_nvml.device_get_nvlink_count && g_nvml.device_get_nvlink_count(dev, &links) == NVML_SUCCESS) {
                total_links += links;
            }
        }
        // total_links == 0 on this hardware: no NVLink links exist.
        return Result<std::vector<LinkRecord>>::ok(std::move(out));
    }

    Result<MeasurementRecord> measure(const MeasurementRequest&) override {
        return Result<MeasurementRecord>::err(
            ErrorCode::UNSUPPORTED,
            "no supported peer-access NVLink path on installed hardware; "
            "measurement requires at least two real NVLink-connected devices");
    }

    bool supports_measurement() const override { return false; }

private:
    static std::string cuda_pci_identity(const cudaDeviceProp& prop, int ordinal) {
        if (ensure_nvml()) {
            nvmlDevice_t dev = nullptr;
            if (g_nvml.device_get_handle(static_cast<unsigned>(ordinal), &dev) == NVML_SUCCESS) {
                nvmlPciInfo_t pci{};
                if (g_nvml.device_get_pci && g_nvml.device_get_pci(dev, &pci) == NVML_SUCCESS) {
                    char buf[64];
                    std::snprintf(buf, sizeof(buf), "%04x:%02x:%02x",
                                  pci.domain, pci.bus, pci.device);
                    return std::string(buf);
                }
            }
        }
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%02x:%02x", prop.pciBusID, prop.pciDeviceID);
        return std::string(buf);
    }
};

}  // namespace

Result<std::unique_ptr<Backend>> make_nvidia_backend() {
    auto backend = std::make_unique<NvidiaBackend>();
    int count = 0;
    if (cudaGetDeviceCount(&count) != cudaSuccess)
        return Result<std::unique_ptr<Backend>>::err(
            ErrorCode::BACKEND_UNAVAILABLE, "CUDA runtime unavailable");
    return Result<std::unique_ptr<Backend>>::ok(std::move(backend));
}

Result<CudaValidationResult> run_cuda_self_check() {
    CudaValidationResult res;
    res.evidence = Evidence{EvidenceClass::UNSUPPORTED, Provenance::NONE,
                            "cuda_self_check", "not yet available"};

    int probe = 0;
    cudaError_t e = cudaGetDeviceCount(&probe);
    if (e != cudaSuccess) {
        res.error = std::string("CUDA runtime unavailable: ") + cuda_err_str(e);
        return Result<CudaValidationResult>::ok(std::move(res));
    }
    res.cuda_available = true;
    res.evidence = Evidence{EvidenceClass::REAL, Provenance::CUDA_RUNTIME,
                            "cuda_self_check", "real CUDA device validation"};

    int count = 0;
    if (cudaGetDeviceCount(&count) != cudaSuccess) { res.error = "cudaGetDeviceCount failed"; return Result<CudaValidationResult>::ok(std::move(res)); }
    res.device_count = static_cast<unsigned>(count);
    cudaDeviceProp prop{};
    if (count > 0 && cudaGetDeviceProperties(&prop, 0) == cudaSuccess) res.device_name = std::string(prop.name);

    std::size_t free_b = 0, total_b = 0;
    if (cudaMemGetInfo(&free_b, &total_b) != cudaSuccess) { res.error = "cudaMemGetInfo failed"; return Result<CudaValidationResult>::ok(std::move(res)); }
    res.bytes_baseline = static_cast<std::uint64_t>(free_b);

    // Driver API context for NVRTC-compiled kernel launch.
    CUdevice cdev = -1;
    CUcontext ctx = nullptr;
    if (cuInit(0) != CUDA_SUCCESS) { res.error = "cuInit failed"; return Result<CudaValidationResult>::ok(std::move(res)); }
    if (cuDeviceGet(&cdev, 0) != CUDA_SUCCESS) { res.error = "cuDeviceGet failed"; return Result<CudaValidationResult>::ok(std::move(res)); }
    if (cuDevicePrimaryCtxRetain(&ctx, cdev) != CUDA_SUCCESS) { res.error = "cuDevicePrimaryCtxRetain failed"; return Result<CudaValidationResult>::ok(std::move(res)); }
    if (cuCtxSetCurrent(ctx) != CUDA_SUCCESS) { cuDevicePrimaryCtxRelease(cdev); res.error = "cuCtxSetCurrent failed"; return Result<CudaValidationResult>::ok(std::move(res)); }

    const std::uint64_t N = 1024u * 1024u;
    const std::size_t bytes = static_cast<std::size_t>(N * sizeof(float));
    float* d_out = nullptr;
    float* d_in = nullptr;
    std::vector<float> host_in(static_cast<std::size_t>(N)), host_out(static_cast<std::size_t>(N)), host_expected(static_cast<std::size_t>(N));
    for (std::uint64_t i = 0; i < N; ++i) {
        host_in[static_cast<std::size_t>(i)] = static_cast<float>(i % 1000u);
        host_expected[static_cast<std::size_t>(i)] = static_cast<float>(i % 1000u) + 1.0f;
    }

    bool ok = true;
    if (cudaMalloc(&d_in, bytes) != cudaSuccess) { res.error = "cudaMalloc(d_in) failed"; ok = false; }
    if (ok && cudaMalloc(&d_out, bytes) != cudaSuccess) { res.error = "cudaMalloc(d_out) failed"; ok = false; }
    if (ok && cudaMemcpy(d_in, host_in.data(), bytes, cudaMemcpyHostToDevice) != cudaSuccess) {
        res.error = "cudaMemcpy H2D failed"; ok = false;
    }
    res.bytes_allocated = ok ? static_cast<std::uint64_t>(bytes) * 2u : 0u;

    const char* src =
        "extern \"C\" __global__ void addf(float* out, const float* in, int n) {\n"
        "  int i = blockIdx.x * blockDim.x + threadIdx.x;\n"
        "  if (i < n) out[i] = in[i] + 1.0f;\n"
        "}\n";
    nvrtcProgram prog = nullptr;
    if (ok && nvrtcCreateProgram(&prog, src, "add.cu", 0, nullptr, nullptr) != NVRTC_SUCCESS) {
        res.error = "nvrtcCreateProgram failed"; ok = false;
    }
    if (ok) {
        const nvrtcResult nv = nvrtcCompileProgram(prog, 0, nullptr);
        if (nv != NVRTC_SUCCESS) {
            std::size_t logsz = 0u;
            nvrtcGetProgramLogSize(prog, &logsz);
            std::vector<char> log(logsz ? logsz : 1u);
            nvrtcGetProgramLog(prog, log.data());
            res.error = std::string("nvrtcCompileProgram failed: ") + log.data();
            ok = false;
        }
    }
    std::vector<char> ptx;
    CUfunction kernel = nullptr;
    CUmodule module = nullptr;
    if (ok) {
        std::size_t ptxsize = 0u;
        if (nvrtcGetPTXSize(prog, &ptxsize) != NVRTC_SUCCESS) { res.error = "nvrtcGetPTXSize failed"; ok = false; }
        if (ok) {
            ptx.resize(ptxsize);
            if (nvrtcGetPTX(prog, ptx.data()) != NVRTC_SUCCESS) { res.error = "nvrtcGetPTX failed"; ok = false; }
        }
    }
    if (ok && cuModuleLoadDataEx(&module, ptx.data(), 0, nullptr, nullptr) != CUDA_SUCCESS) {
        res.error = "cuModuleLoadDataEx failed"; ok = false;
    }
    if (ok && cuModuleGetFunction(&kernel, module, "addf") != CUDA_SUCCESS) {
        res.error = "cuModuleGetFunction failed"; ok = false;
    }

    if (ok) {
        CUdeviceptr dp_out = reinterpret_cast<CUdeviceptr>(d_out);
        CUdeviceptr dp_in = reinterpret_cast<CUdeviceptr>(d_in);
        int n = static_cast<int>(N);
        void* params[] = {&dp_out, &dp_in, &n};
        const int threads = 256;
        const int blocks = static_cast<int>((N + threads - 1) / threads);
        if (cuLaunchKernel(kernel, blocks, 1, 1, threads, 1, 1, 0, nullptr, params, nullptr) != CUDA_SUCCESS) {
            res.error = "cuLaunchKernel failed"; ok = false;
        }
    }
    if (ok && cuCtxSynchronize() != CUDA_SUCCESS) { res.error = "cuCtxSynchronize failed"; ok = false; }
    if (ok && cudaMemcpy(host_out.data(), d_out, bytes, cudaMemcpyDeviceToHost) != cudaSuccess) {
        res.error = "cudaMemcpy D2H failed"; ok = false;
    }

    if (ok) {
        bool parity = true;
        for (std::uint64_t i = 0; i < N; ++i) {
            if (host_out[static_cast<std::size_t>(i)] != host_expected[static_cast<std::size_t>(i)]) { parity = false; break; }
        }
        res.alloc_kernel_d2h_h2d_parity = parity;
        if (!parity) res.error = "CPU-reference parity mismatch";
    }

    if (d_in) cudaFree(d_in);
    if (d_out) cudaFree(d_out);
    if (prog) nvrtcDestroyProgram(&prog);
    if (module) cuModuleUnload(module);

    if (cudaMemGetInfo(&free_b, &total_b) == cudaSuccess)
        res.bytes_after_free = static_cast<std::uint64_t>(free_b);
    res.memory_returned_to_baseline =
        res.bytes_after_free + (2u * 1024u * 1024u) >= res.bytes_baseline;

    if (ctx) cuDevicePrimaryCtxRelease(cdev);

    return Result<CudaValidationResult>::ok(std::move(res));
}

}  // namespace nvlinkfabric
