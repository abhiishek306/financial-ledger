#pragma once

#include <cstdint>

// Phase 2: on-disk binary WAL record layout.
//
// Record layout (all integers little-endian on little-endian hosts; this
// engine targets x86_64/aarch64 Linux where native byte order is used
// directly for simplicity and speed):
//
//   [ RecordHeader (fixed size) ][ payload bytes (payload_size) ][ crc32 (4B) ]
//
// The CRC32 trailer covers the header bytes (as written on disk) followed by
// the payload bytes, so both header and payload corruption are detected.

namespace ledger::wal {

inline constexpr std::uint32_t kMagic = 0x4C454447u; // "LEDG"
inline constexpr std::uint16_t kVersion = 1;

#pragma pack(push, 1)
struct RecordHeader {
    std::uint32_t magic;
    std::uint16_t version;
    std::uint16_t flags;
    std::uint32_t payload_size;
    std::uint64_t transaction_id;
    std::int64_t timestamp_ns; // monotonic clock timestamp at append time
};
#pragma pack(pop)

inline constexpr std::size_t kHeaderSize = sizeof(RecordHeader);
inline constexpr std::size_t kCrcSize = sizeof(std::uint32_t);

// Maximum encoded payload size: bounds the size of the static write buffer so
// the writer never allocates on the hot path.
inline constexpr std::size_t kMaxPayloadSize = 512;
inline constexpr std::size_t kMaxRecordSize = kHeaderSize + kMaxPayloadSize + kCrcSize;

enum class SyncMode : std::uint8_t {
    SyncEveryWrite, // fsync() after every append; strongest durability, lowest throughput
    BatchSyncFlush, // fsync() only on explicit flush() calls / batch boundaries
};

} // namespace ledger::wal
