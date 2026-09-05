#pragma once

#include <cstdint>
#include <string>

#include "ledger/result.hpp"
#include "ledger/types.hpp"
#include "ledger/unique_fd.hpp"
#include "ledger/wal_format.hpp"

// Phase 2: append-only binary WAL writer built directly on POSIX file
// descriptors. Every append serializes the transaction into a static buffer
// (header + payload + CRC32) and writes it in a single write() call before
// the caller is permitted to mutate in-memory state.

namespace ledger::wal {

class BinaryWalWriter {
public:
    BinaryWalWriter() = default;
    ~BinaryWalWriter() = default;

    BinaryWalWriter(const BinaryWalWriter&) = delete;
    BinaryWalWriter& operator=(const BinaryWalWriter&) = delete;
    BinaryWalWriter(BinaryWalWriter&&) = default;
    BinaryWalWriter& operator=(BinaryWalWriter&&) = default;

    // Opens (creating if necessary) `path` for append-only writes.
    [[nodiscard]] Result<void> open(const std::string& path, SyncMode mode);

    // Serializes and appends `tx` to the log. On SyncEveryWrite this fsyncs
    // before returning; on BatchSyncFlush the caller must call flush()
    // periodically (or before acknowledging a batch of transactions) to make
    // the writes durable.
    [[nodiscard]] Result<void> append(const Transaction& tx);

    // Forces any buffered writes to durable storage. No-op if there is
    // nothing pending. Safe to call regardless of SyncMode.
    [[nodiscard]] Result<void> flush();

    // Current size of the log file in bytes (i.e. the offset the next record
    // will be written at). Useful for snapshot checkpoints.
    [[nodiscard]] std::uint64_t size_bytes() const noexcept { return offset_; }

    [[nodiscard]] bool is_open() const noexcept { return fd_.valid(); }

    void close();

private:
    UniqueFd fd_;
    SyncMode sync_mode_{SyncMode::SyncEveryWrite};
    std::uint64_t offset_{0};
    bool dirty_since_sync_{false};
};

} // namespace ledger::wal
