#include "ledger/crc32.hpp"

#include <array>

namespace ledger::crc32 {
namespace {

// Precomputed CRC32 (IEEE 802.3) lookup table, built at compile time.
constexpr std::array<std::uint32_t, 256> make_table() noexcept {
    std::array<std::uint32_t, 256> table{};
    for (std::uint32_t i = 0; i < 256; ++i) {
        std::uint32_t c = i;
        for (int k = 0; k < 8; ++k) {
            c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        }
        table[i] = c;
    }
    return table;
}

constexpr std::array<std::uint32_t, 256> kTable = make_table();

} // namespace

std::uint32_t compute(std::span<const std::byte> data) noexcept {
    std::uint32_t crc = 0xFFFFFFFFu;
    for (std::byte b : data) {
        const auto idx = static_cast<std::uint8_t>((crc ^ static_cast<std::uint8_t>(b)) & 0xFFu);
        crc = kTable[idx] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFFu;
}

std::uint32_t compute(const void* data, std::size_t size) noexcept {
    return compute(std::span<const std::byte>{static_cast<const std::byte*>(data), size});
}

} // namespace ledger::crc32
