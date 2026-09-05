// Fault-injection test: writes several valid transactions to the WAL, then
// simulates an abrupt crash (e.g. std::terminate or a partial disk write)
// by appending a torn/incomplete record directly to the file. Verifies that
// crash recovery detects the corruption, truncates the WAL back to the last
// valid record boundary, and reconstructs a state that satisfies the global
// trial-balance invariant -- i.e. recovery never yields an unbalanced or
// corrupted ledger.

#include <cstdio>
#include <filesystem>
#include <fstream>

#include "ledger/ledger_state.hpp"
#include "ledger/recovery.hpp"
#include "ledger/snapshot.hpp"
#include "ledger/wal_writer.hpp"
#include "test_util.hpp"

using namespace ledger;

namespace {

constexpr AccountId kCash = 1;
constexpr AccountId kRevenue = 2;

Transaction make_tx(TransactionId id, Amount amount) {
    Transaction tx{};
    tx.id = id;
    tx.idempotency_key = IdempotencyKey{.high = id, .low = id};
    LEDGER_CHECK(tx.legs.push_back(EntryLeg{.account_id = kCash, .direction = Direction::Debit, .amount = amount}));
    LEDGER_CHECK(tx.legs.push_back(EntryLeg{.account_id = kRevenue, .direction = Direction::Credit, .amount = amount}));
    return tx;
}

void run_fault_injection_scenario() {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ledger_fault_injection";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    const std::filesystem::path wal_path = dir / "wal.bin";
    const std::filesystem::path snapshot_path = dir / "snapshot.bin";

    // Seed the chart of accounts via an initial (zero-balance) snapshot, the
    // same way a real engine bootstraps before ever writing to the WAL.
    LedgerState seed_state;
    LEDGER_CHECK(seed_state.add_account(Account{.id = kCash, .type = AccountType::Asset, .balance = 0, .overdraft_limit = 0}));
    LEDGER_CHECK(seed_state.add_account(Account{.id = kRevenue, .type = AccountType::Revenue, .balance = 0, .overdraft_limit = 0}));
    LEDGER_CHECK(snapshot::write_snapshot(snapshot_path.string(), seed_state, 0).has_value());

    // Track the expected post-recovery state independently of the engine.
    LedgerState expected = seed_state;

    std::uint64_t valid_wal_length = 0;
    {
        wal::BinaryWalWriter writer;
        LEDGER_CHECK(writer.open(wal_path.string(), wal::SyncMode::SyncEveryWrite).has_value());

        for (TransactionId id = 1; id <= 5; ++id) {
            Transaction tx = make_tx(id, static_cast<Amount>(id) * 1000);
            LEDGER_CHECK(writer.append(tx).has_value());
            LEDGER_CHECK(expected.apply(tx).has_value());
        }
        valid_wal_length = writer.size_bytes();
    }

    // Simulate a crash mid-append: a 6th transaction whose record header was
    // written but the process died before the payload/CRC trailer landed on
    // disk (a torn write), exactly what an abrupt std::terminate() or a
    // simulated partial disk write would leave behind.
    {
        std::ofstream raw(wal_path, std::ios::binary | std::ios::app);
        LEDGER_CHECK(raw.is_open());
        const char torn_header[] = {'L', 'E', 'D', 'G', 1, 0, 0, 0, 0x20, 0x00, 0x00, 0x00};
        raw.write(torn_header, sizeof(torn_header));
    }

    Result<recovery::RecoveredState> recovered = recovery::recover(wal_path.string(), snapshot_path.string());
    LEDGER_CHECK(recovered.has_value());

    const LedgerState& state = recovered.value().state;
    LEDGER_CHECK(state.find_account(kCash)->balance == expected.find_account(kCash)->balance);
    LEDGER_CHECK(state.find_account(kRevenue)->balance == expected.find_account(kRevenue)->balance);
    LEDGER_CHECK(validate_trial_balance(state).has_value());

    // The WAL must have been truncated back to the last valid transaction
    // boundary, discarding the torn tail record entirely.
    LEDGER_CHECK(recovered.value().wal_valid_length == valid_wal_length);
    LEDGER_CHECK(std::filesystem::file_size(wal_path) == valid_wal_length);

    std::filesystem::remove_all(dir);
}

} // namespace

int main() {
    run_fault_injection_scenario();
    std::puts("fault_injection_test: recovery preserved a balanced, uncorrupted ledger");
    return 0;
}
