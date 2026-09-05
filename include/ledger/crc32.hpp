#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

// Phase 2: CRC32 (IEEE 802.3, polynomial 0xEDB88320) used to detect corrupted
// or partially-written WAL records and snapshot files.

namespace ledger::crc32 {

[[nodiscard]] std::uint32_t compute(std::span<const std::byte> data) noexcept;
[[nodiscard]] std::uint32_t compute(const void* data, std::size_t size) noexcept;

} // namespace ledger::crc32
