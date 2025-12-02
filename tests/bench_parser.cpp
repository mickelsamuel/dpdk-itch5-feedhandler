/**
 * Benchmark for ITCH 5.0 parser
 *
 * Measures:
 * - Message parsing throughput
 * - Different message types
 * - Zero-copy performance
 */

#include "../include/itch5/messages.hpp"
#include "../include/itch5/parser.hpp"
#include "../include/common/endian.hpp"

#include <iostream>
#include <iomanip>
#include <vector>
#include <chrono>
#include <cstring>
#include <random>

using namespace hft;
using namespace hft::itch5;

// Configuration
constexpr size_t NUM_MESSAGES = 10'000'000;
constexpr size_t BUFFER_SIZE = 1024 * 1024;  // 1MB buffer

// Helper to set timestamp
void set_timestamp(uint8_t* ts, uint64_t value) {
    ts[0] = (value >> 40) & 0xFF;
    ts[1] = (value >> 32) & 0xFF;
    ts[2] = (value >> 24) & 0xFF;
    ts[3] = (value >> 16) & 0xFF;
    ts[4] = (value >> 8) & 0xFF;
    ts[5] = value & 0xFF;
}

// Create a random AddOrder message
void create_add_order(AddOrder& msg, uint64_t order_ref, uint64_t timestamp) {
    std::memset(&msg, 0, sizeof(msg));
    msg.message_type = 'A';
    msg.stock_locate = endian::hton16(1);
    msg.tracking_number = endian::hton16(0);
    set_timestamp(msg.timestamp, timestamp);
    msg.order_reference_number = endian::hton64(order_ref);
    msg.buy_sell_indicator = (order_ref % 2 == 0) ? 'B' : 'S';
    msg.shares = endian::hton32(100 + (order_ref % 900));
    std::memcpy(msg.stock, "AAPL    ", 8);
    msg.price = endian::hton32(1500000 + (order_ref % 100000));
}

// Create a random OrderExecuted message
void create_order_executed(OrderExecuted& msg, uint64_t order_ref, uint64_t timestamp) {
    std::memset(&msg, 0, sizeof(msg));
    msg.message_type = 'E';
    msg.stock_locate = endian::hton16(1);
    msg.tracking_number = endian::hton16(0);
    set_timestamp(msg.timestamp, timestamp);
    msg.order_reference_number = endian::hton64(order_ref);
    msg.executed_shares = endian::hton32(50);
    msg.match_number = endian::hton64(timestamp);
}

// Create a random OrderDelete message
void create_order_delete(OrderDelete& msg, uint64_t order_ref, uint64_t timestamp) {
    std::memset(&msg, 0, sizeof(msg));
    msg.message_type = 'D';
    msg.stock_locate = endian::hton16(1);
    msg.tracking_number = endian::hton16(0);
    set_timestamp(msg.timestamp, timestamp);
    msg.order_reference_number = endian::hton64(order_ref);
}

// Benchmark AddOrder parsing only
void bench_add_order_parsing() {
    std::cout << "=== AddOrder Parsing Benchmark ===" << std::endl;

    Parser parser;
    uint64_t callback_count = 0;

    parser.set_add_order_callback(
        [&](const AddOrder* msg, Timestamp ts, Price price, Quantity qty) {
            ++callback_count;
        }
    );

    // Create buffer of AddOrder messages
    std::vector<uint8_t> buffer(sizeof(AddOrder) * NUM_MESSAGES);
    uint8_t* ptr = buffer.data();
    uint64_t timestamp = 34200000000000ULL;

    for (size_t i = 0; i < NUM_MESSAGES; ++i) {
        AddOrder* msg = reinterpret_cast<AddOrder*>(ptr);
        create_add_order(*msg, i, timestamp);
        ptr += sizeof(AddOrder);
        timestamp += 1000;
    }

    // Benchmark parsing
    auto start = std::chrono::high_resolution_clock::now();

    ptr = buffer.data();
    for (size_t i = 0; i < NUM_MESSAGES; ++i) {
        parser.parse_message(ptr, sizeof(AddOrder));
        ptr += sizeof(AddOrder);
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();

    double msgs_per_sec = static_cast<double>(NUM_MESSAGES) * 1e9 / duration;
    double ns_per_msg = static_cast<double>(duration) / NUM_MESSAGES;
    double bytes_per_sec = static_cast<double>(buffer.size()) * 1e9 / duration;

    std::cout << "Messages:       " << NUM_MESSAGES << std::endl;
    std::cout << "Message size:   " << sizeof(AddOrder) << " bytes" << std::endl;
    std::cout << "Total time:     " << duration / 1e6 << " ms" << std::endl;
    std::cout << "Throughput:     " << std::fixed << std::setprecision(2)
              << msgs_per_sec / 1e6 << " million msgs/sec" << std::endl;
    std::cout << "Bandwidth:      " << std::fixed << std::setprecision(2)
              << bytes_per_sec / 1e9 << " GB/sec" << std::endl;
    std::cout << "Latency:        " << std::fixed << std::setprecision(1)
              << ns_per_msg << " ns/msg" << std::endl;
    std::cout << "Callback count: " << callback_count << std::endl;
    std::cout << std::endl;
}

// Benchmark mixed message parsing (realistic workload)
void bench_mixed_messages() {
    std::cout << "=== Mixed Message Parsing Benchmark ===" << std::endl;

    Parser parser;
    uint64_t add_count = 0, exec_count = 0, del_count = 0;

    parser.set_add_order_callback(
        [&](const AddOrder* msg, Timestamp ts, Price price, Quantity qty) {
            ++add_count;
        }
    );
    parser.set_order_executed_callback(
        [&](const OrderExecuted* msg, Timestamp ts) {
            ++exec_count;
        }
    );
    parser.set_order_delete_callback(
        [&](const OrderDelete* msg, Timestamp ts) {
            ++del_count;
        }
    );

    // Create buffer with mixed messages
    // Distribution: 60% AddOrder, 30% OrderExecuted, 10% OrderDelete
    std::vector<uint8_t> buffer;
    buffer.reserve(BUFFER_SIZE);

    std::mt19937 rng(42);
    std::uniform_int_distribution<int> type_dist(1, 100);

    uint64_t timestamp = 34200000000000ULL;
    size_t msg_count = 0;

    while (buffer.size() + sizeof(AddOrder) < BUFFER_SIZE && msg_count < NUM_MESSAGES) {
        int type = type_dist(rng);

        if (type <= 60) {
            AddOrder msg;
            create_add_order(msg, msg_count, timestamp);
            const uint8_t* data = reinterpret_cast<const uint8_t*>(&msg);
            buffer.insert(buffer.end(), data, data + sizeof(msg));
        } else if (type <= 90) {
            OrderExecuted msg;
            create_order_executed(msg, msg_count, timestamp);
            const uint8_t* data = reinterpret_cast<const uint8_t*>(&msg);
            buffer.insert(buffer.end(), data, data + sizeof(msg));
        } else {
            OrderDelete msg;
            create_order_delete(msg, msg_count, timestamp);
            const uint8_t* data = reinterpret_cast<const uint8_t*>(&msg);
            buffer.insert(buffer.end(), data, data + sizeof(msg));
        }

        ++msg_count;
        timestamp += 1000;
    }

    // Benchmark parsing
    auto start = std::chrono::high_resolution_clock::now();

    size_t offset = 0;
    size_t parsed_count = 0;
    while (offset < buffer.size()) {
        char msg_type = static_cast<char>(buffer[offset]);
        size_t msg_size = get_message_size(msg_type);
        if (msg_size == 0 || offset + msg_size > buffer.size()) break;

        parser.parse_message(buffer.data() + offset, msg_size);
        offset += msg_size;
        ++parsed_count;
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();

    double msgs_per_sec = static_cast<double>(parsed_count) * 1e9 / duration;
    double bytes_per_sec = static_cast<double>(buffer.size()) * 1e9 / duration;

    std::cout << "Messages:       " << parsed_count << std::endl;
    std::cout << "Buffer size:    " << buffer.size() / 1024 << " KB" << std::endl;
    std::cout << "Total time:     " << duration / 1e6 << " ms" << std::endl;
    std::cout << "Throughput:     " << std::fixed << std::setprecision(2)
              << msgs_per_sec / 1e6 << " million msgs/sec" << std::endl;
    std::cout << "Bandwidth:      " << std::fixed << std::setprecision(2)
              << bytes_per_sec / 1e9 << " GB/sec" << std::endl;
    std::cout << std::endl;
    std::cout << "Message distribution:" << std::endl;
    std::cout << "  AddOrder:      " << add_count << " ("
              << std::fixed << std::setprecision(1)
              << 100.0 * add_count / parsed_count << "%)" << std::endl;
    std::cout << "  OrderExecuted: " << exec_count << " ("
              << 100.0 * exec_count / parsed_count << "%)" << std::endl;
    std::cout << "  OrderDelete:   " << del_count << " ("
              << 100.0 * del_count / parsed_count << "%)" << std::endl;
    std::cout << std::endl;
}

// Benchmark endianness conversion
void bench_endian_conversion() {
    std::cout << "=== Endianness Conversion Benchmark ===" << std::endl;

    constexpr size_t NUM_CONVERSIONS = 100'000'000;

    // Generate test data
    std::vector<uint32_t> data(NUM_CONVERSIONS);
    std::mt19937 rng(42);
    for (auto& v : data) {
        v = rng();
    }

    // Benchmark 32-bit swaps
    auto start = std::chrono::high_resolution_clock::now();

    uint64_t checksum = 0;
    for (size_t i = 0; i < NUM_CONVERSIONS; ++i) {
        uint32_t swapped = endian::swap32(data[i]);
        checksum += swapped;
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();

    double ops_per_sec = static_cast<double>(NUM_CONVERSIONS) * 1e9 / duration;
    double ns_per_op = static_cast<double>(duration) / NUM_CONVERSIONS;

    std::cout << "Operations:     " << NUM_CONVERSIONS << std::endl;
    std::cout << "Total time:     " << duration / 1e6 << " ms" << std::endl;
    std::cout << "Throughput:     " << std::fixed << std::setprecision(2)
              << ops_per_sec / 1e9 << " billion swaps/sec" << std::endl;
    std::cout << "Latency:        " << std::fixed << std::setprecision(2)
              << ns_per_op << " ns/swap" << std::endl;
    std::cout << "Checksum:       " << checksum << " (for optimization prevention)" << std::endl;
    std::cout << std::endl;
}

// Benchmark zero-copy pointer casting
void bench_zero_copy() {
    std::cout << "=== Zero-Copy Pointer Casting Benchmark ===" << std::endl;

    // Create buffer of messages
    std::vector<uint8_t> buffer(sizeof(AddOrder) * NUM_MESSAGES);
    uint8_t* ptr = buffer.data();
    uint64_t timestamp = 34200000000000ULL;

    for (size_t i = 0; i < NUM_MESSAGES; ++i) {
        AddOrder* msg = reinterpret_cast<AddOrder*>(ptr);
        create_add_order(*msg, i, timestamp);
        ptr += sizeof(AddOrder);
        timestamp += 1000;
    }

    // Benchmark zero-copy access (just pointer casting, no copy)
    auto start = std::chrono::high_resolution_clock::now();

    uint64_t sum = 0;
    ptr = buffer.data();
    for (size_t i = 0; i < NUM_MESSAGES; ++i) {
        // Zero-copy: just cast the pointer
        const AddOrder* msg = reinterpret_cast<const AddOrder*>(ptr);

        // Access fields (forces actual memory read)
        sum += endian::ntoh64(msg->order_reference_number);
        sum += endian::ntoh32(msg->price);
        sum += endian::ntoh32(msg->shares);

        ptr += sizeof(AddOrder);
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();

    double msgs_per_sec = static_cast<double>(NUM_MESSAGES) * 1e9 / duration;
    double ns_per_msg = static_cast<double>(duration) / NUM_MESSAGES;

    std::cout << "Messages:       " << NUM_MESSAGES << std::endl;
    std::cout << "Total time:     " << duration / 1e6 << " ms" << std::endl;
    std::cout << "Throughput:     " << std::fixed << std::setprecision(2)
              << msgs_per_sec / 1e6 << " million msgs/sec" << std::endl;
    std::cout << "Latency:        " << std::fixed << std::setprecision(2)
              << ns_per_msg << " ns/msg" << std::endl;
    std::cout << "Sum:            " << sum << " (for optimization prevention)" << std::endl;
    std::cout << std::endl;
}

int main() {
    std::cout << "==================================================" << std::endl;
    std::cout << "  ITCH 5.0 Parser Benchmark" << std::endl;
    std::cout << "==================================================" << std::endl;
    std::cout << std::endl;
    std::cout << "Message sizes:" << std::endl;
    std::cout << "  AddOrder:       " << sizeof(AddOrder) << " bytes" << std::endl;
    std::cout << "  OrderExecuted:  " << sizeof(OrderExecuted) << " bytes" << std::endl;
    std::cout << "  OrderDelete:    " << sizeof(OrderDelete) << " bytes" << std::endl;
    std::cout << "  Trade:          " << sizeof(Trade) << " bytes" << std::endl;
    std::cout << std::endl;

    bench_endian_conversion();
    bench_zero_copy();
    bench_add_order_parsing();
    bench_mixed_messages();

    std::cout << "==================================================" << std::endl;

    return 0;
}
