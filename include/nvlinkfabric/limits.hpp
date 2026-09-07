#pragma once
#include <cstddef>
#include <cstdint>

namespace nvlinkfabric {

//---------------------------------------------------------------------------
// Explicit resource bounds. Path enumeration, history retention, and
// serialized state are all bounded so hostile or malformed inputs cannot
// exhaust resources. Checked arithmetic is used on attacker-controlled counts.
//---------------------------------------------------------------------------
struct Limits {
    std::size_t max_devices{256u};
    std::size_t max_links{1024u};
    std::size_t max_parallel_links_per_pair{16u};
    std::size_t max_path_depth{16u};
    std::size_t max_candidate_paths{256u};
    std::size_t max_measurement_history{1024u};
    std::size_t max_observations{4096u};
    std::size_t max_persisted_state_bytes{64u * 1024u * 1024u};  // 64 MiB
    std::uint32_t max_frame_bytes{16u * 1024u * 1024u};          // protocol frame
    std::size_t max_workers{32u};
    std::size_t max_tcp_clients{64u};
    std::size_t max_queued_messages{256u};
};

}  // namespace nvlinkfabric
