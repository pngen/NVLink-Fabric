#include "nvlinkfabric/runtime.hpp"
#include "nvlinkfabric/protocol.hpp"
#include "nvlinkfabric/identities.hpp"
#include "nvlinkfabric/enums.hpp"
#include "nvlinkfabric/serialization.hpp"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <process.h>
#include <windows.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <signal.h>
#endif

#include <algorithm>
#include <atomic>
#include <map>
#include <mutex>
#include <thread>
#include <vector>

namespace nvlinkfabric {

using proto::MessageType;
using proto::Frame;

namespace {

#ifdef _WIN32
using Sock = SOCKET;
constexpr Sock kInvalid = INVALID_SOCKET;
#else
using Sock = int;
constexpr Sock kInvalid = -1;
#endif

std::once_flag g_winsock_once;
void ensure_net() {
#ifdef _WIN32
    std::call_once(g_winsock_once, []() { WSADATA d; WSAStartup(MAKEWORD(2, 2), &d); });
#else
    (void)0;
#endif
}

bool send_all(Sock s, const std::vector<std::uint8_t>& bytes) {
#ifdef _WIN32
    std::size_t off = 0u;
    while (off < bytes.size()) {
        const int n = static_cast<int>(std::min<std::size_t>(bytes.size() - off, 1u << 20));
        const int sent = ::send(s, reinterpret_cast<const char*>(bytes.data() + off), n, 0);
        if (sent <= 0) return false;
        off += static_cast<std::size_t>(sent);
    }
    return true;
#else
    std::size_t off = 0u;
    while (off < bytes.size()) {
        const ssize_t sent = ::send(s, bytes.data() + off, bytes.size() - off, 0);
        if (sent <= 0) return false;
        off += static_cast<std::size_t>(sent);
    }
    return true;
#endif
}

// Reads exactly n bytes. Returns false on EOF/error.
bool recv_exact(Sock s, std::uint8_t* buf, std::size_t n) {
    std::size_t off = 0u;
    while (off < n) {
#ifdef _WIN32
        const int r = ::recv(s, reinterpret_cast<char*>(buf + off), static_cast<int>(n - off), 0);
#else
        const ssize_t r = ::recv(s, buf + off, n - off, 0);
#endif
        if (r <= 0) return false;
        off += static_cast<std::size_t>(r);
    }
    return true;
}

// Reads one frame (header + payload + crc) from the socket.
Result<Frame> recv_frame(Sock s, std::size_t max_bytes) {
    std::vector<std::uint8_t> header(proto::kHeaderNoPayload);
    if (!recv_exact(s, header.data(), header.size()))
        return Result<Frame>::err(ErrorCode::PROTOCOL_ERROR, "connection closed during header");
    // payload len occupies bytes [6..9] (after magic(4)+version(1)+type(1)).
    const std::uint32_t payload_len = static_cast<std::uint32_t>(header[6]) |
                                      (static_cast<std::uint32_t>(header[7]) << 8) |
                                      (static_cast<std::uint32_t>(header[8]) << 16) |
                                      (static_cast<std::uint32_t>(header[9]) << 24);
    if (payload_len > max_bytes - proto::kHeaderNoPayload - proto::kCrcBytes)
        return Result<Frame>::err(ErrorCode::RESOURCE_LIMIT, "frame too large");
    std::vector<std::uint8_t> full(header.size() + payload_len + proto::kCrcBytes);
    std::copy(header.begin(), header.end(), full.begin());
    if (!recv_exact(s, full.data() + header.size(), payload_len + proto::kCrcBytes))
        return Result<Frame>::err(ErrorCode::PROTOCOL_ERROR, "connection closed during payload");
    return proto::decode_frame(full, max_bytes);
}

bool send_frame(Sock s, const Frame& f, std::size_t max_bytes) {
    auto enc = proto::encode_frame(f, max_bytes);
    if (!enc.has_value()) return false;
    return send_all(s, *enc);
}

std::string sock_error() {
#ifdef _WIN32
    return "winsock error " + std::to_string(WSAGetLastError());
#else
    return "socket error";
#endif
}

// ----- compact payload codecs for worker evidence ---------
void enc_device(std::vector<std::uint8_t>& b, const DeviceRecord& d) {
    proto::put_u64(b, d.id.value());
    proto::put_str(b, d.logical_name);
    proto::put_str(b, d.architecture);
    proto::put_str(b, d.device_generation);
    proto::put_u8(b, static_cast<std::uint8_t>(d.kind));
    proto::put_u8(b, static_cast<std::uint8_t>(d.health));
    proto::put_u8(b, static_cast<std::uint8_t>(d.evidence.evidence_class));
    proto::put_u32(b, static_cast<std::uint32_t>(d.evidence.provenance));
    proto::put_u64(b, d.generation.value());
    proto::put_u64(b, d.capability.nominal_bandwidth_bps);
    proto::put_u8(b, d.capability.peer_access_supported ? 1u : 0u);
    proto::put_str(b, d.pci_identity);
}
DeviceRecord dec_device(proto::PayloadReader& r) {
    DeviceRecord d;
    d.id = DeviceId(r.u64());
    d.logical_name = r.str();
    d.architecture = r.str();
    d.device_generation = r.str();
    d.kind = static_cast<NodeKind>(r.u8());
    d.health = static_cast<DeviceHealthState>(r.u8());
    d.evidence.evidence_class = static_cast<EvidenceClass>(r.u8());
    d.evidence.provenance = static_cast<Provenance>(r.u32());
    d.evidence.source = "worker";
    d.generation = DeviceGeneration(r.u64());
    d.capability.nominal_bandwidth_bps = r.u64();
    d.capability.peer_access_supported = r.u8() != 0u;
    d.pci_identity = r.str();
    d.vendor = "worker";
    d.backend = "worker";
    d.backend_instance = "worker";
    return d;
}
void enc_link(std::vector<std::uint8_t>& b, const LinkRecord& l) {
    proto::put_u64(b, l.id.value());
    proto::put_u64(b, l.source.value());
    proto::put_u64(b, l.target.value());
    proto::put_u8(b, l.directed ? 1u : 0u);
    proto::put_u8(b, static_cast<std::uint8_t>(l.state));
    proto::put_u8(b, static_cast<std::uint8_t>(l.technology));
    proto::put_u64(b, l.capability.nominal_bandwidth_bps);
    proto::put_u64(b, l.generation.value());
    proto::put_str(b, l.physical_identifier);
}
LinkRecord dec_link(proto::PayloadReader& r) {
    LinkRecord l;
    l.id = LinkId(r.u64());
    l.source = DeviceId(r.u64());
    l.target = DeviceId(r.u64());
    l.directed = r.u8() != 0u;
    l.state = static_cast<LinkOperationalState>(r.u8());
    l.technology = static_cast<InterconnectTechnology>(r.u8());
    l.capability.nominal_bandwidth_bps = r.u64();
    l.generation = LinkGeneration(r.u64());
    l.physical_identifier = r.str();
    l.evidence = Evidence{EvidenceClass::SYNTHETIC, Provenance::SYNTHETIC_FIXTURE, "worker", "worker-published"};
    l.last_observation = now_timestamp();
    return l;
}
void enc_meas(std::vector<std::uint8_t>& b, const MeasurementRecord& m) {
    proto::put_u64(b, m.id.value());
    proto::put_u64(b, m.source.value());
    proto::put_u64(b, m.target.value());
    proto::put_u64(b, static_cast<std::uint64_t>(m.bandwidth_bps));
    proto::put_u8(b, m.valid ? 1u : 0u);
    proto::put_u8(b, static_cast<std::uint8_t>(m.evidence.evidence_class));
    proto::put_u64(b, m.payload_bytes);
}
MeasurementRecord dec_meas(proto::PayloadReader& r) {
    MeasurementRecord m;
    m.id = MeasurementId(r.u64());
    m.source = DeviceId(r.u64());
    m.target = DeviceId(r.u64());
    m.bandwidth_bps = static_cast<double>(r.u64());
    m.valid = r.u8() != 0u;
    m.evidence.evidence_class = static_cast<EvidenceClass>(r.u8());
    m.payload_bytes = r.u64();
    m.evidence.source = "worker";
    m.measured_at = now_timestamp();
    return m;
}

}  // namespace

// ---------------------------------------------------------------------------
// CoordinatorServer implementation
// ---------------------------------------------------------------------------
struct CoordinatorServer::Impl {
    Sock listener{kInvalid};
    std::atomic<bool> running{false};
    std::thread accept_thread;
    std::mutex mu;
    std::vector<std::thread> conn_threads;
    std::map<WorkerId, WorkerBootId> last_boot;
    std::size_t live_count{0u};
};

CoordinatorServer::CoordinatorServer(Limits limits) : impl_(new Impl), registry_(limits) {
    epoch_ = fresh_generation<CoordinatorEpochTag>();
}

CoordinatorServer::~CoordinatorServer() {
    stop();
}

Result<std::uint16_t> CoordinatorServer::start(std::uint16_t port) {
    ensure_net();
#ifdef _WIN32
    impl_->listener = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
#else
    impl_->listener = ::socket(AF_INET, SOCK_STREAM, 0);
#endif
    if (impl_->listener == kInvalid)
        return Result<std::uint16_t>::err(ErrorCode::BACKEND_ERROR, "socket() failed: " + sock_error());

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(port);
#ifdef _WIN32
    if (::bind(impl_->listener, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
#else
    if (::bind(impl_->listener, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
#endif
        return Result<std::uint16_t>::err(ErrorCode::BACKEND_ERROR, "bind() failed: " + sock_error());
    }
    if (::listen(impl_->listener, 8) != 0)
        return Result<std::uint16_t>::err(ErrorCode::BACKEND_ERROR, "listen() failed: " + sock_error());

    // Resolve the actual bound port (useful when port==0).
    int len = static_cast<int>(sizeof(addr));
#ifdef _WIN32
    if (::getsockname(impl_->listener, reinterpret_cast<sockaddr*>(&addr), &len) != 0)
#else
    if (::getsockname(impl_->listener, reinterpret_cast<sockaddr*>(&addr), &len) != 0)
#endif
        return Result<std::uint16_t>::err(ErrorCode::BACKEND_ERROR, "getsockname() failed");
    port_ = ntohs(addr.sin_port);

    impl_->running.store(true);
    impl_->accept_thread = std::thread([this]() {
        while (impl_->running.load()) {
            // Poll-select so the loop can observe shutdown without relying on
            // closesocket() interrupting a blocked accept() on Windows.
            fd_set rfds;
            FD_ZERO(&rfds);
            FD_SET(impl_->listener, &rfds);
            timeval tv{};
            tv.tv_sec = 0;
            tv.tv_usec = 100000;  // 100 ms poll
            const int sel = ::select(0, &rfds, nullptr, nullptr, &tv);
            if (sel <= 0) continue;  // timeout or error; re-check running
            Sock client = ::accept(impl_->listener, nullptr, nullptr);
            if (client == kInvalid) {
                if (impl_->running.load()) continue;
                break;
            }
            std::lock_guard<std::mutex> lk(impl_->mu);
            impl_->conn_threads.emplace_back([this, client]() { handle_connection(static_cast<std::uintptr_t>(client)); });
        }
    });
    return Result<std::uint16_t>::ok(port_);
}

void CoordinatorServer::stop() {
    if (!impl_) return;
    impl_->running.store(false);
#ifdef _WIN32
    if (impl_->listener != kInvalid) { ::closesocket(impl_->listener); impl_->listener = kInvalid; }
#else
    if (impl_->listener != kInvalid) { ::close(impl_->listener); impl_->listener = kInvalid; }
#endif
    if (impl_->accept_thread.joinable()) impl_->accept_thread.join();
    std::vector<std::thread> live;
    {
        std::lock_guard<std::mutex> lk(impl_->mu);
        live.swap(impl_->conn_threads);
    }
    for (auto& t : live) if (t.joinable()) t.join();
}

std::size_t CoordinatorServer::live_worker_count() const {
    std::lock_guard<std::mutex> lk(impl_->mu);
    return impl_->live_count;
}

void CoordinatorServer::handle_connection(std::uintptr_t sock_bits) {
    Sock client = static_cast<Sock>(sock_bits);
    static constexpr std::size_t kMax = 16u * 1024u * 1024u;
    // Step 1: HELLO.
    auto hello = recv_frame(client, kMax);
    if (!hello.has_value() || hello->type != MessageType::HELLO) {
#ifdef _WIN32
        ::closesocket(client);
#else
        ::close(client);
#endif
        return;
    }
    proto::PayloadReader pr(hello->payload);
    const WorkerId wid = WorkerId(pr.u64());
    const WorkerBootId boot = WorkerBootId(pr.u64());
    const CoordinatorEpoch reported_epoch = CoordinatorEpoch(pr.u64());
    if (pr.bad) {
#ifdef _WIN32
        ::closesocket(client); 
#else
        ::close(client);
#endif
        return;
    }

    // Validate epoch and boot authority.
    bool accepted = false;
    {
        std::lock_guard<std::mutex> lk(impl_->mu);
        if (reported_epoch != epoch_) {
            accepted = false;
        } else {
            auto it = impl_->last_boot.find(wid);
            if (it == impl_->last_boot.end() || boot > it->second) {
                impl_->last_boot[wid] = boot;
                ++impl_->live_count;
                accepted = true;
            } else {
                accepted = false;  // stale boot replay
            }
        }
    }

    Frame reply;
    reply.type = accepted ? MessageType::READY : MessageType::GOODBYE;
    if (!accepted) {
        const std::string note =
            (reported_epoch != epoch_) ? "stale coordinator epoch" : "stale worker boot identity";
        std::vector<std::uint8_t> nb;
        proto::put_str(nb, note);
        reply.payload = nb;
    } else {
        std::vector<std::uint8_t> nb;
        proto::put_u64(nb, epoch_.value());
        reply.payload = nb;
    }
    if (!send_frame(client, reply, kMax)) {
#ifdef _WIN32
        ::closesocket(client);
#else
        ::close(client);
#endif
        return;
    }
    if (!accepted) {
#ifdef _WIN32
        ::closesocket(client);
#else
        ::close(client);
#endif
        return;
    }

    // Step 2: read publish/query frames until disconnect.
    for (;;) {
        auto fr = recv_frame(client, kMax);
        if (!fr.has_value()) break;  // connection closed (worker death)
        const Frame& f = *fr;
        switch (f.type) {
            case MessageType::PUBLISH_DEVICE: {
                proto::PayloadReader prd(f.payload);
                DeviceRecord d = dec_device(prd);
                d.owner_worker = wid;
                d.owner_boot = boot;
                registry_.upsert_device(std::move(d));
                break;
            }
            case MessageType::PUBLISH_LINK: {
                proto::PayloadReader prl(f.payload);
                LinkRecord l = dec_link(prl);
                registry_.upsert_link(std::move(l));
                break;
            }
            case MessageType::PUBLISH_MEASUREMENT: {
                proto::PayloadReader prm(f.payload);
                MeasurementRecord m = dec_meas(prm);
                m.owner_worker = wid;
                m.owner_boot = boot;
                registry_.publish_measurement(std::move(m));
                break;
            }
            case MessageType::ROUTE: {
                proto::PayloadReader qr(f.payload);
                const DeviceId src(qr.u64());
                const DeviceId tgt(qr.u64());
                if (qr.bad) break;
                auto dec = registry_.decide_route(src, tgt, RoutePolicy::default_policy());
                Frame resp;
                resp.type = MessageType::ROUTE;
                std::vector<std::uint8_t> nb;
                if (dec.has_value()) {
                    proto::put_u8(nb, static_cast<std::uint8_t>(dec->outcome));
                    proto::put_u64(nb, dec->selected.has_value() ? dec->selected->value() : 0ull);
                    proto::put_str(nb, dec->explanation);
                } else {
                    proto::put_u8(nb, static_cast<std::uint8_t>(RouteDecisionOutcome::NO_ROUTE));
                    proto::put_u64(nb, 0ull);
                    proto::put_str(nb, dec.error().to_string());
                }
                resp.payload = nb;
                send_frame(client, resp, kMax);
                break;
            }
            case MessageType::HEARTBEAT:
            case MessageType::READY:
            default:
                // ignore
                break;
        }
    }

    // Worker disconnected (real process death): mark dynamic evidence stale and
    // reduce the live count. last_boot is retained so a stale-boot replay is
    // still rejected.
    registry_.mark_dynamic_evidence_revalidation_required();
    {
        std::lock_guard<std::mutex> lk(impl_->mu);
        if (impl_->live_count > 0u) --impl_->live_count;
    }
#ifdef _WIN32
    ::closesocket(client);
#else
    ::close(client);
#endif
}
// ---------------------------------------------------------------------------
// WorkerClient implementation
// ---------------------------------------------------------------------------
WorkerClient::WorkerClient(std::uint16_t port) : port_(port), sock_(0u) {}
WorkerClient::~WorkerClient() { disconnect(); }

Result<void> WorkerClient::connect(const WorkerDescriptor& desc) {
    ensure_net();
    Sock s;
#ifdef _WIN32
    s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
#else
    s = ::socket(AF_INET, SOCK_STREAM, 0);
#endif
    if (s == kInvalid)
        return Result<void>::err(ErrorCode::BACKEND_ERROR, "worker socket() failed: " + sock_error());
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(port_);
    int r = ::connect(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    if (r != 0) {
#ifdef _WIN32
        ::closesocket(s);
#else
        ::close(s);
#endif
        return Result<void>::err(ErrorCode::BACKEND_ERROR, "worker connect() failed: " + sock_error());
    }
    sock_ = static_cast<std::uintptr_t>(s);

    std::vector<std::uint8_t> pb;
    proto::put_u64(pb, desc.worker.value());
    proto::put_u64(pb, desc.boot.value());
    proto::put_u64(pb, desc.epoch.value());
    Frame hello;
    hello.type = MessageType::HELLO;
    hello.payload = std::move(pb);
    if (!send_frame(s, hello, 16u * 1024u * 1024u))
        return Result<void>::err(ErrorCode::PROTOCOL_ERROR, "worker failed to send HELLO");

    auto reply = recv_frame(s, 16u * 1024u * 1024u);
    if (!reply.has_value())
        return Result<void>::err(ErrorCode::PROTOCOL_ERROR, "worker got no HELLO reply");
    if (reply->type == MessageType::GOODBYE) {
        proto::PayloadReader pr(reply->payload);
        std::string note = pr.str();
        const ErrorCode code = (note.find("epoch") != std::string::npos)
                                   ? ErrorCode::STALE_EPOCH
                                   : ErrorCode::STALE_BOOT;
        return Result<void>::err(code, note.empty() ? "worker rejected" : note);
    }
    if (reply->type != MessageType::READY)
        return Result<void>::err(ErrorCode::PROTOCOL_ERROR, "unexpected HELLO reply");
    return Result<void>::ok();
}

Result<void> WorkerClient::publish_device(const DeviceRecord& device) {
    if (sock_ == 0u) return Result<void>::err(ErrorCode::NOT_FOUND, "not connected");
    std::vector<std::uint8_t> pb;
    enc_device(pb, device);
    Frame f;
    f.type = MessageType::PUBLISH_DEVICE;
    f.payload = std::move(pb);
    if (!send_frame(static_cast<Sock>(sock_), f, 16u * 1024u * 1024u))
        return Result<void>::err(ErrorCode::PROTOCOL_ERROR, "failed to publish device");
    return Result<void>::ok();
}
Result<void> WorkerClient::publish_link(const LinkRecord& link) {
    if (sock_ == 0u) return Result<void>::err(ErrorCode::NOT_FOUND, "not connected");
    std::vector<std::uint8_t> pb;
    enc_link(pb, link);
    Frame f;
    f.type = MessageType::PUBLISH_LINK;
    f.payload = std::move(pb);
    if (!send_frame(static_cast<Sock>(sock_), f, 16u * 1024u * 1024u))
        return Result<void>::err(ErrorCode::PROTOCOL_ERROR, "failed to publish link");
    return Result<void>::ok();
}
Result<void> WorkerClient::publish_measurement(const MeasurementRecord& measurement) {
    if (sock_ == 0u) return Result<void>::err(ErrorCode::NOT_FOUND, "not connected");
    std::vector<std::uint8_t> pb;
    enc_meas(pb, measurement);
    Frame f;
    f.type = MessageType::PUBLISH_MEASUREMENT;
    f.payload = std::move(pb);
    if (!send_frame(static_cast<Sock>(sock_), f, 16u * 1024u * 1024u))
        return Result<void>::err(ErrorCode::PROTOCOL_ERROR, "failed to publish measurement");
    return Result<void>::ok();
}
Result<void> WorkerClient::signal_ready() {
    if (sock_ == 0u) return Result<void>::err(ErrorCode::NOT_FOUND, "not connected");
    Frame f;
    f.type = MessageType::READY;
    if (!send_frame(static_cast<Sock>(sock_), f, 16u * 1024u * 1024u))
        return Result<void>::err(ErrorCode::PROTOCOL_ERROR, "failed to signal ready");
    return Result<void>::ok();
}
Result<void> WorkerClient::heartbeat() {
    if (sock_ == 0u) return Result<void>::err(ErrorCode::NOT_FOUND, "not connected");
    Frame f;
    f.type = MessageType::HEARTBEAT;
    if (!send_frame(static_cast<Sock>(sock_), f, 16u * 1024u * 1024u))
        return Result<void>::err(ErrorCode::PROTOCOL_ERROR, "failed to send heartbeat");
    return Result<void>::ok();
}
Result<RouteDecision> WorkerClient::request_route(DeviceId source, DeviceId target, const RoutePolicy&) {
    if (sock_ == 0u) return Result<RouteDecision>::err(ErrorCode::NOT_FOUND, "not connected");
    std::vector<std::uint8_t> pb;
    proto::put_u64(pb, source.value());
    proto::put_u64(pb, target.value());
    Frame f;
    f.type = MessageType::ROUTE;
    f.payload = std::move(pb);
    Sock s = static_cast<Sock>(sock_);
    if (!send_frame(s, f, 16u * 1024u * 1024u))
        return Result<RouteDecision>::err(ErrorCode::PROTOCOL_ERROR, "failed to send route request");
    auto reply = recv_frame(s, 16u * 1024u * 1024u);
    if (!reply.has_value() || reply->type != MessageType::ROUTE)
        return Result<RouteDecision>::err(ErrorCode::PROTOCOL_ERROR, "bad route reply");
    proto::PayloadReader pr(reply->payload);
    RouteDecision d;
    d.outcome = static_cast<RouteDecisionOutcome>(pr.u8());
    const std::uint64_t sel = pr.u64();
    if (sel != 0ull) d.selected = PathId(sel);
    d.explanation = pr.str();
    return Result<RouteDecision>::ok(std::move(d));
}
void WorkerClient::disconnect() {
    if (sock_ != 0u) {
        Sock s = static_cast<Sock>(sock_);
#ifdef _WIN32
        ::closesocket(s);
#else
        ::close(s);
#endif
        sock_ = 0u;
    }
}

// ---------------------------------------------------------------------------
// Process helpers (real OS process spawn / terminate)
// ---------------------------------------------------------------------------
Result<WorkerProcessHandle> spawn_worker(const WorkerProcessSpec& spec) {
#ifdef _WIN32
    std::string cmdline = "\"" + spec.worker_exe + "\" " +
        std::to_string(spec.port) + " " +
        std::to_string(spec.worker_id.value()) + " " +
        std::to_string(spec.boot.value()) + " " +
        std::to_string(spec.epoch.value());
    const int n = MultiByteToWideChar(CP_UTF8, 0, cmdline.c_str(), -1, nullptr, 0);
    std::wstring wcmdline(static_cast<std::size_t>(n > 0 ? n : 1), L'\0');
    if (n > 0) MultiByteToWideChar(CP_UTF8, 0, cmdline.c_str(), -1, &wcmdline[0], n);
    std::wstring wexe;
    {
        const int m = MultiByteToWideChar(CP_UTF8, 0, spec.worker_exe.c_str(), -1, nullptr, 0);
        wexe.resize(static_cast<std::size_t>(m > 0 ? m : 1));
        if (m > 0) MultiByteToWideChar(CP_UTF8, 0, spec.worker_exe.c_str(), -1, &wexe[0], m);
    }

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    // writable command line buffer
    std::vector<wchar_t> mutable_cmd(wcmdline.begin(), wcmdline.end());
    mutable_cmd.push_back(L'\0');
    BOOL created = CreateProcessW(
        wexe.c_str(), mutable_cmd.data(), nullptr, nullptr, FALSE,
        CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    if (!created)
        return Result<WorkerProcessHandle>::err(ErrorCode::BACKEND_ERROR, "CreateProcess failed");
    auto* pinfo = new PROCESS_INFORMATION(pi);
    WorkerProcessHandle h;
    h.handle = pinfo;
    return Result<WorkerProcessHandle>::ok(std::move(h));
#else
    (void)spec;
    return Result<WorkerProcessHandle>::err(ErrorCode::UNSUPPORTED, "process spawn unsupported here");
#endif
}

Result<void> kill_worker(WorkerProcessHandle& handle) {
#ifdef _WIN32
    if (handle.handle == nullptr) return Result<void>::err(ErrorCode::NOT_FOUND, "no worker process");
    auto* pinfo = static_cast<PROCESS_INFORMATION*>(handle.handle);
    TerminateProcess(pinfo->hProcess, 0);
    WaitForSingleObject(pinfo->hProcess, INFINITE);
    CloseHandle(pinfo->hProcess);
    CloseHandle(pinfo->hThread);
    delete pinfo;
    handle.handle = nullptr;
    return Result<void>::ok();
#else
    (void)handle;
    return Result<void>::err(ErrorCode::UNSUPPORTED, "process kill unsupported here");
#endif
}

Result<void> wait_worker(WorkerProcessHandle& handle) {
#ifdef _WIN32
    if (handle.handle == nullptr) return Result<void>::err(ErrorCode::NOT_FOUND, "no worker process");
    auto* pinfo = static_cast<PROCESS_INFORMATION*>(handle.handle);
    WaitForSingleObject(pinfo->hProcess, INFINITE);
    CloseHandle(pinfo->hProcess);
    CloseHandle(pinfo->hThread);
    delete pinfo;
    handle.handle = nullptr;
    return Result<void>::ok();
#else
    (void)handle;
    return Result<void>::err(ErrorCode::UNSUPPORTED, "process wait unsupported here");
#endif
}

}  // namespace nvlinkfabric

