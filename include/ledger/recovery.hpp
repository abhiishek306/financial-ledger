#pragma once

#include <cstdint>
#include <string>

#include "ledger/ledger_state.hpp"
#include "ledger/result.hpp"

// Phase 4: crash recovery. Loads the most recent snapshot (if any), replays
// the WAL forward from that point, stops at the first corrupted/torn record
// (truncating the WAL back to the last valid boundary), and finally verifies
// the restored state satisfies the global trial-balance invariant before the
// engine is allowed to resume serving traffic.

namespace ledger::recovery {

struct RecoveredState {
    LedgerState state;
    // Byte length the WAL should be (and was truncated to, if necessary) so
    // the writer can resume appending immediately after the last valid record.
    std::uint64_t wal_valid_length{0};
};

[[nodiscard]] Result<RecoveredState> recover(const std::string& wal_path, const std::string& snapshot_path);

} // namespace ledger::recovery
