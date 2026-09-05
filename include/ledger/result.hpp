#pragma once

#include <cassert>
#include <cstdint>
#include <string_view>
#include <utility>
#include <variant>

// Minimal expected<T,E>-style result type. The project targets C++20, where
// std::expected is not yet part of the standard library, so we roll a small
// allocation-free replacement good enough for the ledger's error handling needs.

namespace ledger {

enum class LedgerError : std::uint8_t {
    None = 0,
    InvalidTransaction,
    UnbalancedTransaction,
    TooFewLegs,
    TooManyLegs,
    ZeroAmountLeg,
    AmountOverflow,
    AccountNotFound,
    InsufficientFunds,
    DuplicateTransaction,
    WalOpenFailed,
    WalWriteFailed,
    WalSyncFailed,
    WalCorruptRecord,
    WalTruncatedRecord,
    WalPayloadTooLarge,
    SnapshotOpenFailed,
    SnapshotWriteFailed,
    SnapshotReadFailed,
    SnapshotCorrupt,
    QueueFull,
    TrialBalanceMismatch,
    Unknown,
};

constexpr std::string_view to_string(LedgerError e) noexcept {
    switch (e) {
        case LedgerError::None: return "None";
        case LedgerError::InvalidTransaction: return "InvalidTransaction";
        case LedgerError::UnbalancedTransaction: return "UnbalancedTransaction";
        case LedgerError::TooFewLegs: return "TooFewLegs";
        case LedgerError::TooManyLegs: return "TooManyLegs";
        case LedgerError::ZeroAmountLeg: return "ZeroAmountLeg";
        case LedgerError::AmountOverflow: return "AmountOverflow";
        case LedgerError::AccountNotFound: return "AccountNotFound";
        case LedgerError::InsufficientFunds: return "InsufficientFunds";
        case LedgerError::DuplicateTransaction: return "DuplicateTransaction";
        case LedgerError::WalOpenFailed: return "WalOpenFailed";
        case LedgerError::WalWriteFailed: return "WalWriteFailed";
        case LedgerError::WalSyncFailed: return "WalSyncFailed";
        case LedgerError::WalCorruptRecord: return "WalCorruptRecord";
        case LedgerError::WalTruncatedRecord: return "WalTruncatedRecord";
        case LedgerError::WalPayloadTooLarge: return "WalPayloadTooLarge";
        case LedgerError::SnapshotOpenFailed: return "SnapshotOpenFailed";
        case LedgerError::SnapshotWriteFailed: return "SnapshotWriteFailed";
        case LedgerError::SnapshotReadFailed: return "SnapshotReadFailed";
        case LedgerError::SnapshotCorrupt: return "SnapshotCorrupt";
        case LedgerError::QueueFull: return "QueueFull";
        case LedgerError::TrialBalanceMismatch: return "TrialBalanceMismatch";
        case LedgerError::Unknown: return "Unknown";
    }
    return "Unknown";
}

template <typename T>
class Result {
public:
    Result(T value) : storage_(std::move(value)) {}
    Result(LedgerError error) : storage_(error) { assert(error != LedgerError::None); }

    [[nodiscard]] bool has_value() const noexcept { return storage_.index() == 0; }
    explicit operator bool() const noexcept { return has_value(); }

    [[nodiscard]] const T& value() const& noexcept { assert(has_value()); return std::get<0>(storage_); }
    [[nodiscard]] T& value() & noexcept { assert(has_value()); return std::get<0>(storage_); }
    [[nodiscard]] T&& value() && noexcept { assert(has_value()); return std::get<0>(std::move(storage_)); }

    [[nodiscard]] LedgerError error() const noexcept { assert(!has_value()); return std::get<1>(storage_); }

private:
    std::variant<T, LedgerError> storage_;
};

// void specialization: represents either success or a LedgerError.
template <>
class Result<void> {
public:
    Result() : error_(LedgerError::None) {}
    Result(LedgerError error) : error_(error) {}

    [[nodiscard]] bool has_value() const noexcept { return error_ == LedgerError::None; }
    explicit operator bool() const noexcept { return has_value(); }
    [[nodiscard]] LedgerError error() const noexcept { assert(!has_value()); return error_; }

private:
    LedgerError error_;
};

} // namespace ledger
