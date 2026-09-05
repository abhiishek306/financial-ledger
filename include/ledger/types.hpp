#pragma once

#include <array>
#include <compare>
#include <cstdint>
#include <functional>
#include <span>
#include <string_view>

// Phase 1: core domain types & invariants for the double-entry ledger.

namespace ledger {

using AccountId = std::uint64_t;
using TransactionId = std::uint64_t;
// Fixed-point integer amount in micro-units (1 unit == 1_000_000 micro-units).
// Using int64_t avoids floating point rounding error in monetary math.
using Amount = std::int64_t;

// 128-bit client-supplied idempotency key (e.g. a UUID split into two halves).
struct IdempotencyKey {
    std::uint64_t high{0};
    std::uint64_t low{0};

    friend auto operator<=>(const IdempotencyKey&, const IdempotencyKey&) = default;
};

struct IdempotencyKeyHash {
    std::size_t operator()(const IdempotencyKey& k) const noexcept {
        std::size_t h1 = std::hash<std::uint64_t>{}(k.high);
        std::size_t h2 = std::hash<std::uint64_t>{}(k.low);
        // boost::hash_combine-style mix.
        return h1 ^ (h2 + 0x9e3779b97f4a7c15ULL + (h1 << 6) + (h1 >> 2));
    }
};

enum class AccountType : std::uint8_t { Asset, Liability, Equity, Revenue, Expense };

constexpr std::string_view to_string(AccountType t) noexcept {
    switch (t) {
        case AccountType::Asset: return "Asset";
        case AccountType::Liability: return "Liability";
        case AccountType::Equity: return "Equity";
        case AccountType::Revenue: return "Revenue";
        case AccountType::Expense: return "Expense";
    }
    return "Unknown";
}

// Debit-normal accounts (Asset, Expense) increase on DEBIT; credit-normal
// accounts (Liability, Equity, Revenue) increase on CREDIT.
constexpr bool is_debit_normal(AccountType t) noexcept {
    return t == AccountType::Asset || t == AccountType::Expense;
}

enum class Direction : std::uint8_t { Debit = 0, Credit = 1 };

constexpr std::string_view to_string(Direction d) noexcept {
    return d == Direction::Debit ? "DEBIT" : "CREDIT";
}

// Signed delta to apply to Account::balance for a leg of (type, direction, amount).
// Account::balance is always stored in "natural" units: positive means more of
// whatever increases that account type (debit for Asset/Expense, credit otherwise).
constexpr Amount signed_delta(AccountType type, Direction dir, Amount amount) noexcept {
    const bool increases = (dir == Direction::Debit) == is_debit_normal(type);
    return increases ? amount : -amount;
}

struct EntryLeg {
    AccountId account_id{0};
    Direction direction{Direction::Debit};
    Amount amount{0}; // always > 0; sign is conveyed by `direction`
};

inline constexpr std::size_t kMaxLegsPerTransaction = 16;

// Fixed-capacity leg list: no heap allocation, cache-friendly, bounded size
// so it can live inline inside ring-buffer slots and WAL payload buffers.
class LegList {
public:
    [[nodiscard]] bool push_back(const EntryLeg& leg) noexcept {
        if (count_ >= kMaxLegsPerTransaction) return false;
        legs_[count_++] = leg;
        return true;
    }

    [[nodiscard]] std::span<const EntryLeg> span() const noexcept { return {legs_.data(), count_}; }
    [[nodiscard]] std::size_t size() const noexcept { return count_; }
    [[nodiscard]] bool empty() const noexcept { return count_ == 0; }
    void clear() noexcept { count_ = 0; }

private:
    std::array<EntryLeg, kMaxLegsPerTransaction> legs_{};
    std::size_t count_{0};
};

struct Transaction {
    TransactionId id{0};
    IdempotencyKey idempotency_key{};
    std::int64_t created_at_ns{0}; // monotonic timestamp captured at ingestion
    LegList legs;
};

struct Account {
    AccountId id{0};
    AccountType type{AccountType::Asset};
    Amount balance{0};
    // Only meaningful for debit-normal accounts (Asset/Expense): the account
    // may go as low as -overdraft_limit before being rejected. Zero means no
    // overdraft is permitted.
    Amount overdraft_limit{0};
};

} // namespace ledger
