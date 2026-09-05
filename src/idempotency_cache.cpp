#include "ledger/idempotency_cache.hpp"

#include <algorithm>
#include <bit>

namespace ledger {

IdempotencyCache::IdempotencyCache(std::size_t capacity) {
    capacity = std::max<std::size_t>(capacity, 1);
    nodes_.resize(capacity);

    // Free list initially chains every node together via `next`.
    for (std::size_t i = 0; i < capacity; ++i) {
        nodes_[i].next = (i + 1 < capacity) ? static_cast<std::int32_t>(i + 1) : kInvalid;
    }
    free_head_ = 0;

    const std::size_t bucket_count = std::bit_ceil(std::max<std::size_t>(capacity * 2, 2));
    buckets_.assign(bucket_count, kInvalid);
    bucket_mask_ = bucket_count - 1;
}

std::size_t IdempotencyCache::bucket_for(const IdempotencyKey& key) const noexcept {
    return IdempotencyKeyHash{}(key) & bucket_mask_;
}

void IdempotencyCache::unlink_lru(std::int32_t node_idx) noexcept {
    Node& node = nodes_[node_idx];
    if (node.prev != kInvalid) nodes_[node.prev].next = node.next; else lru_head_ = node.next;
    if (node.next != kInvalid) nodes_[node.next].prev = node.prev; else lru_tail_ = node.prev;
    node.prev = kInvalid;
    node.next = kInvalid;
}

void IdempotencyCache::push_front_lru(std::int32_t node_idx) noexcept {
    Node& node = nodes_[node_idx];
    node.prev = kInvalid;
    node.next = lru_head_;
    if (lru_head_ != kInvalid) nodes_[lru_head_].prev = node_idx;
    lru_head_ = node_idx;
    if (lru_tail_ == kInvalid) lru_tail_ = node_idx;
}

void IdempotencyCache::remove_from_bucket(std::int32_t node_idx) noexcept {
    const std::size_t bucket = bucket_for(nodes_[node_idx].key);
    std::int32_t cur = buckets_[bucket];
    std::int32_t prev = kInvalid;
    while (cur != kInvalid) {
        if (cur == node_idx) {
            if (prev == kInvalid) buckets_[bucket] = nodes_[cur].hash_next; else nodes_[prev].hash_next = nodes_[cur].hash_next;
            nodes_[cur].hash_next = kInvalid;
            return;
        }
        prev = cur;
        cur = nodes_[cur].hash_next;
    }
}

bool IdempotencyCache::check_and_insert(const IdempotencyKey& key) noexcept {
    const std::size_t bucket = bucket_for(key);

    for (std::int32_t cur = buckets_[bucket]; cur != kInvalid; cur = nodes_[cur].hash_next) {
        if (nodes_[cur].occupied && nodes_[cur].key == key) {
            unlink_lru(cur);
            push_front_lru(cur);
            return true; // duplicate
        }
    }

    std::int32_t idx;
    if (free_head_ != kInvalid) {
        idx = free_head_;
        free_head_ = nodes_[idx].next;
        nodes_[idx].next = kInvalid;
        ++size_;
    } else {
        // Cache is full: evict the least-recently-used entry.
        idx = lru_tail_;
        remove_from_bucket(idx);
        unlink_lru(idx);
    }

    nodes_[idx].key = key;
    nodes_[idx].occupied = true;
    nodes_[idx].hash_next = buckets_[bucket];
    buckets_[bucket] = idx;
    push_front_lru(idx);

    return false; // newly inserted, not a duplicate
}

} // namespace ledger
