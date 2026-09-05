#include "ledger/wal_reader.hpp"

#include <fcntl.h>
#include <unistd.h>

#include <array>
#include <cstring>

#include "ledger/crc32.hpp"
#include "ledger/wal_codec.hpp"

namespace ledger::wal {

Result<void> BinaryWalReader::open(const std::string& path) {
    close();

    const int fd = ::open(path.c_str(), O_RDONLY);
    if (fd < 0) return LedgerError::WalOpenFailed;

    fd_.reset(fd);
    cursor_ = 0;
    last_valid_offset_ = 0;
    return {};
}

ReadResult BinaryWalReader::read_next() {
    ReadResult result{};

    if (!fd_.valid()) {
        result.outcome = ReadOutcome::EndOfLog;
        return result;
    }

    RecordHeader header{};
    ssize_t n = ::pread(fd_.get(), &header, kHeaderSize, static_cast<off_t>(cursor_));
    if (n == 0) {
        result.outcome = ReadOutcome::EndOfLog;
        return result;
    }
    if (n < 0 || static_cast<std::size_t>(n) < kHeaderSize) {
        result.outcome = ReadOutcome::Truncated;
        return result;
    }

    if (header.magic != kMagic || header.version != kVersion || header.payload_size > kMaxPayloadSize) {
        result.outcome = ReadOutcome::Corrupt;
        return result;
    }

    const std::size_t record_size = kHeaderSize + header.payload_size + kCrcSize;
    std::array<std::byte, kMaxRecordSize> buffer{};
    std::memcpy(buffer.data(), &header, kHeaderSize);

    n = ::pread(fd_.get(), buffer.data() + kHeaderSize, header.payload_size + kCrcSize,
                static_cast<off_t>(cursor_ + kHeaderSize));
    if (n < 0 || static_cast<std::size_t>(n) < header.payload_size + kCrcSize) {
        result.outcome = ReadOutcome::Truncated;
        return result;
    }

    std::uint32_t stored_crc = 0;
    std::memcpy(&stored_crc, buffer.data() + kHeaderSize + header.payload_size, kCrcSize);
    const std::uint32_t computed_crc = crc32::compute(buffer.data(), kHeaderSize + header.payload_size);
    if (stored_crc != computed_crc) {
        result.outcome = ReadOutcome::Corrupt;
        return result;
    }

    std::span<const std::byte> payload_span{buffer.data() + kHeaderSize, header.payload_size};
    Result<Transaction> tx = decode_transaction_payload(payload_span, header.transaction_id, header.timestamp_ns);
    if (!tx) {
        result.outcome = ReadOutcome::Corrupt;
        return result;
    }

    cursor_ += record_size;
    last_valid_offset_ = cursor_;

    result.outcome = ReadOutcome::Ok;
    result.transaction = tx.value();
    return result;
}

void BinaryWalReader::close() {
    fd_.reset();
    cursor_ = 0;
    last_valid_offset_ = 0;
}

Result<void> truncate_wal(const std::string& path, std::uint64_t valid_offset) {
    const int fd = ::open(path.c_str(), O_WRONLY);
    if (fd < 0) return LedgerError::WalOpenFailed;

    UniqueFd guard{fd};
    if (::ftruncate(fd, static_cast<off_t>(valid_offset)) != 0) return LedgerError::WalWriteFailed;
    if (::fsync(fd) != 0) return LedgerError::WalSyncFailed;
    return {};
}

} // namespace ledger::wal
