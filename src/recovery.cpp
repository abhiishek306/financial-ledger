#include "ledger/recovery.hpp"

#include "ledger/snapshot.hpp"
#include "ledger/wal_reader.hpp"

namespace ledger::recovery {

Result<RecoveredState> recover(const std::string& wal_path, const std::string& snapshot_path) {
    RecoveredState recovered{};
    std::uint64_t replay_from_offset = 0;

    if (Result<snapshot::LoadedSnapshot> loaded = snapshot::load_snapshot(snapshot_path); loaded) {
        recovered.state = std::move(loaded.value().state);
        replay_from_offset = loaded.value().wal_offset;
    } else if (loaded.error() == LedgerError::SnapshotCorrupt) {
        // A corrupt snapshot is fatal: the WAL may already have been
        // truncated past what it covers, so there is no safe fallback.
        return loaded.error();
    }
    // SnapshotReadFailed (no snapshot file yet) is expected on first boot;
    // fall through with a fresh, empty state and full WAL replay.

    wal::BinaryWalReader reader;
    if (Result<void> opened = reader.open(wal_path); !opened) {
        // No WAL file at all: only acceptable if we also have no snapshot
        // offset to resume from (i.e. genuinely first boot).
        if (replay_from_offset != 0) return LedgerError::WalOpenFailed;
        recovered.wal_valid_length = 0;
        if (Result<void> trial = validate_trial_balance(recovered.state); !trial) return trial.error();
        return recovered;
    }

    reader.seek(replay_from_offset);

    for (;;) {
        wal::ReadResult read = reader.read_next();

        if (read.outcome == wal::ReadOutcome::Ok) {
            if (read.transaction.id > recovered.state.last_applied_id()) {
                (void)recovered.state.apply(read.transaction);
            }
            continue;
        }

        if (read.outcome == wal::ReadOutcome::EndOfLog) {
            recovered.wal_valid_length = reader.last_valid_offset();
            break;
        }

        // Corrupt or Truncated tail record: this is the expected shape of an
        // abrupt crash mid-append. Truncate the log back to the last known
        // good boundary so future appends start from a clean state.
        recovered.wal_valid_length = reader.last_valid_offset();
        if (Result<void> truncated = wal::truncate_wal(wal_path, recovered.wal_valid_length); !truncated) {
            return truncated.error();
        }
        break;
    }

    if (Result<void> trial = validate_trial_balance(recovered.state); !trial) return trial.error();

    return recovered;
}

} // namespace ledger::recovery
