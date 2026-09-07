#pragma once
#include "nvlinkfabric/durable.hpp"
#include "nvlinkfabric/result.hpp"
#include <cstdint>
#include <vector>

namespace nvlinkfabric {

//---------------------------------------------------------------------------
// Binary serialization of DurableState with integrity checking.
// The encoding is deterministic, versioned, and bounded. Any truncation,
// corruption, impossible counts, integer overflow, or trailing garbage is
// rejected with ErrorCode::INTEGRITY_FAILURE / INVALID_ARGUMENT.
//---------------------------------------------------------------------------

// Serialized frame: [magic 4][version 4][payload_len 8][payload][crc32 4].
struct Serialized {
    std::uint32_t format_version{1u};
    std::vector<std::uint8_t> bytes;   // full frame including header and checksum
};

Result<Serialized> serialize_durable_state(const DurableState& state);
Result<DurableState> deserialize_durable_state(const Serialized& serialized);

}  // namespace nvlinkfabric
