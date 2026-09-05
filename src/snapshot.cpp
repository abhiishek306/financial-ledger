#include "ledger/snapshot.hpp"

#include <fcntl.h>
#include <unistd.h>

#include <cstring>
#include <thread>
#include <vector>

#include "ledger/crc32.hpp"
#include "ledger/unique_fd.hpp"

namespace ledger::snapshot {
namespace {

#pragma pack(push, 1)
struct AccountRecord {
    AccountId id;
    std::uint8_t type;
    Amount balance;
    Amount overdraft_limit;
};
#pragma pack(pop)

// Best-effort durability for the rename(): fsync the containing directory so
// the rename itself survives a crash, not just the file's own contents.
void fsync_parent_dir(const std::string& path) {
    const auto slash = path.find_last_of('/');
    const std::string dir = (slash == std::string::npos) ? "." : path.substr(0, slash);
    const int dir_fd = ::open(dir.c_str(), O_RDONLY);
    if (dir_fd >= 0) {
        ::fsync(dir_fd);
        ::close(dir_fd);
    }
}

} // namespace

Result<void> write_snapshot(const std::string& path, const LedgerState& state, std::uint64_t wal_offset) {
    const std::string tmp_path = path + ".tmp";

    const int fd = ::open(tmp_path.c_str(), O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if (fd < 0) return LedgerError::SnapshotOpenFailed;
    UniqueFd guard{fd};

    SnapshotHeader header{};
    header.magic = kMagic;
    header.version = kVersion;
    header.reserved = 0;
    header.account_count = static_cast<std::uint32_t>(state.accounts().size());
    header.last_applied_tx_id = state.last_applied_id();
    header.wal_offset = wal_offset;

    std::vector<std::byte> buffer(sizeof(SnapshotHeader) + state.accounts().size() * sizeof(AccountRecord));
    std::size_t offset = 0;
    std::memcpy(buffer.data() + offset, &header, sizeof(header));
    offset += sizeof(header);

    for (const auto& [id, account] : state.accounts()) {
        AccountRecord record{};
        record.id = account.id;
        record.type = static_cast<std::uint8_t>(account.type);
        record.balance = account.balance;
        record.overdraft_limit = account.overdraft_limit;
        std::memcpy(buffer.data() + offset, &record, sizeof(record));
        offset += sizeof(record);
    }

    const std::uint32_t crc = crc32::compute(buffer.data(), buffer.size());

    if (::write(fd, buffer.data(), buffer.size()) != static_cast<ssize_t>(buffer.size())) {
        return LedgerError::SnapshotWriteFailed;
    }
    if (::write(fd, &crc, sizeof(crc)) != static_cast<ssize_t>(sizeof(crc))) {
        return LedgerError::SnapshotWriteFailed;
    }
    if (::fsync(fd) != 0) return LedgerError::SnapshotWriteFailed;
    guard.reset();

    if (::rename(tmp_path.c_str(), path.c_str()) != 0) return LedgerError::SnapshotWriteFailed;
    fsync_parent_dir(path);

    return {};
}

void write_snapshot_async(const std::string& path, LedgerState state, std::uint64_t wal_offset) {
    std::thread([path, state = std::move(state), wal_offset]() mutable {
        // Best-effort: a failed async snapshot just means recovery falls
        // back to (or stays on) the previous snapshot / a longer WAL replay.
        (void)write_snapshot(path, state, wal_offset);
    }).detach();
}

Result<LoadedSnapshot> load_snapshot(const std::string& path) {
    const int fd = ::open(path.c_str(), O_RDONLY);
    if (fd < 0) return LedgerError::SnapshotReadFailed;
    UniqueFd guard{fd};

    SnapshotHeader header{};
    if (::read(fd, &header, sizeof(header)) != static_cast<ssize_t>(sizeof(header))) {
        return LedgerError::SnapshotCorrupt;
    }
    if (header.magic != kMagic || header.version != kVersion) return LedgerError::SnapshotCorrupt;

    const std::size_t records_size = static_cast<std::size_t>(header.account_count) * sizeof(AccountRecord);
    std::vector<std::byte> buffer(sizeof(header) + records_size);
    std::memcpy(buffer.data(), &header, sizeof(header));

    if (records_size > 0) {
        const ssize_t n = ::read(fd, buffer.data() + sizeof(header), records_size);
        if (n < 0 || static_cast<std::size_t>(n) != records_size) return LedgerError::SnapshotCorrupt;
    }

    std::uint32_t stored_crc = 0;
    if (::read(fd, &stored_crc, sizeof(stored_crc)) != static_cast<ssize_t>(sizeof(stored_crc))) {
        return LedgerError::SnapshotCorrupt;
    }

    const std::uint32_t computed_crc = crc32::compute(buffer.data(), buffer.size());
    if (computed_crc != stored_crc) return LedgerError::SnapshotCorrupt;

    LoadedSnapshot loaded{};
    loaded.wal_offset = header.wal_offset;
    loaded.state.set_last_applied_id(header.last_applied_tx_id);

    for (std::uint32_t i = 0; i < header.account_count; ++i) {
        AccountRecord record{};
        std::memcpy(&record, buffer.data() + sizeof(header) + i * sizeof(AccountRecord), sizeof(record));

        Account account{};
        account.id = record.id;
        account.type = static_cast<AccountType>(record.type);
        account.balance = record.balance;
        account.overdraft_limit = record.overdraft_limit;
        loaded.state.add_account(account);
    }

    return loaded;
}

} // namespace ledger::snapshot
