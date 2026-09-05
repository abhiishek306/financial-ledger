#include "ledger/wal_codec.hpp"

#include <cstring>

namespace ledger::wal {
namespace {

template <typename T>
void put(std::byte* dst, std::size_t& offset, const T& value) noexcept {
    std::memcpy(dst + offset, &value, sizeof(T));
    offset += sizeof(T);
}

template <typename T>
void get(const std::byte* src, std::size_t& offset, T& value) noexcept {
    std::memcpy(&value, src + offset, sizeof(T));
    offset += sizeof(T);
}

} // namespace

Result<EncodedPayload> encode_transaction_payload(const Transaction& tx) noexcept {
    const auto legs = tx.legs.span();

    const std::size_t required =
        sizeof(std::uint64_t) * 2 + sizeof(std::uint8_t) + legs.size() * (sizeof(AccountId) + 1 + sizeof(Amount));
    if (required > kMaxPayloadSize) return LedgerError::WalPayloadTooLarge;

    EncodedPayload encoded{};
    std::size_t offset = 0;
    put(encoded.bytes.data(), offset, tx.idempotency_key.high);
    put(encoded.bytes.data(), offset, tx.idempotency_key.low);
    put(encoded.bytes.data(), offset, static_cast<std::uint8_t>(legs.size()));

    for (const EntryLeg& leg : legs) {
        put(encoded.bytes.data(), offset, leg.account_id);
        put(encoded.bytes.data(), offset, static_cast<std::uint8_t>(leg.direction));
        put(encoded.bytes.data(), offset, leg.amount);
    }

    encoded.size = static_cast<std::uint32_t>(offset);
    return encoded;
}

Result<Transaction> decode_transaction_payload(std::span<const std::byte> payload,
                                                TransactionId id,
                                                std::int64_t timestamp_ns) noexcept {
    constexpr std::size_t kMinSize = sizeof(std::uint64_t) * 2 + sizeof(std::uint8_t);
    if (payload.size() < kMinSize) return LedgerError::WalCorruptRecord;

    Transaction tx{};
    tx.id = id;
    tx.created_at_ns = timestamp_ns;

    std::size_t offset = 0;
    get(payload.data(), offset, tx.idempotency_key.high);
    get(payload.data(), offset, tx.idempotency_key.low);

    std::uint8_t leg_count = 0;
    get(payload.data(), offset, leg_count);

    if (leg_count > kMaxLegsPerTransaction) return LedgerError::WalCorruptRecord;

    const std::size_t expected_size = offset + static_cast<std::size_t>(leg_count) * (sizeof(AccountId) + 1 + sizeof(Amount));
    if (payload.size() != expected_size) return LedgerError::WalCorruptRecord;

    for (std::uint8_t i = 0; i < leg_count; ++i) {
        EntryLeg leg{};
        get(payload.data(), offset, leg.account_id);

        std::uint8_t dir_byte = 0;
        get(payload.data(), offset, dir_byte);
        if (dir_byte > static_cast<std::uint8_t>(Direction::Credit)) return LedgerError::WalCorruptRecord;
        leg.direction = static_cast<Direction>(dir_byte);

        get(payload.data(), offset, leg.amount);

        if (!tx.legs.push_back(leg)) return LedgerError::WalCorruptRecord;
    }

    return tx;
}

} // namespace ledger::wal
