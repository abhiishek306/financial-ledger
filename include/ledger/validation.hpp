#pragma once

#include <functional>

#include "ledger/result.hpp"
#include "ledger/types.hpp"

// Phase 1: validation logic enforcing the double-entry balance invariant and
// per-account directional balance bounds (overdraft limits).

namespace ledger {

// Validates that a transaction is internally well-formed:
//  - has between 2 and kMaxLegsPerTransaction legs
//  - every leg has a strictly positive amount
//  - sum(debits) - sum(credits) == 0, checked with overflow-safe accumulation
[[nodiscard]] Result<void> validate_transaction_shape(const Transaction& tx) noexcept;

// Looks up the current committed Account state for `account_id`, or nullptr
// if the account does not exist. Implemented by the in-memory state store.
using AccountLookup = std::function<const Account*(AccountId account_id)>;

// Validates that applying `tx` against the state reachable via `lookup` would
// not violate any account's directional balance bound (e.g. an Asset/Expense
// account going below -overdraft_limit). Pure/read-only: does not mutate state.
[[nodiscard]] Result<void> validate_transaction_against_state(const Transaction& tx,
                                                               const AccountLookup& lookup) noexcept;

} // namespace ledger
