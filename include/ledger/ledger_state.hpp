#pragma once

#include <atomic>
#include <memory>
#include <unordered_map>

#include "ledger/result.hpp"
#include "ledger/types.hpp"

// Phase 3: in-memory ledger state (account balances) plus a copy-on-write
// snapshot mechanism for lock-free concurrent reads while a single writer
// thread mutates the authoritative working copy.

namespace ledger {

// Immutable, shareable snapshot of all account balances. Readers obtain a
// std::shared_ptr<const LedgerState> via LedgerStateStore::snapshot() and can
// safely use it without any locking, even while the writer publishes newer
// snapshots concurrently.
class LedgerState {
public:
    // Registers a new account. Returns false if an account with this id
    // already exists.
    bool add_account(const Account& account);

    [[nodiscard]] const Account* find_account(AccountId id) const noexcept;
    [[nodiscard]] Account* find_account_mut(AccountId id) noexcept;

    [[nodiscard]] const std::unordered_map<AccountId, Account>& accounts() const noexcept { return accounts_; }

    // Applies every leg of `tx` to the corresponding account balances.
    // Precondition: the transaction has already been validated (shape +
    // against-state); this method does not re-validate and always succeeds
    // as long as every referenced account exists.
    [[nodiscard]] Result<void> apply(const Transaction& tx) noexcept;

    // The highest transaction id applied so far (0 if none). Used by the
    // recovery engine / snapshotting to know where to resume WAL replay.
    [[nodiscard]] TransactionId last_applied_id() const noexcept { return last_applied_id_; }
    void set_last_applied_id(TransactionId id) noexcept { last_applied_id_ = id; }

private:
    std::unordered_map<AccountId, Account> accounts_;
    TransactionId last_applied_id_{0};
};

// Verifies the fundamental accounting identity across every account:
//   sum(Asset.balance) + sum(Expense.balance)
//     - sum(Liability.balance) - sum(Equity.balance) - sum(Revenue.balance) == 0
// This holds iff every transaction ever applied was individually balanced.
[[nodiscard]] Result<void> validate_trial_balance(const LedgerState& state) noexcept;

// Thread-safe holder for the single writer thread's working copy plus a
// publishable, immutable snapshot for concurrent lock-free readers.
class LedgerStateStore {
public:
    LedgerStateStore() : published_(std::make_shared<const LedgerState>()) {}

    // Writer-only: mutable reference to the authoritative working state.
    // Must only be called from the single writer thread.
    [[nodiscard]] LedgerState& working_copy() noexcept { return working_; }

    // Writer-only: publishes a fresh copy-on-write snapshot of the working
    // state for readers. Cheap relative to per-transaction cost if called
    // periodically rather than after every single transaction.
    void publish_snapshot() {
        published_.store(std::make_shared<const LedgerState>(working_), std::memory_order_release);
    }

    // Reader-side (any thread): obtain a stable, immutable snapshot.
    [[nodiscard]] std::shared_ptr<const LedgerState> snapshot() const {
        return published_.load(std::memory_order_acquire);
    }

private:
    LedgerState working_;
    std::atomic<std::shared_ptr<const LedgerState>> published_;
};

} // namespace ledger
