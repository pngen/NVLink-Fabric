#pragma once
#include "nvlinkfabric/result.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace nvlinkfabric {
namespace proto {

//---------------------------------------------------------------------------
// Bounded, framed coordinator<->worker protocol.
//
// Frame layout (little-endian):
//   [ magic 4 ][ version 1 ][ type 1 ][ payload_len 4 ][ payload ][ crc32 4 ]
//
// Every frame is size-bounded (see Limits::max_frame_bytes). Decoding rejects
// malformed headers, absurd lengths, truncation, unknown mandatory versions,
// invalid message types, bad checksums, and trailing garbage.
//---------------------------------------------------------------------------
constexpr std::uint32_t kMagic = 0x46564E4Eu;  // "NVF" little-endian
constexpr std::uint8_t kVersion = 1u;
constexpr std::size_t kHeaderNoPayload = 4u + 1u + 1u + 4u;
constexpr std::size_t kCrcBytes = 4u;

enum class MessageType : std::uint8_t {
    HELLO = 1u,
    REGISTER = 2u,
    PUBLISH_DEVICE = 3u,
    PUBLISH_LINK = 4u,
    PUBLISH_TOPOLOGY = 5u,
    PUBLISH_MEASUREMENT = 6u,
    READY = 7u,
    QUERY = 8u,
    ROUTE = 9u,
    HEARTBEAT = 10u,
    GOODBYE = 11u
};

inline const char* message_type_name(MessageType t) noexcept {
    switch (t) {
        case MessageType::HELLO: return "HELLO";
        case MessageType::REGISTER: return "REGISTER";
        case MessageType::PUBLISH_DEVICE: return "PUBLISH_DEVICE";
        case MessageType::PUBLISH_LINK: return "PUBLISH_LINK";
        case MessageType::PUBLISH_TOPOLOGY: return "PUBLISH_TOPOLOGY";
        case MessageType::PUBLISH_MEASUREMENT: return "PUBLISH_MEASUREMENT";
        case MessageType::READY: return "READY";
        case MessageType::QUERY: return "QUERY";
        case MessageType::ROUTE: return "ROUTE";
        case MessageType::HEARTBEAT: return "HEARTBEAT";
        case MessageType::GOODBYE: return "GOODBYE";
    }
    return "UNKNOWN";
}

struct Frame {
    MessageType type{MessageType::HELLO};
    std::vector<std::uint8_t> payload;
};

// Serializes a frame (with header + checksum). Returns an error if the payload
// exceeds the configured bound.
Result<std::vector<std::uint8_t>> encode_frame(const Frame& frame, std::size_t max_bytes);
// Decodes exactly one frame; rejects malformed/invalid/oversized input and any
// trailing bytes beyond the frame (caller may handle a stream of frames).
Result<Frame> decode_frame(const std::vector<std::uint8_t>& bytes, std::size_t max_bytes);

// Small helpers for building/reading typed payloads with bounds.
void put_u8(std::vector<std::uint8_t>& out, std::uint8_t v);
void put_u16(std::vector<std::uint8_t>& out, std::uint16_t v);
void put_u32(std::vector<std::uint8_t>& out, std::uint32_t v);
void put_u64(std::vector<std::uint8_t>& out, std::uint64_t v);
void put_str(std::vector<std::uint8_t>& out, const std::string& s);

struct PayloadReader {
    const std::vector<std::uint8_t>& b;
    std::size_t pos{0u};
    bool bad{false};
    explicit PayloadReader(const std::vector<std::uint8_t>& v) : b(v) {}
    std::uint8_t u8();
    std::uint16_t u16();
    std::uint32_t u32();
    std::uint64_t u64();
    std::string str();
};

}  // namespace proto
}  // namespace nvlinkfabric
