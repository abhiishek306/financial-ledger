#include "ledger/ledger_state.hpp"

namespace ledger {

bool LedgerState::add_account(const Account& account) {
    return accounts_.emplace(account.id, account).second;
}

const Account* LedgerState::find_account(AccountId id) const noexcept {
    auto it = accounts_.find(id);
    return it == accounts_.end() ? nullptr : &it->second;
}

Account* LedgerState::find_account_mut(AccountId id) noexcept {
    auto it = accounts_.find(id);
    return it == accounts_.end() ? nullptr : &it->second;
}

Result<void> LedgerState::apply(const Transaction& tx) noexcept {
    for (const EntryLeg& leg : tx.legs.span()) {
        Account* account = find_account_mut(leg.account_id);
        if (account == nullptr) return LedgerError::AccountNotFound;

        account->balance += signed_delta(account->type, leg.direction, leg.amount);
    }

    if (tx.id > last_applied_id_) last_applied_id_ = tx.id;
    return {};
}

Result<void> validate_trial_balance(const LedgerState& state) noexcept {
    std::int64_t debit_normal_total = 0;
    std::int64_t credit_normal_total = 0;

    for (const auto& [id, account] : state.accounts()) {
        if (is_debit_normal(account.type)) {
            debit_normal_total += account.balance;
        } else {
            credit_normal_total += account.balance;
        }
    }

    if (debit_normal_total != credit_normal_total) return LedgerError::TrialBalanceMismatch;
    return {};
}

} // namespace ledger
