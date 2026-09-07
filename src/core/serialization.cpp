#include "nvlinkfabric/serialization.hpp"
#include "nvlinkfabric/limits.hpp"
#include <cstring>
#include <string>

namespace nvlinkfabric {

namespace {

constexpr std::uint32_t kMagic = 0x4446564Eu;  // 'NVFD' little-endian
constexpr std::uint32_t kVersion = 1u;
constexpr std::uint16_t kMaxString = 4096u;

// ---------------------------------------------------------------------------
// Writer
// ---------------------------------------------------------------------------
struct Writer {
    std::vector<std::uint8_t>& b;
    void u8(std::uint8_t v) { b.push_back(v); }
    void u16(std::uint16_t v) { b.push_back(static_cast<std::uint8_t>(v)); b.push_back(static_cast<std::uint8_t>(v >> 8)); }
    void u32(std::uint32_t v) { for (int i = 0; i < 4; ++i) b.push_back(static_cast<std::uint8_t>((v >> (8 * i)) & 0xFFu)); }
    void u64(std::uint64_t v) { for (int i = 0; i < 8; ++i) b.push_back(static_cast<std::uint8_t>((v >> (8 * i)) & 0xFFu)); }
    void str(const std::string& s) {
        std::uint16_t len = static_cast<std::uint16_t>(s.size());
        u16(len);
        for (char c : s) u8(static_cast<std::uint8_t>(c));
    }
};

// ---------------------------------------------------------------------------
// Reader with strict bounds checking
// ---------------------------------------------------------------------------
struct Reader {
    const std::vector<std::uint8_t>& b;
    std::size_t pos{0u};
    bool bad{false};
    explicit Reader(const std::vector<std::uint8_t>& v) : b(v) {}
    bool remaining(std::size_t n) const { return pos + n <= b.size(); }
    std::uint8_t u8() {
        if (!remaining(1)) { bad = true; return 0u; }
        return b[pos++];
    }
    std::uint16_t u16() {
        if (!remaining(2)) { bad = true; return 0u; }
        std::uint16_t v = static_cast<std::uint16_t>(b[pos]) | (static_cast<std::uint16_t>(b[pos + 1]) << 8);
        pos += 2;
        return v;
    }
    std::uint32_t u32() {
        if (!remaining(4)) { bad = true; return 0u; }
        std::uint32_t v = 0u;
        for (int i = 0; i < 4; ++i) v |= (static_cast<std::uint32_t>(b[pos + i]) << (8 * i));
        pos += 4;
        return v;
    }
    std::uint64_t u64() {
        if (!remaining(8)) { bad = true; return 0u; }
        std::uint64_t v = 0u;
        for (int i = 0; i < 8; ++i) v |= (static_cast<std::uint64_t>(b[pos + i]) << (8 * i));
        pos += 8;
        return v;
    }
    std::string str() {
        const std::uint16_t n = u16();
        if (bad || n > kMaxString || !remaining(n)) { bad = true; return {}; }
        std::string s;
        s.reserve(n);
        for (std::uint16_t i = 0; i < n; ++i) s.push_back(static_cast<char>(b[pos++]));
        return s;
    }
};

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

void write_device(Writer& w, const DeviceRecord& d) {
    w.u64(d.id.value());
    w.str(d.logical_name);
    w.u8(static_cast<std::uint8_t>(d.kind));
    w.str(d.backend);
    w.str(d.backend_instance);
    w.str(d.pci_identity);
    w.u8(d.cuda_ordinal.has_value() ? 1u : 0u);
    if (d.cuda_ordinal.has_value()) w.u32(*d.cuda_ordinal);
    w.str(d.vendor);
    w.str(d.architecture);
    w.str(d.device_generation);
    w.u8(static_cast<std::uint8_t>(d.capability.interconnect));
    w.u32(d.capability.link_count);
    w.u32(d.capability.nominal_lane_width);
    w.u64(d.capability.nominal_bandwidth_bps);
    w.u8(d.capability.peer_access_supported ? 1u : 0u);
    w.u8(static_cast<std::uint8_t>(d.capability.measurement_support));
    w.u32(d.capability.supported_counters);
    w.u8(d.capability.telemetry_supported ? 1u : 0u);
    w.u8(static_cast<std::uint8_t>(d.health));
    w.u8(static_cast<std::uint8_t>(d.evidence.evidence_class));
    w.u32(static_cast<std::uint32_t>(d.evidence.provenance));
    w.str(d.evidence.source);
    w.str(d.evidence.note);
    w.u64(d.generation.value());
    w.u8(d.owner_worker.has_value() ? 1u : 0u);
    if (d.owner_worker.has_value()) w.u64(d.owner_worker->value());
    w.u8(d.owner_boot.has_value() ? 1u : 0u);
    if (d.owner_boot.has_value()) w.u64(d.owner_boot->value());
}

void read_device(Reader& r, DeviceRecord& d) {
    d.id = DeviceId(r.u64());
    d.logical_name = r.str();
    d.kind = static_cast<NodeKind>(r.u8());
    d.backend = r.str();
    d.backend_instance = r.str();
    d.pci_identity = r.str();
    if (r.u8() != 0u) d.cuda_ordinal = r.u32();
    d.vendor = r.str();
    d.architecture = r.str();
    d.device_generation = r.str();
    d.capability.interconnect = static_cast<InterconnectTechnology>(r.u8());
    d.capability.link_count = r.u32();
    d.capability.nominal_lane_width = r.u32();
    d.capability.nominal_bandwidth_bps = r.u64();
    d.capability.peer_access_supported = r.u8() != 0u;
    d.capability.measurement_support = static_cast<MeasurementSupport>(r.u8());
    d.capability.supported_counters = r.u32();
    d.capability.telemetry_supported = r.u8() != 0u;
    d.health = static_cast<DeviceHealthState>(r.u8());
    d.evidence.evidence_class = static_cast<EvidenceClass>(r.u8());
    d.evidence.provenance = static_cast<Provenance>(r.u32());
    d.evidence.source = r.str();
    d.evidence.note = r.str();
    d.generation = DeviceGeneration(r.u64());
    if (r.u8() != 0u) d.owner_worker = WorkerId(r.u64());
    if (r.u8() != 0u) d.owner_boot = WorkerBootId(r.u64());
}

void write_link(Writer& w, const LinkRecord& l) {
    w.u64(l.id.value());
    w.u64(l.source.value());
    w.u64(l.target.value());
    w.u8(l.directed ? 1u : 0u);
    w.str(l.physical_identifier);
    w.u8(static_cast<std::uint8_t>(l.state));
    w.u8(static_cast<std::uint8_t>(l.technology));
    w.u8(static_cast<std::uint8_t>(l.capability.interconnect));
    w.u32(l.capability.link_count);
    w.u32(l.capability.nominal_lane_width);
    w.u64(l.capability.nominal_bandwidth_bps);
    w.u8(l.capability.peer_access_supported ? 1u : 0u);
    w.u8(static_cast<std::uint8_t>(l.capability.measurement_support));
    w.u32(l.capability.supported_counters);
    w.u8(l.capability.telemetry_supported ? 1u : 0u);
    w.u64(l.generation.value());
    w.u8(static_cast<std::uint8_t>(l.evidence.evidence_class));
    w.u32(static_cast<std::uint32_t>(l.evidence.provenance));
    w.str(l.evidence.source);
    w.str(l.evidence.note);
    w.u64(static_cast<std::uint64_t>(l.last_observation.unix_ns));
    w.u8(l.last_observation.valid ? 1u : 0u);
    w.u8(l.measurement_supported ? 1u : 0u);
    w.u8(l.degraded ? 1u : 0u);
    w.str(l.degradation_note);
    w.u64(l.topology_generation.value());
}

void read_link(Reader& r, LinkRecord& l) {
    l.id = LinkId(r.u64());
    l.source = DeviceId(r.u64());
    l.target = DeviceId(r.u64());
    l.directed = r.u8() != 0u;
    l.physical_identifier = r.str();
    l.state = static_cast<LinkOperationalState>(r.u8());
    l.technology = static_cast<InterconnectTechnology>(r.u8());
    l.capability.interconnect = static_cast<InterconnectTechnology>(r.u8());
    l.capability.link_count = r.u32();
    l.capability.nominal_lane_width = r.u32();
    l.capability.nominal_bandwidth_bps = r.u64();
    l.capability.peer_access_supported = r.u8() != 0u;
    l.capability.measurement_support = static_cast<MeasurementSupport>(r.u8());
    l.capability.supported_counters = r.u32();
    l.capability.telemetry_supported = r.u8() != 0u;
    l.generation = LinkGeneration(r.u64());
    l.evidence.evidence_class = static_cast<EvidenceClass>(r.u8());
    l.evidence.provenance = static_cast<Provenance>(r.u32());
    l.evidence.source = r.str();
    l.evidence.note = r.str();
    l.last_observation = Timestamp(r.u64(), r.u8() != 0u);
    l.measurement_supported = r.u8() != 0u;
    l.degraded = r.u8() != 0u;
    l.degradation_note = r.str();
    l.topology_generation = TopologyGeneration(r.u64());
}

void write_durable(Writer& w, const DurableState& s) {
    w.u64(s.topology_generation.value());
    w.u64(static_cast<std::uint64_t>(s.saved_at.unix_ns));
    w.u8(s.saved_at.valid ? 1u : 0u);
    const std::uint32_t nd = static_cast<std::uint32_t>(s.devices.size());
    const std::uint32_t nl = static_cast<std::uint32_t>(s.links.size());
    w.u32(nd);
    for (const auto& d : s.devices) write_device(w, d);
    w.u32(nl);
    for (const auto& l : s.links) write_link(w, l);
}

}  // namespace

Result<Serialized> serialize_durable_state(const DurableState& state) {
    Serialized out;
    out.format_version = kVersion;
    Writer w{out.bytes};
    w.u32(kMagic);
    w.u32(kVersion);
    w.u32(0u);  // payload length placeholder
    const std::size_t len_pos = out.bytes.size() - 4u;
    write_durable(w, state);
    const std::uint32_t payload_len = static_cast<std::uint32_t>(out.bytes.size() - (len_pos + 4u));
    // patch length
    for (int i = 0; i < 4; ++i) out.bytes[len_pos + i] = static_cast<std::uint8_t>((payload_len >> (8 * i)) & 0xFFu);
    const std::uint32_t crc = crc32(out.bytes.data(), out.bytes.size());
    w.u32(crc);
    return Result<Serialized>::ok(std::move(out));
}

Result<DurableState> deserialize_durable_state(const Serialized& serialized) {
    if (serialized.format_version != kVersion)
        return Result<DurableState>::err(ErrorCode::INTEGRITY_FAILURE, "unsupported serialized version");
    if (serialized.bytes.size() < 4u + 4u + 4u + 4u)
        return Result<DurableState>::err(ErrorCode::INTEGRITY_FAILURE, "frame too short");
    Reader r{serialized.bytes};
    const std::uint32_t magic = r.u32();
    const std::uint32_t version = r.u32();
    const std::uint32_t payload_len = r.u32();
    if (magic != kMagic)
        return Result<DurableState>::err(ErrorCode::INTEGRITY_FAILURE, "bad magic");
    if (version != kVersion)
        return Result<DurableState>::err(ErrorCode::INTEGRITY_FAILURE, "unsupported version");
    if (!r.remaining(payload_len))
        return Result<DurableState>::err(ErrorCode::INTEGRITY_FAILURE, "truncated payload");
    if (!r.remaining(4u))
        return Result<DurableState>::err(ErrorCode::INTEGRITY_FAILURE, "missing checksum");

    DurableState st;
    const std::uint64_t topo = r.u64();
    st.topology_generation = TopologyGeneration(topo);
    st.saved_at = Timestamp(r.u64(), r.u8() != 0u);
    const std::uint32_t nd = r.u32();
    if (r.bad || nd > Limits{}.max_devices)
        return Result<DurableState>::err(ErrorCode::INTEGRITY_FAILURE, "impossible device count");
    st.devices.reserve(nd);
    for (std::uint32_t i = 0; i < nd; ++i) {
        DeviceRecord d;
        read_device(r, d);
        if (r.bad) return Result<DurableState>::err(ErrorCode::INTEGRITY_FAILURE, "corrupt device record");
        st.devices.push_back(std::move(d));
    }
    const std::uint32_t nl = r.u32();
    if (r.bad || nl > Limits{}.max_links)
        return Result<DurableState>::err(ErrorCode::INTEGRITY_FAILURE, "impossible link count");
    st.links.reserve(nl);
    for (std::uint32_t i = 0; i < nl; ++i) {
        LinkRecord l;
        read_link(r, l);
        if (r.bad) return Result<DurableState>::err(ErrorCode::INTEGRITY_FAILURE, "corrupt link record");
        st.links.push_back(std::move(l));
    }

    // Verify checksum over the prefix [magic|version|len|payload).
    const std::uint32_t expected = crc32(serialized.bytes.data(),
                                         static_cast<std::size_t>(4u + 4u + 4u) + payload_len);
    std::uint32_t stored = 0u;
    const std::size_t cs_pos = static_cast<std::size_t>(4u + 4u + 4u) + payload_len;
    if (serialized.bytes.size() < cs_pos + 4u)
        return Result<DurableState>::err(ErrorCode::INTEGRITY_FAILURE, "missing checksum");
    for (int i = 0; i < 4; ++i)
        stored |= static_cast<std::uint32_t>(serialized.bytes[cs_pos + i]) << (8 * i);
    if (serialized.bytes.size() != cs_pos + 4u)
        return Result<DurableState>::err(ErrorCode::INTEGRITY_FAILURE, "trailing garbage after frame");
    if (stored != expected)
        return Result<DurableState>::err(ErrorCode::INTEGRITY_FAILURE, "checksum mismatch (corrupt frame)");
    if (r.pos != cs_pos)
        return Result<DurableState>::err(ErrorCode::INTEGRITY_FAILURE, "payload length inconsistent");
    return Result<DurableState>::ok(std::move(st));
}

}  // namespace nvlinkfabric
