#pragma once
#include "nvlinkfabric/durable.hpp"
#include "nvlinkfabric/limits.hpp"
#include "nvlinkfabric/result.hpp"
#include <string>

namespace nvlinkfabric {

//---------------------------------------------------------------------------
// Durable-state persistence to a file.
// The file is written atomically (temp file + atomic replace). On load the
// frame is size-bounded, integrity-checked, and validated. Any truncation,
// corruption, or trailing garbage is rejected.
//---------------------------------------------------------------------------
Result<void> save_durable_state(const DurableState& state, const std::string& path,
                                const Limits& limits);
Result<DurableState> load_durable_state(const std::string& path, const Limits& limits);

}  // namespace nvlinkfabric
