#pragma once

#include <atomic>
#include <cstddef>
#include <memory>
#include <string>

#include "ledger/idempotency_cache.hpp"
#include "ledger/ledger_state.hpp"
#include "ledger/result.hpp"
#include "ledger/ring_buffer.hpp"
#include "ledger/types.hpp"
#include "ledger/wal_writer.hpp"

// Phase 3: wires together the ring buffer, WAL, and in-memory state store
// into the engine's single-writer event loop:
//
//   producer threads --submit()--> MPSC ring buffer
//                                       |
//                             [ single writer thread ]
//                                       |
//                     process_pending(): dequeue -> validate
//                                        -> WAL append + fsync
//                                        -> mutate in-memory state
//                                        -> publish snapshot (periodic)
//                                        -> signal completion (ack)

namespace ledger {

// Caller-owned completion slot (e.g. on the submitting thread's stack) used
// to signal the result of a submitted transaction without any allocation or
// std::future overhead. The caller must keep this alive until `done` is
// observed true.
struct alignas(kCacheLineSize) Completion {
    std::atomic<bool> done{false};
    LedgerError error{LedgerError::None};
};

class LedgerEngine {
public:
    struct Config {
        std::string wal_path;
        wal::SyncMode sync_mode{wal::SyncMode::SyncEveryWrite};
        std::size_t ring_buffer_capacity{4096}; // must be a power of two
        std::size_t idempotency_capacity{1u << 17};
        // Publish a new reader snapshot after this many committed transactions.
        // 1 = publish after every transaction (freshest reads, more copying);
        // higher values trade snapshot staleness for writer throughput.
        std::uint64_t publish_snapshot_every_n{1};
    };

    explicit LedgerEngine(Config config);

    // Opens the WAL for append. Must be called before submit()/process_pending().
    [[nodiscard]] Result<void> open();

    // Setup-time only (before concurrent submit() calls begin): registers a
    // new account in the working state.
    bool add_account(const Account& account) { return state_.working_copy().add_account(account); }

    [[nodiscard]] TransactionId allocate_transaction_id() noexcept {
        return next_tx_id_.fetch_add(1, std::memory_order_relaxed) + 1;
    }

    // Producer side (any thread, including concurrently from many threads):
    // enqueues `tx` for processing. The caller must have already assigned
    // `tx.id` via allocate_transaction_id(). Returns QueueFull immediately if
    // the ring buffer has no free slot (never blocks). `completion` must
    // remain valid until completion.done.load() observes true.
    [[nodiscard]] Result<void> submit(Transaction tx, Completion& completion) noexcept;

    // Consumer side: must be invoked only from the single dedicated writer
    // thread. Drains up to `max_items` pending requests, applying the full
    // validate -> WAL append -> mutate -> ack pipeline. Returns the number of
    // requests processed (0 if the queue was empty).
    std::size_t process_pending(std::size_t max_items = 1024);

    [[nodiscard]] std::shared_ptr<const LedgerState> snapshot() const { return state_.snapshot(); }
    [[nodiscard]] LedgerState& state_working_copy() noexcept { return state_.working_copy(); }
    [[nodiscard]] wal::BinaryWalWriter& wal_writer() noexcept { return wal_writer_; }
    [[nodiscard]] IdempotencyCache& idempotency_cache() noexcept { return idempotency_; }

private:
    struct PendingRequest {
        Transaction tx{};
        Completion* completion{nullptr};
    };

    static void complete(Completion& completion, LedgerError error) noexcept {
        completion.error = error;
        completion.done.store(true, std::memory_order_release);
    }

    Config config_;
    wal::BinaryWalWriter wal_writer_;
    LedgerStateStore state_;
    IdempotencyCache idempotency_;
    MpscRingBuffer<PendingRequest> ring_;
    std::atomic<TransactionId> next_tx_id_{0};
    std::uint64_t processed_since_publish_{0};
};

} // namespace ledger
