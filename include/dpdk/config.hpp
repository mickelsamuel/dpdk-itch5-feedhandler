#pragma once

#include <cstdint>
#include <cstddef>
#include <string>

namespace hft {
namespace dpdk {

/**
 * DPDK Configuration Settings
 *
 * These values are tuned for HFT workloads:
 * - Large mempool for burst traffic
 * - Optimized ring sizes
 * - Cache-aligned buffer sizes
 */
struct Config {
    // EAL (Environment Abstraction Layer) settings
    static constexpr int DEFAULT_CORE_COUNT = 2;
    static constexpr int PRODUCER_CORE = 1;     // Core for packet reception
    static constexpr int CONSUMER_CORE = 2;     // Core for packet processing

    // Memory pool settings
    static constexpr uint32_t NUM_MBUFS = 8192;
    static constexpr uint32_t MBUF_CACHE_SIZE = 256;

    // Ring buffer sizes (must be power of 2)
    static constexpr uint16_t RX_RING_SIZE = 1024;
    static constexpr uint16_t TX_RING_SIZE = 1024;

    // Burst size for polling
    static constexpr uint16_t BURST_SIZE = 32;

    // Maximum packet size
    static constexpr uint16_t MAX_PKT_SIZE = 2048;

    // Port configuration
    static constexpr uint16_t NUM_RX_QUEUES = 1;
    static constexpr uint16_t NUM_TX_QUEUES = 1;

    // Hugepage memory settings
    static constexpr size_t HUGEPAGE_SIZE = 2 * 1024 * 1024;  // 2MB hugepages

    // Poll mode driver settings
    static constexpr uint32_t PMD_POLL_TIMEOUT_US = 0;  // No timeout (busy poll)

    // Multicast group for NASDAQ ITCH (example)
    static constexpr const char* DEFAULT_MULTICAST_GROUP = "233.54.12.111";
    static constexpr uint16_t DEFAULT_MULTICAST_PORT = 26477;

    // PCAP file for testing (when not using live NIC)
    std::string pcap_file;

    // Port ID to use
    uint16_t port_id = 0;

    // Whether to use PCAP PMD instead of real NIC
    bool use_pcap = false;

    // Whether to run in promiscuous mode
    bool promiscuous = true;

    // CPU core affinity settings
    bool pin_to_core = true;
    int producer_core_id = PRODUCER_CORE;
    int consumer_core_id = CONSUMER_CORE;
};

// Network header sizes for offset calculations
namespace header_sizes {
    constexpr size_t ETHERNET = 14;
    constexpr size_t IPV4 = 20;         // Without options
    constexpr size_t IPV4_MAX = 60;     // With options
    constexpr size_t UDP = 8;
    constexpr size_t MOLDUDP64 = 20;

    // Total header size for typical ITCH packet
    constexpr size_t TOTAL_MIN = ETHERNET + IPV4 + UDP + MOLDUDP64;
}

// Ethernet header structure
#pragma pack(push, 1)
struct EthernetHeader {
    uint8_t dst_mac[6];
    uint8_t src_mac[6];
    uint16_t ether_type;
};
static_assert(sizeof(EthernetHeader) == 14, "Ethernet header must be 14 bytes");

// IPv4 header structure
struct IPv4Header {
    uint8_t version_ihl;        // Version (4 bits) + IHL (4 bits)
    uint8_t tos;
    uint16_t total_length;
    uint16_t identification;
    uint16_t flags_fragment;
    uint8_t ttl;
    uint8_t protocol;
    uint16_t checksum;
    uint32_t src_addr;
    uint32_t dst_addr;
};
static_assert(sizeof(IPv4Header) == 20, "IPv4 header must be 20 bytes");

// UDP header structure
struct UDPHeader {
    uint16_t src_port;
    uint16_t dst_port;
    uint16_t length;
    uint16_t checksum;
};
static_assert(sizeof(UDPHeader) == 8, "UDP header must be 8 bytes");
#pragma pack(pop)

// Helper to get IHL (IP Header Length) in bytes
inline uint8_t get_ip_header_length(const IPv4Header* ip) {
    return (ip->version_ihl & 0x0F) * 4;
}

// Protocol numbers
constexpr uint8_t IP_PROTO_UDP = 17;
constexpr uint16_t ETHER_TYPE_IPV4 = 0x0800;

} // namespace dpdk
} // namespace hft
