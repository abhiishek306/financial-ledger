#include "ledger/validation.hpp"

#include <array>
#include <limits>

namespace ledger {
namespace {

// Adds `delta` to `total`, returning false on signed 64-bit overflow.
[[nodiscard]] bool checked_add(std::int64_t& total, std::int64_t delta) noexcept {
    if (delta > 0 && total > std::numeric_limits<std::int64_t>::max() - delta) return false;
    if (delta < 0 && total < std::numeric_limits<std::int64_t>::min() - delta) return false;
    total += delta;
    return true;
}

} // namespace

Result<void> validate_transaction_shape(const Transaction& tx) noexcept {
    const auto legs = tx.legs.span();

    if (legs.size() < 2) return LedgerError::TooFewLegs;
    if (legs.size() > kMaxLegsPerTransaction) return LedgerError::TooManyLegs;

    std::int64_t debit_total = 0;
    std::int64_t credit_total = 0;

    for (const EntryLeg& leg : legs) {
        if (leg.amount <= 0) return LedgerError::ZeroAmountLeg;

        const bool ok = (leg.direction == Direction::Debit) ? checked_add(debit_total, leg.amount)
                                                              : checked_add(credit_total, leg.amount);
        if (!ok) return LedgerError::AmountOverflow;
    }

    if (debit_total != credit_total) return LedgerError::UnbalancedTransaction;

    return {};
}

Result<void> validate_transaction_against_state(const Transaction& tx, const AccountLookup& lookup) noexcept {
    // Accumulate the net delta per account within this transaction first (an
    // account may appear in more than one leg), then check the bound once
    // against the account's committed balance plus its pending net delta.
    struct PendingDelta {
        AccountId account_id;
        Amount delta;
    };
    std::array<PendingDelta, kMaxLegsPerTransaction> pending{};
    std::size_t pending_count = 0;

    for (const EntryLeg& leg : tx.legs.span()) {
        const Account* account = lookup(leg.account_id);
        if (account == nullptr) return LedgerError::AccountNotFound;

        const Amount delta = signed_delta(account->type, leg.direction, leg.amount);

        bool merged = false;
        for (std::size_t i = 0; i < pending_count; ++i) {
            if (pending[i].account_id == leg.account_id) {
                pending[i].delta += delta;
                merged = true;
                break;
            }
        }
        if (!merged) {
            pending[pending_count++] = PendingDelta{leg.account_id, delta};
        }
    }

    for (std::size_t i = 0; i < pending_count; ++i) {
        const Account* account = lookup(pending[i].account_id);
        // Already validated to exist above.
        const Amount projected_balance = account->balance + pending[i].delta;

        if (is_debit_normal(account->type)) {
            if (projected_balance < -account->overdraft_limit) return LedgerError::InsufficientFunds;
        }
        // Credit-normal accounts (Liability/Equity/Revenue) are not subject to
        // an overdraft bound in this engine; they may go arbitrarily positive
        // or, if desired, a similar bound could be added symmetrically here.
    }

    return {};
}

} // namespace ledger
