// Demo/reference driver for the ledger engine: boots the engine, seeds a
// small chart of accounts, submits transactions concurrently from several
// producer threads while a single writer thread drains the ring buffer, then
// snapshots and shows a recovery pass reading the WAL back from disk.

#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

#include "ledger/engine.hpp"
#include "ledger/recovery.hpp"
#include "ledger/snapshot.hpp"

using namespace ledger;
using namespace std::chrono_literals;

namespace {

constexpr AccountId kCash = 1;
constexpr AccountId kRevenue = 2;

} // namespace

int main() {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ledger_demo";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    const std::string wal_path = (dir / "wal.bin").string();
    const std::string snapshot_path = (dir / "snapshot.bin").string();

    LedgerEngine::Config config{};
    config.wal_path = wal_path;
    config.sync_mode = wal::SyncMode::SyncEveryWrite;
    config.ring_buffer_capacity = 1024;
    config.publish_snapshot_every_n = 16;

    LedgerEngine engine(config);
    if (Result<void> opened = engine.open(); !opened) {
        std::fprintf(stderr, "failed to open WAL: %s\n", std::string(to_string(opened.error())).c_str());
        return 1;
    }

    engine.add_account(Account{.id = kCash, .type = AccountType::Asset, .balance = 0, .overdraft_limit = 50'000});
    engine.add_account(Account{.id = kRevenue, .type = AccountType::Revenue, .balance = 0, .overdraft_limit = 0});

    // Persist the chart of accounts as the initial snapshot so recovery has
    // somewhere to seed accounts from before replaying the WAL.
    (void)snapshot::write_snapshot(snapshot_path, engine.state_working_copy(), 0);

    constexpr int kProducers = 4;
    constexpr int kTxPerProducer = 500;
    std::vector<std::thread> producers;
    std::atomic<int> accepted{0};
    std::atomic<int> rejected{0};
    std::atomic<int> producers_finished{0};

    for (int p = 0; p < kProducers; ++p) {
        producers.emplace_back([&engine, &accepted, &rejected, &producers_finished]() {
            for (int i = 0; i < kTxPerProducer; ++i) {
                Transaction tx{};
                tx.id = engine.allocate_transaction_id();
                tx.idempotency_key = IdempotencyKey{.high = tx.id, .low = tx.id};
                (void)tx.legs.push_back(EntryLeg{.account_id = kRevenue, .direction = Direction::Debit, .amount = 10});
                (void)tx.legs.push_back(EntryLeg{.account_id = kCash, .direction = Direction::Credit, .amount = 10});

                Completion completion{};
                if (!engine.submit(tx, completion)) {
                    rejected.fetch_add(1, std::memory_order_relaxed);
                    continue;
                }
                while (!completion.done.load(std::memory_order_acquire)) {
                    std::this_thread::yield();
                }
                if (completion.error == LedgerError::None) {
                    accepted.fetch_add(1, std::memory_order_relaxed);
                } else {
                    rejected.fetch_add(1, std::memory_order_relaxed);
                }
            }
            producers_finished.fetch_add(1, std::memory_order_release);
        });
    }

    // Single-writer event loop: the only thread allowed to mutate state.
    // Keep draining the ring buffer until every producer has finished
    // submitting (producers block on their own completion, so this thread
    // must keep making progress or they would deadlock).
    while (producers_finished.load(std::memory_order_acquire) < kProducers) {
        engine.process_pending(1024);
    }

    for (std::thread& t : producers) t.join();
    engine.process_pending(1 << 20); // final drain of any stragglers

    std::printf("accepted=%d rejected=%d\n", accepted.load(), rejected.load());

    std::shared_ptr<const LedgerState> snap = engine.snapshot();
    std::printf("cash balance=%lld revenue balance=%lld\n",
                static_cast<long long>(snap->find_account(kCash)->balance),
                static_cast<long long>(snap->find_account(kRevenue)->balance));

    (void)snapshot::write_snapshot(snapshot_path, *snap, engine.wal_writer().size_bytes());

    Result<recovery::RecoveredState> recovered = recovery::recover(wal_path, snapshot_path);
    if (!recovered) {
        std::fprintf(stderr, "recovery failed: %s\n", std::string(to_string(recovered.error())).c_str());
        return 1;
    }
    std::printf("recovery OK: cash=%lld revenue=%lld\n",
                static_cast<long long>(recovered.value().state.find_account(kCash)->balance),
                static_cast<long long>(recovered.value().state.find_account(kRevenue)->balance));

    return 0;
}
