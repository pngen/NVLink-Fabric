#include "test_framework.hpp"
#include "nvlinkfabric/protocol.hpp"
#include "nvlinkfabric/limits.hpp"

using namespace nvlinkfabric;
namespace p = nvlinkfabric::proto;

static void run_tests() {
    const std::size_t MB = 16u * 1024u * 1024u;

    // Round-trip each message type.
    for (std::uint8_t t = 1u; t <= 11u; ++t) {
        p::Frame f;
        f.type = static_cast<p::MessageType>(t);
        std::vector<std::uint8_t> pb;
        p::put_u64(pb, 42u);
        p::put_str(pb, "hello");
        f.payload = pb;
        auto enc = p::encode_frame(f, MB);
        CHECK(enc.has_value());
        auto dec = p::decode_frame(*enc, MB);
        CHECK(dec.has_value());
        CHECK(dec->type == f.type);
        p::PayloadReader r(dec->payload);
        CHECK_EQ(r.u64(), 42u);
        CHECK(r.str() == "hello");
    }

    // Valid but oversized -> resource limit.
    p::Frame big;
    big.type = p::MessageType::HEARTBEAT;
    big.payload.assign(MB + 1u, 0x00u);
    CHECK(!p::encode_frame(big, MB).has_value());

    // Corrupt checksum.
    p::Frame f;
    f.type = p::MessageType::HELLO;
    auto enc = p::encode_frame(f, MB);
    (*enc)[enc->size() - 1u] ^= 0xFFu;
    CHECK(!p::decode_frame(*enc, MB).has_value());

    // Unknown mandatory version.
    auto enc2 = p::encode_frame(f, MB);
    (*enc2)[4u] = 99u;  // version byte
    CHECK(!p::decode_frame(*enc2, MB).has_value());

    // Invalid message type (0).
    auto enc3 = p::encode_frame(f, MB);
    (*enc3)[5u] = 0u;
    CHECK(!p::decode_frame(*enc3, MB).has_value());

    // Truncated frame.
    auto enc4 = p::encode_frame(f, MB);
    enc4->resize(enc4->size() - 2u);
    CHECK(!p::decode_frame(*enc4, MB).has_value());

    // Trailing garbage.
    auto enc5 = p::encode_frame(f, MB);
    enc5->push_back(0x00u);
    CHECK(!p::decode_frame(*enc5, MB).has_value());

    // Too-short frame.
    std::vector<std::uint8_t> shortf(3u, 0u);
    CHECK(!p::decode_frame(shortf, MB).has_value());

    // Bad magic.
    auto enc6 = p::encode_frame(f, MB);
    (*enc6)[0u] = 0xABu;
    CHECK(!p::decode_frame(*enc6, MB).has_value());

    // Absurd length declared.
    auto enc7 = p::encode_frame(f, MB);
    for (int i = 4; i < 8; ++i) (*enc7)[i] = 0xFFu;  // payload_len = 0xFFFFFFFF
    CHECK(!p::decode_frame(*enc7, MB).has_value());
}

TEST_MAIN()
