#pragma once

#include "../include/common/types.hpp"
#include "../include/dpdk/config.hpp"
#include "../include/dpdk/packet_handler.hpp"
#include "../include/spsc/ring_buffer.hpp"

#include <atomic>
#include <thread>
#include <chrono>
#include <iostream>
#include <iomanip>
#include <fstream>
#include <cstring>

namespace hft {

/**
 * DPDK-based ITCH 5.0 Feed Handler
 *
 * Architecture:
 * - Producer thread: Polls NIC (or PCAP) and parses packets
 * - Consumer thread: Processes normalized messages from ring buffer
 *
 * The ring buffer decouples packet reception from message processing,
 * allowing each to run at maximum speed on dedicated CPU cores.
 */
class FeedHandler {
public:
    using MessageBuffer = spsc::RingBuffer<NormalizedMessage, 65536>;

    explicit FeedHandler(const dpdk::Config& config)
        : config_(config)
        , packet_handler_(message_buffer_)
        , running_(false)
        , producer_running_(false)
        , consumer_running_(false) {}

    ~FeedHandler() {
        stop();
    }

    /**
     * Initialize DPDK and prepare for packet processing
     */
    bool initialize() {
#ifdef USE_DPDK
        // Real DPDK initialization would go here
        // For now, we support PCAP/file mode
        return true;
#else
        // Non-DPDK mode - for development/testing
        return true;
#endif
    }

    /**
     * Start the feed handler threads
     */
    void start() {
        if (running_.load()) {
            return;
        }

        running_.store(true, std::memory_order_release);
        packet_handler_.start();

        // Start producer thread
        producer_thread_ = std::thread([this]() {
            run_producer();
        });

        // Start consumer thread
        consumer_thread_ = std::thread([this]() {
            run_consumer();
        });
    }

    /**
     * Stop the feed handler
     */
    void stop() {
        running_.store(false, std::memory_order_release);
        packet_handler_.stop();

        if (producer_thread_.joinable()) {
            producer_thread_.join();
        }
        if (consumer_thread_.joinable()) {
            consumer_thread_.join();
        }
    }

    /**
     * Process a single ITCH binary file (for testing)
     */
    size_t process_itch_file(const std::string& filename) {
        std::ifstream file(filename, std::ios::binary);
        if (!file) {
            std::cerr << "Failed to open file: " << filename << std::endl;
            return 0;
        }

        // Get file size
        file.seekg(0, std::ios::end);
        size_t file_size = file.tellg();
        file.seekg(0, std::ios::beg);

        // Read entire file into memory
        std::vector<uint8_t> buffer(file_size);
        file.read(reinterpret_cast<char*>(buffer.data()), file_size);

        // Process the ITCH data
        return packet_handler_.process_itch_file_data(buffer.data(), buffer.size());
    }

    /**
     * Process a PCAP file
     */
    size_t process_pcap_file(const std::string& filename) {
        std::ifstream file(filename, std::ios::binary);
        if (!file) {
            std::cerr << "Failed to open PCAP file: " << filename << std::endl;
            return 0;
        }

        // Read PCAP global header (24 bytes)
        uint8_t global_header[24];
        file.read(reinterpret_cast<char*>(global_header), 24);

        // Check magic number
        uint32_t magic = *reinterpret_cast<uint32_t*>(global_header);
        bool swap_bytes = false;
        if (magic == 0xd4c3b2a1) {
            swap_bytes = true;
        } else if (magic != 0xa1b2c3d4) {
            std::cerr << "Invalid PCAP magic number" << std::endl;
            return 0;
        }

        size_t packets_processed = 0;

        // Read packets
        while (file) {
            // Read packet header (16 bytes)
            uint8_t pkt_header[16];
            file.read(reinterpret_cast<char*>(pkt_header), 16);
            if (file.gcount() < 16) break;

            uint32_t incl_len;
            if (swap_bytes) {
                incl_len = __builtin_bswap32(*reinterpret_cast<uint32_t*>(pkt_header + 8));
            } else {
                incl_len = *reinterpret_cast<uint32_t*>(pkt_header + 8);
            }

            // Read packet data
            std::vector<uint8_t> packet(incl_len);
            file.read(reinterpret_cast<char*>(packet.data()), incl_len);
            if (static_cast<size_t>(file.gcount()) < incl_len) break;

            // Process the packet
            if (packet_handler_.process_raw_packet(packet.data(), packet.size())) {
                ++packets_processed;
            }
        }

        return packets_processed;
    }

    // Getters
    bool is_running() const { return running_.load(std::memory_order_acquire); }
    const MessageBuffer& get_message_buffer() const { return message_buffer_; }

    /**
     * Print statistics
     */
    void print_stats() const {
        auto stats = packet_handler_.get_stats();

        std::cout << "\n=== Feed Handler Statistics ===" << std::endl;
        std::cout << "Packets processed:    " << stats.packets_processed << std::endl;
        std::cout << "Bytes processed:      " << stats.bytes_processed << std::endl;
        std::cout << "Invalid packets:      " << stats.invalid_packets << std::endl;
        std::cout << "Messages pushed:      " << stats.messages_pushed << std::endl;
        std::cout << "Buffer full events:   " << stats.buffer_full_count << std::endl;

        std::cout << "\n--- Parser Statistics ---" << std::endl;
        std::cout << "Total messages:       " << stats.parser_stats.total_messages << std::endl;
        std::cout << "Add orders:           " << stats.parser_stats.add_orders << std::endl;
        std::cout << "Order executed:       " << stats.parser_stats.order_executed << std::endl;
        std::cout << "Order deleted:        " << stats.parser_stats.order_deleted << std::endl;
        std::cout << "Order cancelled:      " << stats.parser_stats.order_cancelled << std::endl;
        std::cout << "Order replaced:       " << stats.parser_stats.order_replaced << std::endl;
        std::cout << "Trades:               " << stats.parser_stats.trades << std::endl;
        std::cout << "Other messages:       " << stats.parser_stats.other_messages << std::endl;
        std::cout << "Unknown messages:     " << stats.parser_stats.unknown_messages << std::endl;

        std::cout << "\n--- Session Statistics ---" << std::endl;
        std::cout << "Session packets:      " << stats.session_stats.packets_received << std::endl;
        std::cout << "Session messages:     " << stats.session_stats.messages_received << std::endl;
        std::cout << "Gaps detected:        " << stats.session_stats.gaps_detected << std::endl;
        std::cout << "Heartbeats:           " << stats.session_stats.heartbeats_received << std::endl;

        std::cout << "\n--- Ring Buffer Status ---" << std::endl;
        std::cout << "Buffer size:          " << message_buffer_.size() << std::endl;
        std::cout << "Buffer capacity:      " << message_buffer_.capacity() << std::endl;
        std::cout << "Buffer available:     " << message_buffer_.available() << std::endl;
    }

private:
    /**
     * Producer thread: Poll for packets and parse them
     */
    void run_producer() {
        producer_running_.store(true, std::memory_order_release);

        // Set CPU affinity if configured
        if (config_.pin_to_core) {
#ifdef __linux__
            cpu_set_t cpuset;
            CPU_ZERO(&cpuset);
            CPU_SET(config_.producer_core_id, &cpuset);
            pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
#endif
        }

#ifdef USE_DPDK
        // DPDK poll loop would go here
        // For now, we process files synchronously
        while (running_.load(std::memory_order_acquire)) {
            // Poll for packets using DPDK
            // rte_eth_rx_burst(), etc.
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
#else
        // Non-DPDK mode: just wait for file processing
        while (running_.load(std::memory_order_acquire)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
#endif

        producer_running_.store(false, std::memory_order_release);
    }

    /**
     * Consumer thread: Read messages from ring buffer
     */
    void run_consumer() {
        consumer_running_.store(true, std::memory_order_release);

        // Set CPU affinity if configured
        if (config_.pin_to_core) {
#ifdef __linux__
            cpu_set_t cpuset;
            CPU_ZERO(&cpuset);
            CPU_SET(config_.consumer_core_id, &cpuset);
            pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
#endif
        }

        uint64_t messages_consumed = 0;
        auto last_report = std::chrono::steady_clock::now();

        while (running_.load(std::memory_order_acquire)) {
            // Try to pop messages from the ring buffer
            while (auto msg = message_buffer_.try_pop()) {
                // Process the message
                process_message(*msg);
                ++messages_consumed;
            }

            // Periodic stats report
            auto now = std::chrono::steady_clock::now();
            if (now - last_report > std::chrono::seconds(5)) {
                // Log throughput
                last_report = now;
            }

            // Small pause if buffer was empty
#if defined(__x86_64__) || defined(_M_X64)
            __builtin_ia32_pause();
#endif
        }

        // Drain remaining messages
        while (auto msg = message_buffer_.try_pop()) {
            process_message(*msg);
            ++messages_consumed;
        }

        consumer_running_.store(false, std::memory_order_release);
    }

    /**
     * Process a single normalized message
     * This is where you'd integrate with order book, strategy, etc.
     */
    void process_message(const NormalizedMessage& msg) {
        // In a real system, this would:
        // 1. Update order book state
        // 2. Notify strategy of price changes
        // 3. Log to persistence layer

        // For now, just count messages
        ++total_messages_processed_;
    }

    dpdk::Config config_;
    MessageBuffer message_buffer_;
    dpdk::PacketHandler packet_handler_;

    std::atomic<bool> running_;
    std::atomic<bool> producer_running_;
    std::atomic<bool> consumer_running_;

    std::thread producer_thread_;
    std::thread consumer_thread_;

    uint64_t total_messages_processed_ = 0;
};

} // namespace hft
