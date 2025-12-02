/**
 * Unit tests for the lock-free SPSC ring buffer
 *
 * Tests:
 * - Basic push/pop operations
 * - Boundary conditions (empty, full)
 * - Thread safety (producer/consumer)
 * - Performance characteristics
 */

#include "../include/spsc/ring_buffer.hpp"
#include "../include/common/types.hpp"

#include <iostream>
#include <thread>
#include <atomic>
#include <chrono>
#include <cassert>
#include <cstring>

using namespace hft;
using namespace hft::spsc;

// Test configuration
constexpr size_t BUFFER_SIZE = 1024;  // Must be power of 2
constexpr size_t NUM_MESSAGES = 100000;

// Simple test item
struct TestItem {
    uint64_t value;
    uint64_t timestamp;
    char data[48];  // Pad to 64 bytes (cache line size)
};

// Test helper
#define TEST_ASSERT(condition, message) \
    do { \
        if (!(condition)) { \
            std::cerr << "FAIL: " << message << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
            return false; \
        } \
    } while(0)

#define TEST_PASS(name) \
    std::cout << "PASS: " << name << std::endl

// Test basic push/pop operations
bool test_basic_operations() {
    RingBuffer<TestItem, BUFFER_SIZE> buffer;

    // Test empty buffer
    TEST_ASSERT(buffer.empty(), "Buffer should be empty initially");
    TEST_ASSERT(buffer.size() == 0, "Buffer size should be 0");
    TEST_ASSERT(!buffer.full(), "Buffer should not be full");

    // Test pop from empty
    TEST_ASSERT(!buffer.try_pop().has_value(), "Pop from empty should return nullopt");

    // Test single push/pop
    TestItem item1{42, 12345, "test"};
    TEST_ASSERT(buffer.try_push(item1), "Push should succeed");
    TEST_ASSERT(!buffer.empty(), "Buffer should not be empty after push");
    TEST_ASSERT(buffer.size() == 1, "Buffer size should be 1");

    auto popped = buffer.try_pop();
    TEST_ASSERT(popped.has_value(), "Pop should succeed");
    TEST_ASSERT(popped->value == 42, "Popped value should match");
    TEST_ASSERT(popped->timestamp == 12345, "Popped timestamp should match");
    TEST_ASSERT(buffer.empty(), "Buffer should be empty after pop");

    // Test fill buffer completely
    for (size_t i = 0; i < BUFFER_SIZE - 1; ++i) {  // -1 because one slot is always empty
        TestItem item{i, i * 100, ""};
        TEST_ASSERT(buffer.try_push(item), "Push should succeed");
    }
    TEST_ASSERT(buffer.full(), "Buffer should be full");

    // Test push to full buffer
    TestItem extra{999, 999, ""};
    TEST_ASSERT(!buffer.try_push(extra), "Push to full buffer should fail");

    // Drain buffer
    for (size_t i = 0; i < BUFFER_SIZE - 1; ++i) {
        auto item = buffer.try_pop();
        TEST_ASSERT(item.has_value(), "Pop should succeed");
        TEST_ASSERT(item->value == i, "Values should match in order");
    }
    TEST_ASSERT(buffer.empty(), "Buffer should be empty after draining");

    TEST_PASS("test_basic_operations");
    return true;
}

// Test FIFO ordering
bool test_fifo_ordering() {
    RingBuffer<uint64_t, BUFFER_SIZE> buffer;

    // Push sequence
    for (uint64_t i = 0; i < 100; ++i) {
        buffer.try_push(i);
    }

    // Pop and verify order
    for (uint64_t i = 0; i < 100; ++i) {
        auto val = buffer.try_pop();
        TEST_ASSERT(val.has_value(), "Pop should succeed");
        TEST_ASSERT(*val == i, "FIFO order should be maintained");
    }

    TEST_PASS("test_fifo_ordering");
    return true;
}

// Test wrap-around behavior
bool test_wraparound() {
    RingBuffer<uint64_t, 8> buffer;  // Small buffer to test wraparound

    // Fill and drain multiple times
    for (int round = 0; round < 10; ++round) {
        // Push some items
        for (uint64_t i = 0; i < 5; ++i) {
            TEST_ASSERT(buffer.try_push(round * 10 + i), "Push should succeed");
        }

        // Pop some items
        for (uint64_t i = 0; i < 5; ++i) {
            auto val = buffer.try_pop();
            TEST_ASSERT(val.has_value(), "Pop should succeed");
            TEST_ASSERT(*val == static_cast<uint64_t>(round * 10 + i), "Value should match");
        }
    }

    TEST_PASS("test_wraparound");
    return true;
}

// Test peek operation
bool test_peek() {
    RingBuffer<uint64_t, BUFFER_SIZE> buffer;

    // Peek empty buffer
    TEST_ASSERT(!buffer.peek().has_value(), "Peek empty should return nullopt");

    // Push and peek
    buffer.try_push(42);
    auto peeked = buffer.peek();
    TEST_ASSERT(peeked.has_value(), "Peek should succeed");
    TEST_ASSERT(*peeked == 42, "Peeked value should match");

    // Peek should not remove item
    peeked = buffer.peek();
    TEST_ASSERT(peeked.has_value(), "Peek should still succeed");
    TEST_ASSERT(*peeked == 42, "Peeked value should still be 42");

    // Pop should return same value
    auto popped = buffer.try_pop();
    TEST_ASSERT(popped.has_value(), "Pop should succeed");
    TEST_ASSERT(*popped == 42, "Popped value should match");

    TEST_PASS("test_peek");
    return true;
}

// Test concurrent producer/consumer
bool test_concurrent_spsc() {
    RingBuffer<uint64_t, BUFFER_SIZE> buffer;
    std::atomic<bool> done{false};
    std::atomic<uint64_t> produced{0};
    std::atomic<uint64_t> consumed{0};
    uint64_t last_consumed = 0;
    bool order_error = false;

    // Producer thread
    std::thread producer([&]() {
        for (uint64_t i = 0; i < NUM_MESSAGES; ++i) {
            while (!buffer.try_push(i)) {
                // Spin until space available
                std::this_thread::yield();
            }
            produced.fetch_add(1, std::memory_order_relaxed);
        }
        done.store(true, std::memory_order_release);
    });

    // Consumer thread
    std::thread consumer([&]() {
        while (!done.load(std::memory_order_acquire) || !buffer.empty()) {
            if (auto val = buffer.try_pop()) {
                if (*val != last_consumed) {
                    order_error = true;
                }
                last_consumed = *val + 1;
                consumed.fetch_add(1, std::memory_order_relaxed);
            }
        }
    });

    producer.join();
    consumer.join();

    TEST_ASSERT(!order_error, "FIFO order should be maintained in concurrent test");
    TEST_ASSERT(produced.load() == NUM_MESSAGES, "All messages should be produced");
    TEST_ASSERT(consumed.load() == NUM_MESSAGES, "All messages should be consumed");
    TEST_ASSERT(buffer.empty(), "Buffer should be empty after test");

    TEST_PASS("test_concurrent_spsc");
    return true;
}

// Test with NormalizedMessage type
bool test_normalized_message() {
    RingBuffer<NormalizedMessage, BUFFER_SIZE> buffer;

    NormalizedMessage msg;
    msg.type = MessageType::AddOrder;
    msg.timestamp = 123456789;
    msg.order_ref = 42;
    msg.side = Side::Buy;
    msg.price = 1000000;  // $1.00
    msg.quantity = 100;

    TEST_ASSERT(buffer.try_push(msg), "Push should succeed");

    auto popped = buffer.try_pop();
    TEST_ASSERT(popped.has_value(), "Pop should succeed");
    TEST_ASSERT(popped->type == MessageType::AddOrder, "Type should match");
    TEST_ASSERT(popped->timestamp == 123456789, "Timestamp should match");
    TEST_ASSERT(popped->order_ref == 42, "Order ref should match");
    TEST_ASSERT(popped->side == Side::Buy, "Side should match");
    TEST_ASSERT(popped->price == 1000000, "Price should match");
    TEST_ASSERT(popped->quantity == 100, "Quantity should match");

    TEST_PASS("test_normalized_message");
    return true;
}

// Test cache line alignment
bool test_alignment() {
    RingBuffer<uint64_t, BUFFER_SIZE> buffer;

    // Verify the buffer structure is properly aligned
    // In a real test, we'd check actual memory addresses
    TEST_ASSERT(alignof(decltype(buffer)) >= CACHE_LINE_SIZE,
                "Buffer should be cache-line aligned");

    // Verify CACHE_LINE_SIZE is reasonable
    TEST_ASSERT(CACHE_LINE_SIZE == 64 || CACHE_LINE_SIZE == 128,
                "Cache line size should be 64 or 128 bytes");

    TEST_PASS("test_alignment");
    return true;
}

int main() {
    std::cout << "=== Lock-Free SPSC Ring Buffer Tests ===" << std::endl;
    std::cout << "Buffer size: " << BUFFER_SIZE << std::endl;
    std::cout << "Concurrent test messages: " << NUM_MESSAGES << std::endl;
    std::cout << "Cache line size: " << CACHE_LINE_SIZE << " bytes" << std::endl;
    std::cout << std::endl;

    int passed = 0;
    int failed = 0;

    auto run_test = [&](bool (*test)(), const char* name) {
        try {
            if (test()) {
                ++passed;
            } else {
                ++failed;
            }
        } catch (const std::exception& e) {
            std::cerr << "FAIL: " << name << " threw exception: " << e.what() << std::endl;
            ++failed;
        }
    };

    run_test(test_basic_operations, "test_basic_operations");
    run_test(test_fifo_ordering, "test_fifo_ordering");
    run_test(test_wraparound, "test_wraparound");
    run_test(test_peek, "test_peek");
    run_test(test_concurrent_spsc, "test_concurrent_spsc");
    run_test(test_normalized_message, "test_normalized_message");
    run_test(test_alignment, "test_alignment");

    std::cout << std::endl;
    std::cout << "=== Results ===" << std::endl;
    std::cout << "Passed: " << passed << std::endl;
    std::cout << "Failed: " << failed << std::endl;

    return failed == 0 ? 0 : 1;
}
