#include "nvlinkfabric/protocol.hpp"
#include <cstring>
#include <string>

namespace nvlinkfabric {
namespace proto {

namespace {
std::uint32_t crc32(const std::uint8_t* data, std::size_t len) {
    std::uint32_t crc = 0xFFFFFFFFu;
    for (std::size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (int k = 0; k < 8; ++k) {
            const std::uint32_t mask = static_cast<std::uint32_t>(-(static_cast<std::int32_t>(crc & 1u)));
            crc = (crc >> 1) ^ (0xEDB88320u & mask);
        }
    }
    return ~crc;
}

bool valid_type(std::uint8_t t) {
    return t >= static_cast<std::uint8_t>(MessageType::HELLO) &&
           t <= static_cast<std::uint8_t>(MessageType::GOODBYE) &&
           t != 0u;
}
}  // namespace

void put_u8(std::vector<std::uint8_t>& out, std::uint8_t v) { out.push_back(v); }
void put_u16(std::vector<std::uint8_t>& out, std::uint16_t v) {
    out.push_back(static_cast<std::uint8_t>(v));
    out.push_back(static_cast<std::uint8_t>(v >> 8));
}
void put_u32(std::vector<std::uint8_t>& out, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) out.push_back(static_cast<std::uint8_t>((v >> (8 * i)) & 0xFFu));
}
void put_u64(std::vector<std::uint8_t>& out, std::uint64_t v) {
    for (int i = 0; i < 8; ++i) out.push_back(static_cast<std::uint8_t>((v >> (8 * i)) & 0xFFu));
}
void put_str(std::vector<std::uint8_t>& out, const std::string& s) {
    const std::uint16_t len = static_cast<std::uint16_t>(s.size() <= 65535u ? s.size() : 65535u);
    put_u16(out, len);
    for (std::size_t i = 0; i < static_cast<std::size_t>(len); ++i)
        out.push_back(static_cast<std::uint8_t>(s[i]));
}

std::uint8_t PayloadReader::u8() {
    if (!(pos + 1u <= b.size())) { bad = true; return 0u; }
    return b[pos++];
}
std::uint16_t PayloadReader::u16() {
    if (!(pos + 2u <= b.size())) { bad = true; return 0u; }
    std::uint16_t v = static_cast<std::uint16_t>(b[pos]) | (static_cast<std::uint16_t>(b[pos + 1u]) << 8);
    pos += 2u;
    return v;
}
std::uint32_t PayloadReader::u32() {
    if (!(pos + 4u <= b.size())) { bad = true; return 0u; }
    std::uint32_t v = 0u;
    for (int i = 0; i < 4; ++i) v |= (static_cast<std::uint32_t>(b[pos + i]) << (8 * i));
    pos += 4u;
    return v;
}
std::uint64_t PayloadReader::u64() {
    if (!(pos + 8u <= b.size())) { bad = true; return 0u; }
    std::uint64_t v = 0u;
    for (int i = 0; i < 8; ++i) v |= (static_cast<std::uint64_t>(b[pos + i]) << (8 * i));
    pos += 8u;
    return v;
}
std::string PayloadReader::str() {
    const std::uint16_t n = u16();
    if (bad || !(pos + static_cast<std::size_t>(n) <= b.size())) { bad = true; return {}; }
    std::string s;
    s.reserve(n);
    for (std::uint16_t i = 0; i < n; ++i) s.push_back(static_cast<char>(b[pos++]));
    return s;
}

Result<std::vector<std::uint8_t>> encode_frame(const Frame& frame, std::size_t max_bytes) {
    if (frame.payload.size() + kHeaderNoPayload + kCrcBytes > max_bytes)
        return Result<std::vector<std::uint8_t>>::err(ErrorCode::RESOURCE_LIMIT,
                                                      "frame payload exceeds bound");
    std::vector<std::uint8_t> out;
    put_u32(out, kMagic);
    put_u8(out, kVersion);
    put_u8(out, static_cast<std::uint8_t>(frame.type));
    put_u32(out, static_cast<std::uint32_t>(frame.payload.size()));
    out.insert(out.end(), frame.payload.begin(), frame.payload.end());
    const std::uint32_t crc = crc32(out.data(), out.size());
    put_u32(out, crc);
    return Result<std::vector<std::uint8_t>>::ok(std::move(out));
}

Result<Frame> decode_frame(const std::vector<std::uint8_t>& bytes, std::size_t max_bytes) {
    if (bytes.size() < kHeaderNoPayload + kCrcBytes)
        return Result<Frame>::err(ErrorCode::PROTOCOL_ERROR, "frame shorter than header+checksum");
    std::size_t pos = 0u;
    const std::uint32_t magic = static_cast<std::uint32_t>(bytes[pos]) | (static_cast<std::uint32_t>(bytes[pos + 1u]) << 8) |
                                (static_cast<std::uint32_t>(bytes[pos + 2u]) << 16) | (static_cast<std::uint32_t>(bytes[pos + 3u]) << 24);
    pos += 4u;
    if (magic != kMagic) return Result<Frame>::err(ErrorCode::PROTOCOL_ERROR, "bad magic");
    const std::uint8_t version = bytes[pos++];
    if (version != kVersion) return Result<Frame>::err(ErrorCode::PROTOCOL_ERROR, "unsupported protocol version");
    const std::uint8_t type_byte = bytes[pos++];
    if (!valid_type(type_byte)) return Result<Frame>::err(ErrorCode::PROTOCOL_ERROR, "invalid message type");
    const std::uint32_t payload_len = static_cast<std::uint32_t>(bytes[pos]) | (static_cast<std::uint32_t>(bytes[pos + 1u]) << 8) |
                                      (static_cast<std::uint32_t>(bytes[pos + 2u]) << 16) | (static_cast<std::uint32_t>(bytes[pos + 3u]) << 24);
    pos += 4u;
    const std::size_t frame_size = static_cast<std::size_t>(kHeaderNoPayload) + payload_len + kCrcBytes;
    if (frame_size > max_bytes)
        return Result<Frame>::err(ErrorCode::RESOURCE_LIMIT, "frame exceeds configured bound");
    if (bytes.size() < frame_size)
        return Result<Frame>::err(ErrorCode::PROTOCOL_ERROR, "truncated frame");
    if (bytes.size() != frame_size)
        return Result<Frame>::err(ErrorCode::PROTOCOL_ERROR, "trailing bytes after frame");

    const std::uint32_t expected = crc32(bytes.data(), frame_size - kCrcBytes);
    std::uint32_t stored = 0u;
    for (int i = 0; i < 4; ++i)
        stored |= static_cast<std::uint32_t>(bytes[frame_size - 4u + i]) << (8 * i);
    if (stored != expected)
        return Result<Frame>::err(ErrorCode::PROTOCOL_ERROR, "frame checksum mismatch");

    Frame f;
    f.type = static_cast<MessageType>(type_byte);
    f.payload.assign(bytes.begin() + static_cast<std::ptrdiff_t>(kHeaderNoPayload),
                     bytes.begin() + static_cast<std::ptrdiff_t>(kHeaderNoPayload + payload_len));
    return Result<Frame>::ok(std::move(f));
}

}  // namespace proto
}  // namespace nvlinkfabric
