#pragma once
#include "nvlinkfabric/backend.hpp"
#include <memory>

namespace nvlinkfabric {

// A no-op backend used for portability tests: it reports no devices, no links,
// and UNSUPPORTED measurement, always with honest evidence classification.
std::unique_ptr<Backend> make_nop_backend();

}  // namespace nvlinkfabric
