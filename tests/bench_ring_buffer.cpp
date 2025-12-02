/**
 * Benchmark for lock-free SPSC ring buffer
 *
 * Measures:
 * - Single-threaded throughput
 * - Producer-consumer throughput with core pinning
 * - Latency distribution
 */

#include "../include/spsc/ring_buffer.hpp"
#include "../include/common/types.hpp"

#include <iostream>
#include <iomanip>
#include <thread>
#include <chrono>
#include <atomic>
#include <vector>
#include <algorithm>
#include <numeric>

#ifdef __linux__
#include <sched.h>
#include <pthread.h>
#endif

using namespace hft;
using namespace hft::spsc;

// Configuration
constexpr size_t BUFFER_SIZE = 65536;  // 64K entries
constexpr size_t NUM_OPERATIONS = 10'000'000;
constexpr size_t LATENCY_SAMPLES = 100'000;

// Pin thread to CPU core (Linux only)
void pin_to_core(int core_id) {
#ifdef __linux__
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(core_id, &cpuset);
    pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
#else
    (void)core_id;
#endif
}

// Get current time in nanoseconds
inline uint64_t get_nanos() {
    auto now = std::chrono::high_resolution_clock::now();
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        now.time_since_epoch()
    ).count();
}

// Benchmark single-threaded push/pop
void bench_single_threaded() {
    std::cout << "=== Single-Threaded Benchmark ===" << std::endl;

    RingBuffer<uint64_t, BUFFER_SIZE> buffer;

    // Warm up
    for (size_t i = 0; i < 10000; ++i) {
        buffer.try_push(i);
        buffer.try_pop();
    }

    // Benchmark push
    auto start = std::chrono::high_resolution_clock::now();
    for (size_t i = 0; i < NUM_OPERATIONS; ++i) {
        buffer.try_push(i);
        buffer.try_pop();
    }
    auto end = std::chrono::high_resolution_clock::now();

    auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
    double ops_per_sec = static_cast<double>(NUM_OPERATIONS) * 1e9 / duration;
    double ns_per_op = static_cast<double>(duration) / NUM_OPERATIONS;

    std::cout << "Operations:     " << NUM_OPERATIONS << std::endl;
    std::cout << "Total time:     " << duration / 1e6 << " ms" << std::endl;
    std::cout << "Throughput:     " << std::fixed << std::setprecision(2)
              << ops_per_sec / 1e6 << " million ops/sec" << std::endl;
    std::cout << "Latency:        " << std::fixed << std::setprecision(1)
              << ns_per_op << " ns/op" << std::endl;
    std::cout << std::endl;
}

// Benchmark concurrent producer/consumer
void bench_concurrent() {
    std::cout << "=== Concurrent Producer/Consumer Benchmark ===" << std::endl;

    RingBuffer<uint64_t, BUFFER_SIZE> buffer;
    std::atomic<bool> start_flag{false};
    std::atomic<bool> done{false};
    std::atomic<uint64_t> produced{0};
    std::atomic<uint64_t> consumed{0};

    // Producer thread
    std::thread producer([&]() {
        pin_to_core(1);  // Core 1

        // Wait for start signal
        while (!start_flag.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }

        for (uint64_t i = 0; i < NUM_OPERATIONS; ++i) {
            while (!buffer.try_push(i)) {
                // Spin
#if defined(__x86_64__) || defined(_M_X64)
                __builtin_ia32_pause();
#endif
            }
            produced.fetch_add(1, std::memory_order_relaxed);
        }
        done.store(true, std::memory_order_release);
    });

    // Consumer thread
    std::thread consumer([&]() {
        pin_to_core(2);  // Core 2

        // Wait for start signal
        while (!start_flag.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }

        while (!done.load(std::memory_order_acquire) || !buffer.empty()) {
            if (buffer.try_pop()) {
                consumed.fetch_add(1, std::memory_order_relaxed);
            }
#if defined(__x86_64__) || defined(_M_X64)
            __builtin_ia32_pause();
#endif
        }
    });

    // Start timing
    auto start = std::chrono::high_resolution_clock::now();
    start_flag.store(true, std::memory_order_release);

    producer.join();
    consumer.join();

    auto end = std::chrono::high_resolution_clock::now();

    auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
    double ops_per_sec = static_cast<double>(NUM_OPERATIONS) * 1e9 / duration;

    std::cout << "Operations:     " << NUM_OPERATIONS << std::endl;
    std::cout << "Total time:     " << duration / 1e6 << " ms" << std::endl;
    std::cout << "Throughput:     " << std::fixed << std::setprecision(2)
              << ops_per_sec / 1e6 << " million ops/sec" << std::endl;
    std::cout << "Produced:       " << produced.load() << std::endl;
    std::cout << "Consumed:       " << consumed.load() << std::endl;
    std::cout << std::endl;
}

// Benchmark latency distribution
void bench_latency() {
    std::cout << "=== Latency Distribution Benchmark ===" << std::endl;

    RingBuffer<uint64_t, BUFFER_SIZE> buffer;
    std::vector<uint64_t> latencies;
    latencies.reserve(LATENCY_SAMPLES);

    std::atomic<bool> start_flag{false};
    std::atomic<bool> done{false};

    // Consumer thread - measures time between push and pop
    std::thread consumer([&]() {
        pin_to_core(2);

        while (!start_flag.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }

        while (!done.load(std::memory_order_acquire) || !buffer.empty()) {
            if (auto val = buffer.try_pop()) {
                uint64_t now = get_nanos();
                uint64_t latency = now - *val;
                if (latencies.size() < LATENCY_SAMPLES) {
                    latencies.push_back(latency);
                }
            }
        }
    });

    // Producer thread - pushes timestamp as value
    std::thread producer([&]() {
        pin_to_core(1);

        while (!start_flag.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }

        for (size_t i = 0; i < LATENCY_SAMPLES * 2; ++i) {
            uint64_t now = get_nanos();
            while (!buffer.try_push(now)) {
#if defined(__x86_64__) || defined(_M_X64)
                __builtin_ia32_pause();
#endif
            }
            // Small delay to avoid overwhelming consumer
            for (volatile int j = 0; j < 10; ++j) {}
        }
        done.store(true, std::memory_order_release);
    });

    start_flag.store(true, std::memory_order_release);
    producer.join();
    consumer.join();

    if (latencies.empty()) {
        std::cout << "No latency samples collected" << std::endl;
        return;
    }

    // Calculate statistics
    std::sort(latencies.begin(), latencies.end());

    double mean = std::accumulate(latencies.begin(), latencies.end(), 0.0) / latencies.size();
    uint64_t min = latencies.front();
    uint64_t max = latencies.back();
    uint64_t p50 = latencies[latencies.size() * 50 / 100];
    uint64_t p90 = latencies[latencies.size() * 90 / 100];
    uint64_t p99 = latencies[latencies.size() * 99 / 100];
    uint64_t p999 = latencies[latencies.size() * 999 / 1000];

    std::cout << "Samples:        " << latencies.size() << std::endl;
    std::cout << "Min:            " << min << " ns" << std::endl;
    std::cout << "Max:            " << max << " ns" << std::endl;
    std::cout << "Mean:           " << std::fixed << std::setprecision(1) << mean << " ns" << std::endl;
    std::cout << "P50:            " << p50 << " ns" << std::endl;
    std::cout << "P90:            " << p90 << " ns" << std::endl;
    std::cout << "P99:            " << p99 << " ns" << std::endl;
    std::cout << "P99.9:          " << p999 << " ns" << std::endl;
    std::cout << std::endl;
}

// Benchmark with NormalizedMessage (realistic workload)
void bench_normalized_messages() {
    std::cout << "=== NormalizedMessage Throughput Benchmark ===" << std::endl;

    RingBuffer<NormalizedMessage, BUFFER_SIZE> buffer;
    std::atomic<bool> done{false};
    std::atomic<uint64_t> consumed{0};

    NormalizedMessage template_msg;
    template_msg.type = MessageType::AddOrder;
    template_msg.timestamp = 12345678900000ULL;
    template_msg.order_ref = 1;
    template_msg.side = Side::Buy;
    template_msg.price = 1500000;
    template_msg.quantity = 100;

    std::thread consumer([&]() {
        pin_to_core(2);

        while (!done.load(std::memory_order_acquire) || !buffer.empty()) {
            if (buffer.try_pop()) {
                consumed.fetch_add(1, std::memory_order_relaxed);
            }
        }
    });

    auto start = std::chrono::high_resolution_clock::now();

    for (size_t i = 0; i < NUM_OPERATIONS; ++i) {
        template_msg.order_ref = i;
        while (!buffer.try_push(template_msg)) {
#if defined(__x86_64__) || defined(_M_X64)
            __builtin_ia32_pause();
#endif
        }
    }
    done.store(true, std::memory_order_release);

    consumer.join();

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
    double ops_per_sec = static_cast<double>(NUM_OPERATIONS) * 1e9 / duration;

    std::cout << "Message size:   " << sizeof(NormalizedMessage) << " bytes" << std::endl;
    std::cout << "Operations:     " << NUM_OPERATIONS << std::endl;
    std::cout << "Total time:     " << duration / 1e6 << " ms" << std::endl;
    std::cout << "Throughput:     " << std::fixed << std::setprecision(2)
              << ops_per_sec / 1e6 << " million msgs/sec" << std::endl;
    std::cout << "Consumed:       " << consumed.load() << std::endl;
    std::cout << std::endl;
}

int main() {
    std::cout << "==================================================" << std::endl;
    std::cout << "  Lock-Free SPSC Ring Buffer Benchmark" << std::endl;
    std::cout << "==================================================" << std::endl;
    std::cout << std::endl;
    std::cout << "Configuration:" << std::endl;
    std::cout << "  Buffer size:     " << BUFFER_SIZE << " entries" << std::endl;
    std::cout << "  Operations:      " << NUM_OPERATIONS << std::endl;
    std::cout << "  Latency samples: " << LATENCY_SAMPLES << std::endl;
    std::cout << "  Cache line size: " << CACHE_LINE_SIZE << " bytes" << std::endl;
    std::cout << std::endl;

    bench_single_threaded();
    bench_concurrent();
    bench_latency();
    bench_normalized_messages();

    std::cout << "==================================================" << std::endl;

    return 0;
}
