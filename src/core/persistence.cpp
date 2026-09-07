#include "nvlinkfabric/persistence.hpp"
#include "nvlinkfabric/serialization.hpp"
#include <cstdio>
#include <cstdint>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

namespace nvlinkfabric {

namespace {
bool atomic_replace(const std::string& target, const std::string& tmp) {
#ifdef _WIN32
    // MOVEFILE_REPLACE_EXISTING provides an atomic replace on the same volume.
    const auto to_wide = [](const std::string& s) -> std::wstring {
        int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
        std::wstring w(static_cast<std::size_t>(n > 0 ? n - 1 : 0), L'\0');
        if (n > 0) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], n);
        return w;
    };
    const std::wstring wtmp = to_wide(tmp);
    const std::wstring wtgt = to_wide(target);
    if (!MoveFileExW(wtmp.c_str(), wtgt.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        return false;
    }
    return true;
#else
    std::error_code ec;
    std::filesystem::rename(tmp, target, ec);
    return !ec;
#endif
}
}  // namespace

Result<void> save_durable_state(const DurableState& state, const std::string& path,
                                const Limits& limits) {
    auto ser = serialize_durable_state(state);
    if (!ser.has_value()) return Result<void>::err(ser.error());
    if (ser->bytes.size() > limits.max_persisted_state_bytes)
        return Result<void>::err(ErrorCode::RESOURCE_LIMIT, "durable state exceeds size bound");

    const std::string tmp = path + ".tmp";
    {
        std::ofstream ofs(tmp, std::ios::binary | std::ios::trunc);
        if (!ofs.is_open())
            return Result<void>::err(ErrorCode::BACKEND_ERROR, "cannot open temp state file");
        ofs.write(reinterpret_cast<const char*>(ser->bytes.data()),
                  static_cast<std::streamsize>(ser->bytes.size()));
        ofs.flush();
        if (!ofs.good())
            return Result<void>::err(ErrorCode::BACKEND_ERROR, "failed writing temp state file");
    }
    if (!atomic_replace(path, tmp)) {
        std::remove(tmp.c_str());
        return Result<void>::err(ErrorCode::BACKEND_ERROR, "atomic replace of state file failed");
    }
    return Result<void>::ok();
}

Result<DurableState> load_durable_state(const std::string& path, const Limits& limits) {
    std::ifstream ifs(path, std::ios::binary | std::ios::ate);
    if (!ifs.is_open())
        return Result<DurableState>::err(ErrorCode::NOT_FOUND, "state file not found");
    const std::streamsize size = ifs.tellg();
    if (size < 0)
        return Result<DurableState>::err(ErrorCode::BACKEND_ERROR, "cannot read state file size");
    if (static_cast<std::size_t>(size) > limits.max_persisted_state_bytes)
        return Result<DurableState>::err(ErrorCode::RESOURCE_LIMIT, "state file exceeds size bound");
    ifs.seekg(0);
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    ifs.read(reinterpret_cast<char*>(bytes.data()), size);
    if (!ifs.good() && !ifs.eof())
        return Result<DurableState>::err(ErrorCode::BACKEND_ERROR, "failed reading state file");

    Serialized ser;
    ser.bytes = std::move(bytes);
    return deserialize_durable_state(ser);
}

}  // namespace nvlinkfabric
