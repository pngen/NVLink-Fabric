#pragma once
#include <cstdint>
#include <functional>
#include <atomic>
#include <string>

namespace nvlinkfabric {

//---------------------------------------------------------------------------
// Strongly-typed identity handles. Id<Tag> and Generation<Tag> are distinct
// types; a raw integer is never interchanged with an identity.
//---------------------------------------------------------------------------

template <class Tag>
class Id {
public:
    Id() noexcept = default;
    explicit Id(std::uint64_t v) noexcept : v_(v) {}
    std::uint64_t value() const noexcept { return v_; }
    bool is_nil() const noexcept { return v_ == 0u; }
    explicit operator bool() const noexcept { return !is_nil(); }
    std::string to_string() const { return std::to_string(v_); }

    friend bool operator==(Id a, Id b) noexcept { return a.v_ == b.v_; }
    friend bool operator!=(Id a, Id b) noexcept { return !(a == b); }
    friend bool operator<(Id a, Id b) noexcept { return a.v_ < b.v_; }
    friend bool operator>(Id a, Id b) noexcept { return b < a; }

private:
    std::uint64_t v_{0u};
};

//---------------------------------------------------------------------------
// Monotonic generation counter. A fresh generation is always strictly greater
// than any previously issued generation, so it can never move backward and
// stale replays are detectable by comparison.
//---------------------------------------------------------------------------
template <class Tag>
class Generation {
public:
    Generation() noexcept = default;
    explicit Generation(std::uint64_t v) noexcept : v_(v) {}
    std::uint64_t value() const noexcept { return v_; }
    bool valid() const noexcept { return v_ != 0u; }
    Generation next() const noexcept { return Generation(v_ + 1u); }
    std::string to_string() const { return std::to_string(v_); }

    friend bool operator==(Generation a, Generation b) noexcept { return a.v_ == b.v_; }
    friend bool operator!=(Generation a, Generation b) noexcept { return !(a == b); }
    friend bool operator<(Generation a, Generation b) noexcept { return a.v_ < b.v_; }
    friend bool operator>(Generation a, Generation b) noexcept { return b < a; }

private:
    std::uint64_t v_{0u};
};

//---------------------------------------------------------------------------
// Fresh identity / generation issuance. Values are drawn from monotonically
// increasing per-tag sequences; this makes fabric-wide comparisons safe.
//---------------------------------------------------------------------------
namespace detail {
template <class Tag>
std::atomic<std::uint64_t>& id_counter() {
    static std::atomic<std::uint64_t> counter{0u};
    return counter;
}
template <class Tag>
std::atomic<std::uint64_t>& generation_counter() {
    static std::atomic<std::uint64_t> counter{0u};
    return counter;
}
}  // namespace detail

template <class Tag>
Id<Tag> fresh_id() {
    const auto v = detail::id_counter<Tag>().fetch_add(1u, std::memory_order_relaxed) + 1u;
    return Id<Tag>(v);
}

template <class Tag>
Generation<Tag> fresh_generation() {
    const auto v = detail::generation_counter<Tag>().fetch_add(1u, std::memory_order_relaxed) + 1u;
    return Generation<Tag>(v);
}

//---------------------------------------------------------------------------
// Identity tags and public identity aliases.
//---------------------------------------------------------------------------
struct DeviceTag;
struct DeviceGenerationTag;
struct LinkTag;
struct LinkGenerationTag;
struct PathTag;
struct TopologyTag;
struct TopologySnapshotTag;
struct WorkerTag;
struct WorkerBootTag;
struct CoordinatorEpochTag;
struct ObservationTag;
struct MeasurementTag;
struct RouteDecisionTag;
struct RouteGenerationTag;
struct PolicyTag;

using DeviceId            = Id<DeviceTag>;
using DeviceGeneration    = Generation<DeviceGenerationTag>;
using LinkId              = Id<LinkTag>;
using LinkGeneration      = Generation<LinkGenerationTag>;
using PathId              = Id<PathTag>;
using TopologyGeneration  = Generation<TopologyTag>;
using TopologySnapshotId  = Id<TopologySnapshotTag>;
using WorkerId            = Id<WorkerTag>;
using WorkerBootId        = Id<WorkerBootTag>;
using CoordinatorEpoch    = Generation<CoordinatorEpochTag>;
using ObservationId       = Id<ObservationTag>;
using MeasurementId       = Id<MeasurementTag>;
using RouteDecisionId     = Id<RouteDecisionTag>;
using RouteGeneration     = Generation<RouteGenerationTag>;
using PolicyId            = Id<PolicyTag>;

}  // namespace nvlinkfabric

//---------------------------------------------------------------------------
// Hash support at namespace std scope (required for std::unordered_*).
//---------------------------------------------------------------------------
namespace std {
template <class Tag>
struct hash<nvlinkfabric::Id<Tag>> {
    std::size_t operator()(const nvlinkfabric::Id<Tag>& id) const noexcept {
        return std::hash<std::uint64_t>{}(id.value());
    }
};
template <class Tag>
struct hash<nvlinkfabric::Generation<Tag>> {
    std::size_t operator()(const nvlinkfabric::Generation<Tag>& g) const noexcept {
        return std::hash<std::uint64_t>{}(g.value());
    }
};
}  // namespace std
