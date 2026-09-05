#pragma once

#include <array>
#include <cstddef>
#include <span>

#include "ledger/result.hpp"
#include "ledger/types.hpp"
#include "ledger/wal_format.hpp"

// Phase 2: structured binary serialization of a Transaction's payload
// (everything the WAL record stores besides the transaction id / timestamp,
// which live in the fixed RecordHeader). Encoding uses a static buffer to
// avoid heap allocation on the hot path.

namespace ledger::wal {

struct EncodedPayload {
    std::array<std::byte, kMaxPayloadSize> bytes{};
    std::uint32_t size{0};

    [[nodiscard]] std::span<const std::byte> span() const noexcept { return {bytes.data(), size}; }
};

// Layout: [idempotency.high:8][idempotency.low:8][leg_count:1]
//         { [account_id:8][direction:1][amount:8] } * leg_count
[[nodiscard]] Result<EncodedPayload> encode_transaction_payload(const Transaction& tx) noexcept;

// Reconstructs a Transaction's id/timestamp/idempotency/legs from a decoded
// payload buffer. `id` and `timestamp_ns` come from the record header.
[[nodiscard]] Result<Transaction> decode_transaction_payload(std::span<const std::byte> payload,
                                                               TransactionId id,
                                                               std::int64_t timestamp_ns) noexcept;

} // namespace ledger::wal
