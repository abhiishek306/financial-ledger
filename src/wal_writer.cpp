#include "ledger/wal_writer.hpp"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <cstring>
#include <ctime>

#include "ledger/crc32.hpp"
#include "ledger/wal_codec.hpp"

namespace ledger::wal {
namespace {

std::int64_t monotonic_now_ns() noexcept {
    timespec ts{};
    ::clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<std::int64_t>(ts.tv_sec) * 1'000'000'000LL + ts.tv_nsec;
}

} // namespace

Result<void> BinaryWalWriter::open(const std::string& path, SyncMode mode) {
    close();

    const int fd = ::open(path.c_str(), O_CREAT | O_WRONLY | O_APPEND, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
    if (fd < 0) return LedgerError::WalOpenFailed;

    struct stat st{};
    if (::fstat(fd, &st) != 0) {
        ::close(fd);
        return LedgerError::WalOpenFailed;
    }

    fd_.reset(fd);
    sync_mode_ = mode;
    offset_ = static_cast<std::uint64_t>(st.st_size);
    dirty_since_sync_ = false;
    return {};
}

Result<void> BinaryWalWriter::append(const Transaction& tx) {
    if (!fd_.valid()) return LedgerError::WalOpenFailed;

    Result<EncodedPayload> payload = encode_transaction_payload(tx);
    if (!payload) return payload.error();

    RecordHeader header{};
    header.magic = kMagic;
    header.version = kVersion;
    header.flags = 0;
    header.payload_size = payload.value().size;
    header.transaction_id = tx.id;
    header.timestamp_ns = monotonic_now_ns();

    std::array<std::byte, kMaxRecordSize> record{};
    std::size_t offset = 0;
    std::memcpy(record.data() + offset, &header, kHeaderSize);
    offset += kHeaderSize;
    std::memcpy(record.data() + offset, payload.value().bytes.data(), payload.value().size);
    offset += payload.value().size;

    const std::uint32_t crc = crc32::compute(record.data(), offset);
    std::memcpy(record.data() + offset, &crc, kCrcSize);
    offset += kCrcSize;

    const ssize_t written = ::write(fd_.get(), record.data(), offset);
    if (written < 0 || static_cast<std::size_t>(written) != offset) return LedgerError::WalWriteFailed;

    offset_ += offset;
    dirty_since_sync_ = true;

    if (sync_mode_ == SyncMode::SyncEveryWrite) {
        return flush();
    }
    return {};
}

Result<void> BinaryWalWriter::flush() {
    if (!fd_.valid()) return LedgerError::WalOpenFailed;
    if (!dirty_since_sync_) return {};

    if (::fsync(fd_.get()) != 0) return LedgerError::WalSyncFailed;
    dirty_since_sync_ = false;
    return {};
}

void BinaryWalWriter::close() {
    if (fd_.valid()) {
        ::fsync(fd_.get());
        fd_.reset();
    }
    offset_ = 0;
    dirty_since_sync_ = false;
}

} // namespace ledger::wal
