#pragma once

#include <cstdint>
#include <vector>

#include "ledger/types.hpp"

// Phase 3 (Idempotency & Deduplication): a bounded, fixed-capacity LRU set of
// 128-bit idempotency keys. Backed by flat arrays sized once at construction
// time (a one-time allocation, not on the steady-state hot path) using
// index-based intrusive linked lists + open-addressed hashing, so no heap
// allocation occurs on lookup/insert/evict.

namespace ledger {

class IdempotencyCache {
public:
    explicit IdempotencyCache(std::size_t capacity);

    // Checks whether `key` has already been seen. If not, records it as seen
    // (evicting the least-recently-used entry if the cache is full) and
    // returns false. If it *has* been seen, moves it to most-recently-used
    // and returns true (caller should treat this as a duplicate request).
    [[nodiscard]] bool check_and_insert(const IdempotencyKey& key) noexcept;

    [[nodiscard]] std::size_t size() const noexcept { return size_; }
    [[nodiscard]] std::size_t capacity() const noexcept { return nodes_.size(); }

private:
    static constexpr std::int32_t kInvalid = -1;

    struct Node {
        IdempotencyKey key{};
        std::int32_t prev{kInvalid};
        std::int32_t next{kInvalid};
        std::int32_t hash_next{kInvalid}; // next node in this bucket's chain
        bool occupied{false};
    };

    [[nodiscard]] std::size_t bucket_for(const IdempotencyKey& key) const noexcept;
    void unlink_lru(std::int32_t node_idx) noexcept;
    void push_front_lru(std::int32_t node_idx) noexcept;
    void remove_from_bucket(std::int32_t node_idx) noexcept;

    std::vector<Node> nodes_;
    std::vector<std::int32_t> buckets_; // bucket -> head node index (or kInvalid)
    std::size_t bucket_mask_{0};

    std::int32_t lru_head_{kInvalid}; // most-recently-used
    std::int32_t lru_tail_{kInvalid}; // least-recently-used
    std::int32_t free_head_{kInvalid};
    std::size_t size_{0};
};

} // namespace ledger
