// Phase 5: microbenchmarks for the ledger engine.
//
//  BM_InMemoryApply           - pure in-memory transaction application, no WAL
//  BM_EndToEnd_SyncEveryWrite - full pipeline with fsync() on every append
//  BM_EndToEnd_BatchSync      - full pipeline, fsync() only every N appends
//  BM_LatencyPercentiles      - records P50/P95/P99 end-to-end append latency
//
// Build with: cmake -DLEDGER_BUILD_BENCHMARKS=ON ... (requires network access
// to fetch Google Benchmark via FetchContent).

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include <benchmark/benchmark.h>

#include "ledger/ledger_state.hpp"
#include "ledger/wal_writer.hpp"

using namespace ledger;

namespace {

constexpr AccountId kCash = 1;
constexpr AccountId kRevenue = 2;

LedgerState make_seed_state() {
    LedgerState state;
    state.add_account(Account{.id = kCash, .type = AccountType::Asset, .balance = 1'000'000'000, .overdraft_limit = 0});
    state.add_account(Account{.id = kRevenue, .type = AccountType::Revenue, .balance = 0, .overdraft_limit = 0});
    return state;
}

Transaction make_tx(TransactionId id) {
    Transaction tx{};
    tx.id = id;
    tx.idempotency_key = IdempotencyKey{.high = id, .low = id};
    (void)tx.legs.push_back(EntryLeg{.account_id = kCash, .direction = Direction::Credit, .amount = 10});
    (void)tx.legs.push_back(EntryLeg{.account_id = kRevenue, .direction = Direction::Debit, .amount = 10});
    return tx;
}

std::filesystem::path bench_wal_path(const char* suffix) {
    return std::filesystem::temp_directory_path() / (std::string("ledger_bench_") + suffix + ".bin");
}

} // namespace

static void BM_InMemoryApply(benchmark::State& state) {
    LedgerState ledger_state = make_seed_state();
    TransactionId id = 0;

    for (auto _ : state) {
        Transaction tx = make_tx(++id);
        benchmark::DoNotOptimize(ledger_state.apply(tx));
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_InMemoryApply);

static void BM_EndToEnd_SyncEveryWrite(benchmark::State& state) {
    const auto path = bench_wal_path("sync_every_write");
    std::filesystem::remove(path);

    LedgerState ledger_state = make_seed_state();
    wal::BinaryWalWriter writer;
    (void)writer.open(path.string(), wal::SyncMode::SyncEveryWrite);
    TransactionId id = 0;

    for (auto _ : state) {
        Transaction tx = make_tx(++id);
        benchmark::DoNotOptimize(writer.append(tx));
        benchmark::DoNotOptimize(ledger_state.apply(tx));
    }
    state.SetItemsProcessed(state.iterations());
    std::filesystem::remove(path);
}
BENCHMARK(BM_EndToEnd_SyncEveryWrite);

static void BM_EndToEnd_BatchSync(benchmark::State& state) {
    const auto path = bench_wal_path("batch_sync");
    std::filesystem::remove(path);

    LedgerState ledger_state = make_seed_state();
    wal::BinaryWalWriter writer;
    (void)writer.open(path.string(), wal::SyncMode::BatchSyncFlush);
    TransactionId id = 0;
    constexpr int kBatchSize = 128;
    int in_batch = 0;

    for (auto _ : state) {
        Transaction tx = make_tx(++id);
        benchmark::DoNotOptimize(writer.append(tx));
        benchmark::DoNotOptimize(ledger_state.apply(tx));
        if (++in_batch >= kBatchSize) {
            (void)writer.flush();
            in_batch = 0;
        }
    }
    (void)writer.flush();
    state.SetItemsProcessed(state.iterations());
    std::filesystem::remove(path);
}
BENCHMARK(BM_EndToEnd_BatchSync);

static void BM_LatencyPercentiles(benchmark::State& state) {
    const auto path = bench_wal_path("latency");
    std::filesystem::remove(path);

    LedgerState ledger_state = make_seed_state();
    wal::BinaryWalWriter writer;
    (void)writer.open(path.string(), wal::SyncMode::SyncEveryWrite);
    TransactionId id = 0;

    std::vector<double> latencies_us;
    latencies_us.reserve(static_cast<std::size_t>(state.max_iterations));

    for (auto _ : state) {
        Transaction tx = make_tx(++id);

        const auto start = std::chrono::steady_clock::now();
        benchmark::DoNotOptimize(writer.append(tx));
        benchmark::DoNotOptimize(ledger_state.apply(tx));
        const auto end = std::chrono::steady_clock::now();

        latencies_us.push_back(std::chrono::duration<double, std::micro>(end - start).count());
    }

    std::sort(latencies_us.begin(), latencies_us.end());
    const auto percentile = [&](double p) {
        if (latencies_us.empty()) return 0.0;
        const std::size_t idx = static_cast<std::size_t>(p * static_cast<double>(latencies_us.size() - 1));
        return latencies_us[idx];
    };

    state.counters["P50_us"] = percentile(0.50);
    state.counters["P95_us"] = percentile(0.95);
    state.counters["P99_us"] = percentile(0.99);
    std::filesystem::remove(path);
}
BENCHMARK(BM_LatencyPercentiles)->Iterations(2000);

BENCHMARK_MAIN();
