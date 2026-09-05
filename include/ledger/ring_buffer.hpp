#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <new>
#include <vector>

// Phase 3: bounded lock-free multi-producer / single-consumer ring buffer
// (Dmitry Vyukov's bounded MPMC queue algorithm, restricted here to a single
// consumer, which is exactly the "single-writer event loop" the engine uses).
//
// Each slot carries a `sequence` number that lets producers/consumers detect
// whether a slot is ready to be written/read without any locks, using only
// atomic compare-exchange / load-store with acquire/release ordering.
//
// Capacity must be a power of two. Storage is allocated once at construction
// (not on the hot path) and never reallocated afterwards.

namespace ledger {

// Fixed at 64 bytes (typical x86_64/aarch64 cache line) rather than querying
// std::hardware_destructive_interference_size, whose value is intentionally
// tied to -mtune/-mcpu and triggers -Werror=interference-size otherwise.
inline constexpr std::size_t kCacheLineSize = 64;

template <typename T>
class MpscRingBuffer {
public:
    explicit MpscRingBuffer(std::size_t capacity_pow2)
        : mask_(capacity_pow2 - 1), buffer_(capacity_pow2) {
        // Capacity must be a power of two so `index & mask_` wraps correctly.
        for (std::size_t i = 0; i < capacity_pow2; ++i) {
            buffer_[i].sequence.store(i, std::memory_order_relaxed);
        }
    }

    MpscRingBuffer(const MpscRingBuffer&) = delete;
    MpscRingBuffer& operator=(const MpscRingBuffer&) = delete;

    // Producer side (may be called concurrently from multiple threads).
    // Returns false if the queue is full.
    [[nodiscard]] bool try_push(const T& item) noexcept {
        Cell* cell;
        std::size_t pos = enqueue_pos_.load(std::memory_order_relaxed);
        for (;;) {
            cell = &buffer_[pos & mask_];
            const std::size_t seq = cell->sequence.load(std::memory_order_acquire);
            const std::intptr_t diff = static_cast<std::intptr_t>(seq) - static_cast<std::intptr_t>(pos);

            if (diff == 0) {
                if (enqueue_pos_.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed)) {
                    break;
                }
            } else if (diff < 0) {
                return false; // queue full
            } else {
                pos = enqueue_pos_.load(std::memory_order_relaxed);
            }
        }

        cell->data = item;
        cell->sequence.store(pos + 1, std::memory_order_release);
        return true;
    }

    // Consumer side (must only be called from a single thread).
    // Returns false if the queue is empty.
    [[nodiscard]] bool try_pop(T& out) noexcept {
        Cell* cell = &buffer_[dequeue_pos_ & mask_];
        const std::size_t seq = cell->sequence.load(std::memory_order_acquire);
        const std::intptr_t diff = static_cast<std::intptr_t>(seq) - static_cast<std::intptr_t>(dequeue_pos_ + 1);

        if (diff != 0) return false; // empty (or producer still mid-write)

        out = cell->data;
        cell->sequence.store(dequeue_pos_ + mask_ + 1, std::memory_order_release);
        ++dequeue_pos_;
        return true;
    }

private:
    struct Cell {
        std::atomic<std::size_t> sequence;
        T data;
    };

    alignas(kCacheLineSize) std::size_t mask_;
    std::vector<Cell> buffer_;

    alignas(kCacheLineSize) std::atomic<std::size_t> enqueue_pos_{0};
    alignas(kCacheLineSize) std::size_t dequeue_pos_{0};
};

} // namespace ledger
