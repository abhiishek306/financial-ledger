#include "ledger/engine.hpp"

#include <ctime>

#include "ledger/validation.hpp"

namespace ledger {
namespace {

std::int64_t monotonic_now_ns() noexcept {
    timespec ts{};
    ::clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<std::int64_t>(ts.tv_sec) * 1'000'000'000LL + ts.tv_nsec;
}

} // namespace

LedgerEngine::LedgerEngine(Config config)
    : config_(config), idempotency_(config.idempotency_capacity), ring_(config.ring_buffer_capacity) {}

Result<void> LedgerEngine::open() {
    return wal_writer_.open(config_.wal_path, config_.sync_mode);
}

Result<void> LedgerEngine::submit(Transaction tx, Completion& completion) noexcept {
    tx.created_at_ns = monotonic_now_ns();
    completion.done.store(false, std::memory_order_relaxed);

    PendingRequest request{std::move(tx), &completion};
    if (!ring_.try_push(request)) return LedgerError::QueueFull;
    return {};
}

std::size_t LedgerEngine::process_pending(std::size_t max_items) {
    std::size_t processed = 0;
    PendingRequest request{};

    while (processed < max_items && ring_.try_pop(request)) {
        ++processed;

        // Deduplicate first: a key seen before is rejected without touching
        // WAL or account state, regardless of what the transaction contains.
        if (idempotency_.check_and_insert(request.tx.idempotency_key)) {
            complete(*request.completion, LedgerError::DuplicateTransaction);
            continue;
        }

        if (Result<void> shape = validate_transaction_shape(request.tx); !shape) {
            complete(*request.completion, shape.error());
            continue;
        }

        LedgerState& working = state_.working_copy();
        AccountLookup lookup = [&working](AccountId id) { return working.find_account(id); };
        if (Result<void> against_state = validate_transaction_against_state(request.tx, lookup); !against_state) {
            complete(*request.completion, against_state.error());
            continue;
        }

        // Durability before visibility: the WAL append (with fsync per the
        // configured SyncMode) must complete before in-memory state changes.
        if (Result<void> appended = wal_writer_.append(request.tx); !appended) {
            complete(*request.completion, appended.error());
            continue;
        }

        // Safe to assume success: shape + against-state validation already
        // guaranteed every referenced account exists and bounds are respected.
        (void)working.apply(request.tx);

        if (++processed_since_publish_ >= config_.publish_snapshot_every_n) {
            state_.publish_snapshot();
            processed_since_publish_ = 0;
        }

        complete(*request.completion, LedgerError::None);
    }

    return processed;
}

} // namespace ledger
