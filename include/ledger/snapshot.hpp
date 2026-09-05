#pragma once

#include <cstdint>
#include <string>

#include "ledger/ledger_state.hpp"
#include "ledger/result.hpp"

// Phase 4/5: point-in-time state snapshotting. A snapshot captures every
// account's balance plus the id of the last transaction applied and the WAL
// byte offset it corresponds to, so recovery can replay only the WAL tail
// written after the snapshot (deterministic WAL truncation/compaction).

namespace ledger::snapshot {

struct SnapshotHeader {
    std::uint32_t magic;
    std::uint16_t version;
    std::uint16_t reserved;
    std::uint32_t account_count;
    std::uint64_t last_applied_tx_id;
    std::uint64_t wal_offset;
};

inline constexpr std::uint32_t kMagic = 0x534E4150u; // "SNAP"
inline constexpr std::uint16_t kVersion = 1;

struct LoadedSnapshot {
    LedgerState state;
    std::uint64_t wal_offset{0};
};

// Synchronously serializes `state` (as of `wal_offset` bytes into the WAL) to
// `path` via a temp file + atomic rename, so a crash mid-write can never
// leave a corrupt/partial snapshot visible at `path`.
[[nodiscard]] Result<void> write_snapshot(const std::string& path, const LedgerState& state, std::uint64_t wal_offset);

// Spawns a detached background thread that snapshots a copy of `state`
// (taken at call time) to `path`. Fire-and-forget: does not block the
// caller / writer thread beyond the cost of copying the state.
void write_snapshot_async(const std::string& path, LedgerState state, std::uint64_t wal_offset);

// Loads and CRC-validates a snapshot file previously written by
// write_snapshot(). Returns SnapshotReadFailed if the file does not exist,
// and SnapshotCorrupt if it exists but fails CRC/shape validation.
[[nodiscard]] Result<LoadedSnapshot> load_snapshot(const std::string& path);

} // namespace ledger::snapshot
