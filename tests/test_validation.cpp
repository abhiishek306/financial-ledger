#include <cstdio>
#include <initializer_list>

#include "ledger/types.hpp"
#include "ledger/validation.hpp"
#include "test_util.hpp"

using namespace ledger;

namespace {

Transaction make_tx(TransactionId id, std::initializer_list<EntryLeg> legs) {
    Transaction tx{};
    tx.id = id;
    for (const EntryLeg& leg : legs) {
        LEDGER_CHECK(tx.legs.push_back(leg));
    }
    return tx;
}

void test_balanced_transaction_is_valid() {
    Transaction tx = make_tx(1, {
        EntryLeg{.account_id = 1, .direction = Direction::Debit, .amount = 1000},
        EntryLeg{.account_id = 2, .direction = Direction::Credit, .amount = 1000},
    });
    LEDGER_CHECK(validate_transaction_shape(tx).has_value());
}

void test_unbalanced_transaction_is_rejected() {
    Transaction tx = make_tx(2, {
        EntryLeg{.account_id = 1, .direction = Direction::Debit, .amount = 1000},
        EntryLeg{.account_id = 2, .direction = Direction::Credit, .amount = 999},
    });
    Result<void> result = validate_transaction_shape(tx);
    LEDGER_CHECK(!result.has_value());
    LEDGER_CHECK(result.error() == LedgerError::UnbalancedTransaction);
}

void test_single_leg_transaction_is_rejected() {
    Transaction tx = make_tx(3, {EntryLeg{.account_id = 1, .direction = Direction::Debit, .amount = 1000}});
    Result<void> result = validate_transaction_shape(tx);
    LEDGER_CHECK(!result.has_value());
    LEDGER_CHECK(result.error() == LedgerError::TooFewLegs);
}

void test_zero_amount_leg_is_rejected() {
    Transaction tx = make_tx(4, {
        EntryLeg{.account_id = 1, .direction = Direction::Debit, .amount = 0},
        EntryLeg{.account_id = 2, .direction = Direction::Credit, .amount = 0},
    });
    Result<void> result = validate_transaction_shape(tx);
    LEDGER_CHECK(!result.has_value());
    LEDGER_CHECK(result.error() == LedgerError::ZeroAmountLeg);
}

void test_overdraft_limit_enforced() {
    Account checking{.id = 1, .type = AccountType::Asset, .balance = 500, .overdraft_limit = 200};
    Account revenue{.id = 2, .type = AccountType::Revenue, .balance = 0, .overdraft_limit = 0};

    auto lookup = [&](AccountId id) -> const Account* {
        if (id == 1) return &checking;
        if (id == 2) return &revenue;
        return nullptr;
    };

    // Withdrawing 800 from an asset account with balance 500 and overdraft
    // 200 would push it to -300, which exceeds the -200 floor.
    Transaction over_limit = make_tx(5, {
        EntryLeg{.account_id = 2, .direction = Direction::Debit, .amount = 800},
        EntryLeg{.account_id = 1, .direction = Direction::Credit, .amount = 800},
    });
    Result<void> rejected = validate_transaction_against_state(over_limit, lookup);
    LEDGER_CHECK(!rejected.has_value());
    LEDGER_CHECK(rejected.error() == LedgerError::InsufficientFunds);

    // Withdrawing 700 lands exactly at the -200 floor: allowed.
    Transaction within_limit = make_tx(6, {
        EntryLeg{.account_id = 2, .direction = Direction::Debit, .amount = 700},
        EntryLeg{.account_id = 1, .direction = Direction::Credit, .amount = 700},
    });
    LEDGER_CHECK(validate_transaction_against_state(within_limit, lookup).has_value());
}

void test_unknown_account_is_rejected() {
    Account checking{.id = 1, .type = AccountType::Asset, .balance = 500, .overdraft_limit = 0};
    auto lookup = [&](AccountId id) -> const Account* { return id == 1 ? &checking : nullptr; };

    Transaction tx = make_tx(7, {
        EntryLeg{.account_id = 1, .direction = Direction::Debit, .amount = 10},
        EntryLeg{.account_id = 999, .direction = Direction::Credit, .amount = 10},
    });
    Result<void> result = validate_transaction_against_state(tx, lookup);
    LEDGER_CHECK(!result.has_value());
    LEDGER_CHECK(result.error() == LedgerError::AccountNotFound);
}

} // namespace

int main() {
    test_balanced_transaction_is_valid();
    test_unbalanced_transaction_is_rejected();
    test_single_leg_transaction_is_rejected();
    test_zero_amount_leg_is_rejected();
    test_overdraft_limit_enforced();
    test_unknown_account_is_rejected();
    std::puts("test_validation: all checks passed");
    return 0;
}
