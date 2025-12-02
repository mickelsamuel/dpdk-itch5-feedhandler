/**
 * DPDK ITCH 5.0 Feed Handler
 *
 * High-performance market data feed handler for NASDAQ TotalView-ITCH 5.0
 * using DPDK for kernel-bypass packet processing.
 *
 * Features:
 * - Zero-copy packet parsing
 * - Lock-free SPSC ring buffer for producer-consumer architecture
 * - MoldUDP64 session layer with gap detection
 * - CPU core pinning for optimal cache utilization
 * - False sharing prevention in data structures
 *
 * Usage:
 *   ./feed_handler --pcap-file data.pcap       # Process PCAP file
 *   ./feed_handler --itch-file data.itch       # Process raw ITCH file
 *   ./feed_handler --port 0                    # Live capture (requires DPDK)
 *
 * Build with DPDK:
 *   mkdir build && cd build
 *   cmake -DUSE_DPDK=ON ..
 *   make -j$(nproc)
 */

#include "feed_handler.hpp"

#include <iostream>
#include <string>
#include <chrono>
#include <csignal>
#include <getopt.h>

using namespace hft;

// Global feed handler for signal handling
FeedHandler* g_feed_handler = nullptr;

void signal_handler(int signum) {
    std::cout << "\nReceived signal " << signum << ", shutting down..." << std::endl;
    if (g_feed_handler) {
        g_feed_handler->stop();
    }
}

void print_usage(const char* program) {
    std::cout << "Usage: " << program << " [OPTIONS]\n"
              << "\n"
              << "DPDK ITCH 5.0 Feed Handler\n"
              << "\n"
              << "Options:\n"
              << "  -p, --pcap-file FILE    Process PCAP file\n"
              << "  -i, --itch-file FILE    Process raw ITCH binary file\n"
              << "  -P, --port NUM          DPDK port ID for live capture\n"
              << "  -c, --producer-core N   CPU core for packet reception (default: 1)\n"
              << "  -C, --consumer-core N   CPU core for message processing (default: 2)\n"
              << "  -n, --no-pin            Disable CPU core pinning\n"
              << "  -s, --stats             Show statistics after processing\n"
              << "  -v, --verbose           Enable verbose output\n"
              << "  -h, --help              Show this help message\n"
              << "\n"
              << "Examples:\n"
              << "  " << program << " --pcap-file nasdaq_20190130.pcap\n"
              << "  " << program << " --itch-file 01302019.NASDAQ_ITCH50\n"
              << "  " << program << " --port 0 --producer-core 1 --consumer-core 2\n"
              << "\n"
              << "For DPDK live capture, run setup script first:\n"
              << "  sudo ./scripts/setup_dpdk_env.sh setup\n"
              << "\n";
}

void print_banner() {
    std::cout << R"(
  _____ _____ _____ _  __  _____   _____              _   _   _                 _ _
 |_   _|_   _/ ____| |/ / | ____| |  ___|__  ___  __| | | | | | __ _ _ __   __| | | ___ _ __
   | |   | || |    | ' /  | |___  | |_ / _ \/ _ \/ _` | | |_| |/ _` | '_ \ / _` | |/ _ \ '__|
  _| |_  | || |____| . \  |___  | |  _|  __/  __/ (_| | |  _  | (_| | | | | (_| | |  __/ |
 |_____| |_| \_____|_|\_\ |____/  |_|  \___|\___|\__,_| |_| |_|\__,_|_| |_|\__,_|_|\___|_|

 DPDK-based NASDAQ TotalView-ITCH 5.0 Feed Handler
 Zero-copy | Lock-free | Kernel Bypass

)" << std::endl;
}

int main(int argc, char* argv[]) {
    print_banner();

    // Command line options
    static struct option long_options[] = {
        {"pcap-file",     required_argument, 0, 'p'},
        {"itch-file",     required_argument, 0, 'i'},
        {"port",          required_argument, 0, 'P'},
        {"producer-core", required_argument, 0, 'c'},
        {"consumer-core", required_argument, 0, 'C'},
        {"no-pin",        no_argument,       0, 'n'},
        {"stats",         no_argument,       0, 's'},
        {"verbose",       no_argument,       0, 'v'},
        {"help",          no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };

    dpdk::Config config;
    std::string pcap_file;
    std::string itch_file;
    bool show_stats = false;
    bool verbose = false;
    bool live_mode = false;

    int opt;
    while ((opt = getopt_long(argc, argv, "p:i:P:c:C:nsvh", long_options, nullptr)) != -1) {
        switch (opt) {
            case 'p':
                pcap_file = optarg;
                config.use_pcap = true;
                break;
            case 'i':
                itch_file = optarg;
                break;
            case 'P':
                config.port_id = static_cast<uint16_t>(std::stoi(optarg));
                live_mode = true;
                break;
            case 'c':
                config.producer_core_id = std::stoi(optarg);
                break;
            case 'C':
                config.consumer_core_id = std::stoi(optarg);
                break;
            case 'n':
                config.pin_to_core = false;
                break;
            case 's':
                show_stats = true;
                break;
            case 'v':
                verbose = true;
                break;
            case 'h':
                print_usage(argv[0]);
                return 0;
            default:
                print_usage(argv[0]);
                return 1;
        }
    }

    // Validate arguments
    if (pcap_file.empty() && itch_file.empty() && !live_mode) {
        std::cerr << "Error: Must specify --pcap-file, --itch-file, or --port\n" << std::endl;
        print_usage(argv[0]);
        return 1;
    }

    // Create feed handler
    FeedHandler feed_handler(config);
    g_feed_handler = &feed_handler;

    // Set up signal handlers
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    // Initialize
    if (!feed_handler.initialize()) {
        std::cerr << "Failed to initialize feed handler" << std::endl;
        return 1;
    }

    // Record start time
    auto start_time = std::chrono::high_resolution_clock::now();
    size_t result = 0;

    // Process input
    if (!itch_file.empty()) {
        std::cout << "Processing ITCH file: " << itch_file << std::endl;
        result = feed_handler.process_itch_file(itch_file);
        std::cout << "Processed " << result << " messages" << std::endl;
    } else if (!pcap_file.empty()) {
        std::cout << "Processing PCAP file: " << pcap_file << std::endl;
        result = feed_handler.process_pcap_file(pcap_file);
        std::cout << "Processed " << result << " packets" << std::endl;
    } else if (live_mode) {
        std::cout << "Starting live capture on port " << config.port_id << std::endl;
        std::cout << "Producer core: " << config.producer_core_id << std::endl;
        std::cout << "Consumer core: " << config.consumer_core_id << std::endl;

        feed_handler.start();

        std::cout << "Feed handler running. Press Ctrl+C to stop." << std::endl;

        // Wait for signal
        while (feed_handler.is_running()) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }

    // Calculate elapsed time
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);

    std::cout << "\nProcessing time: " << duration.count() << " ms" << std::endl;

    if (result > 0 && duration.count() > 0) {
        double rate = static_cast<double>(result) * 1000.0 / duration.count();
        std::cout << "Throughput: " << std::fixed << std::setprecision(2)
                  << rate << " messages/sec" << std::endl;

        if (rate > 1000000) {
            std::cout << "           " << std::fixed << std::setprecision(2)
                      << rate / 1000000.0 << " million messages/sec" << std::endl;
        }
    }

    // Show statistics
    if (show_stats || verbose) {
        feed_handler.print_stats();
    }

    // Check for gaps (would integrate with order book in production)
    (void)verbose;  // Suppress unused warning

    std::cout << "\nFeed handler terminated successfully." << std::endl;

    return 0;
}
