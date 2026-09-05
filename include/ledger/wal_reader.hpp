#pragma once

#include <cstdint>
#include <string>

#include "ledger/result.hpp"
#include "ledger/types.hpp"
#include "ledger/unique_fd.hpp"
#include "ledger/wal_format.hpp"

// Phase 2 / 4: sequential binary WAL reader used during crash recovery.
// Detects truncated or corrupted records (CRC32 mismatch, bad magic, torn
// writes) and reports the byte offset of the last known-good record boundary
// so the caller can truncate the log back to a consistent state.

namespace ledger::wal {

enum class ReadOutcome : std::uint8_t {
    Ok,          // a valid transaction was decoded
    EndOfLog,    // reached a clean end-of-file at a record boundary
    Corrupt,     // CRC mismatch / bad magic — record boundary is NOT trustworthy
    Truncated,   // fewer bytes remain than a full record needs (partial write)
};

struct ReadResult {
    ReadOutcome outcome{ReadOutcome::EndOfLog};
    Transaction transaction{}; // valid only when outcome == Ok
};

class BinaryWalReader {
public:
    BinaryWalReader() = default;

    BinaryWalReader(const BinaryWalReader&) = delete;
    BinaryWalReader& operator=(const BinaryWalReader&) = delete;
    BinaryWalReader(BinaryWalReader&&) = default;
    BinaryWalReader& operator=(BinaryWalReader&&) = default;

    [[nodiscard]] Result<void> open(const std::string& path);

    // Repositions the read cursor to `offset` bytes from the start of the
    // file (used to resume replay after a snapshot checkpoint).
    void seek(std::uint64_t offset) noexcept { cursor_ = offset; last_valid_offset_ = offset; }

    // Reads and decodes the next record. Advances the internal cursor only
    // when outcome == Ok; on Corrupt/Truncated/EndOfLog the cursor stays at
    // the start of the offending (or missing) record.
    [[nodiscard]] ReadResult read_next();

    // Byte offset of the last successfully validated record boundary. This
    // is the safe point to which the underlying file should be truncated if
    // a Corrupt/Truncated record is encountered.
    [[nodiscard]] std::uint64_t last_valid_offset() const noexcept { return last_valid_offset_; }

    void close();

private:
    UniqueFd fd_;
    std::uint64_t cursor_{0};
    std::uint64_t last_valid_offset_{0};
};

// Truncates the WAL file at `path` to exactly `valid_offset` bytes. Used by
// the recovery engine after detecting a corrupt/torn tail record.
[[nodiscard]] Result<void> truncate_wal(const std::string& path, std::uint64_t valid_offset);

} // namespace ledger::wal
