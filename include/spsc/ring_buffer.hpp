#pragma once

#include "../common/types.hpp"

#include <atomic>
#include <array>
#include <cstddef>
#include <new>
#include <type_traits>
#include <optional>

namespace hft {
namespace spsc {

/**
 * Lock-Free Single-Producer Single-Consumer (SPSC) Ring Buffer
 *
 * Design principles for HFT:
 * 1. Cache line padding to prevent false sharing between producer and consumer
 * 2. Power-of-2 capacity for efficient modulo operations (use bitwise AND)
 * 3. Acquire-release memory ordering for thread synchronization
 * 4. No dynamic allocation after construction
 * 5. Bounded wait-free operations
 *
 * Memory ordering explained:
 * - Producer: store with release semantics (ensures data is visible before head update)
 * - Consumer: load with acquire semantics (ensures we see all data written before head)
 *
 * False Sharing Prevention:
 * - head_ and tail_ are on separate cache lines (64 bytes apart)
 * - This prevents cache line ping-pong between CPU cores
 */
template <typename T, size_t Capacity>
class RingBuffer {
    static_assert(Capacity > 0, "Capacity must be positive");
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of 2");
    static_assert(std::is_trivially_copyable_v<T>, "T must be trivially copyable for lock-free operations");

public:
    RingBuffer() : head_(0), tail_(0) {
        // Zero-initialize the buffer
        for (size_t i = 0; i < Capacity; ++i) {
            new (&buffer_[i]) T{};
        }
    }

    // Non-copyable and non-movable (contains atomics)
    RingBuffer(const RingBuffer&) = delete;
    RingBuffer& operator=(const RingBuffer&) = delete;
    RingBuffer(RingBuffer&&) = delete;
    RingBuffer& operator=(RingBuffer&&) = delete;

    /**
     * Try to push an item into the buffer (Producer only)
     * Returns true on success, false if buffer is full
     * Wait-free: completes in bounded number of steps
     */
    bool try_push(const T& item) noexcept {
        const size_t current_head = head_.load(std::memory_order_relaxed);
        const size_t next_head = increment(current_head);

        // Check if buffer is full
        // We need to load tail with acquire to synchronize with consumer's release
        if (next_head == tail_.load(std::memory_order_acquire)) {
            return false;  // Buffer is full
        }

        // Write the data
        buffer_[current_head] = item;

        // Update head with release semantics
        // This ensures the data write is visible to consumer before head update
        head_.store(next_head, std::memory_order_release);

        return true;
    }

    /**
     * Push an item, spinning until space is available (Producer only)
     * Use with caution - can burn CPU cycles
     */
    void push(const T& item) noexcept {
        while (!try_push(item)) {
            // Spin - in production, might want to add pause instruction
            // or yield after N iterations
#if defined(__x86_64__) || defined(_M_X64)
            __builtin_ia32_pause();
#endif
        }
    }

    /**
     * Try to pop an item from the buffer (Consumer only)
     * Returns the item on success, std::nullopt if buffer is empty
     * Wait-free: completes in bounded number of steps
     */
    std::optional<T> try_pop() noexcept {
        const size_t current_tail = tail_.load(std::memory_order_relaxed);

        // Check if buffer is empty
        // We need to load head with acquire to synchronize with producer's release
        if (current_tail == head_.load(std::memory_order_acquire)) {
            return std::nullopt;  // Buffer is empty
        }

        // Read the data
        T item = buffer_[current_tail];

        // Update tail with release semantics
        // This ensures the data read is complete before tail update
        tail_.store(increment(current_tail), std::memory_order_release);

        return item;
    }

    /**
     * Pop an item, spinning until data is available (Consumer only)
     * Use with caution - can burn CPU cycles
     */
    T pop() noexcept {
        while (true) {
            if (auto item = try_pop()) {
                return *item;
            }
#if defined(__x86_64__) || defined(_M_X64)
            __builtin_ia32_pause();
#endif
        }
    }

    /**
     * Peek at the front item without removing it (Consumer only)
     * Returns the item on success, std::nullopt if buffer is empty
     */
    std::optional<T> peek() const noexcept {
        const size_t current_tail = tail_.load(std::memory_order_relaxed);

        if (current_tail == head_.load(std::memory_order_acquire)) {
            return std::nullopt;
        }

        return buffer_[current_tail];
    }

    /**
     * Check if the buffer is empty
     * Note: This is a snapshot - may change immediately after return
     */
    bool empty() const noexcept {
        return head_.load(std::memory_order_acquire) ==
               tail_.load(std::memory_order_acquire);
    }

    /**
     * Check if the buffer is full
     * Note: This is a snapshot - may change immediately after return
     */
    bool full() const noexcept {
        return increment(head_.load(std::memory_order_acquire)) ==
               tail_.load(std::memory_order_acquire);
    }

    /**
     * Get approximate number of items in the buffer
     * Note: This is a snapshot - may change immediately after return
     */
    size_t size() const noexcept {
        const size_t head = head_.load(std::memory_order_acquire);
        const size_t tail = tail_.load(std::memory_order_acquire);

        if (head >= tail) {
            return head - tail;
        } else {
            return Capacity - tail + head;
        }
    }

    /**
     * Get the capacity of the buffer
     */
    static constexpr size_t capacity() noexcept {
        return Capacity;
    }

    /**
     * Get available space in the buffer
     */
    size_t available() const noexcept {
        return Capacity - size() - 1;  // -1 because we can't fill completely
    }

private:
    // Efficient modulo for power-of-2 capacity
    static constexpr size_t increment(size_t index) noexcept {
        return (index + 1) & (Capacity - 1);
    }

    // Buffer storage
    // Aligned to cache line to prevent false sharing with adjacent data
    alignas(CACHE_LINE_SIZE) std::array<T, Capacity> buffer_;

    // Producer index (only written by producer, read by consumer)
    // Aligned to separate cache line to prevent false sharing with tail_
    alignas(CACHE_LINE_SIZE) std::atomic<size_t> head_;

    // Padding to ensure tail_ is on a different cache line
    char padding_[CACHE_LINE_SIZE - sizeof(std::atomic<size_t>)];

    // Consumer index (only written by consumer, read by producer)
    // On its own cache line due to alignas
    alignas(CACHE_LINE_SIZE) std::atomic<size_t> tail_;
};

/**
 * Batch operations for higher throughput
 * These can amortize the cost of atomic operations over multiple items
 */
template <typename T, size_t Capacity>
class BatchRingBuffer : public RingBuffer<T, Capacity> {
public:
    using Base = RingBuffer<T, Capacity>;

    /**
     * Try to push multiple items at once
     * Returns number of items actually pushed
     */
    size_t try_push_batch(const T* items, size_t count) noexcept {
        size_t pushed = 0;
        for (size_t i = 0; i < count; ++i) {
            if (!Base::try_push(items[i])) {
                break;
            }
            ++pushed;
        }
        return pushed;
    }

    /**
     * Try to pop multiple items at once
     * Returns number of items actually popped
     */
    size_t try_pop_batch(T* items, size_t max_count) noexcept {
        size_t popped = 0;
        for (size_t i = 0; i < max_count; ++i) {
            if (auto item = Base::try_pop()) {
                items[i] = *item;
                ++popped;
            } else {
                break;
            }
        }
        return popped;
    }
};

// Type alias for common message buffer size (64K entries)
using MessageBuffer = RingBuffer<NormalizedMessage, 65536>;

} // namespace spsc
} // namespace hft
