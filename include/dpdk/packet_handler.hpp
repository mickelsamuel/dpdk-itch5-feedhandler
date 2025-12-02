#pragma once

#include "config.hpp"
#include "../common/types.hpp"
#include "../common/endian.hpp"
#include "../moldudp64/header.hpp"
#include "../moldudp64/session.hpp"
#include "../itch5/parser.hpp"
#include "../spsc/ring_buffer.hpp"

#include <cstdint>
#include <cstddef>
#include <functional>
#include <atomic>

// Forward declarations for DPDK types
// These would be included from DPDK headers in actual build
#ifdef USE_DPDK
#include <rte_mbuf.h>
#include <rte_ether.h>
#include <rte_ip.h>
#include <rte_udp.h>
#else
// Mock types for compilation without DPDK
struct rte_mbuf {
    uint16_t data_len;
    uint16_t pkt_len;
    void* buf_addr;
    uint16_t data_off;
};

// Mock DPDK macros/functions for compilation without DPDK
// In real DPDK, these are macros that return typed pointers
#define rte_pktmbuf_mtod(m, t) \
    reinterpret_cast<t>(static_cast<uint8_t*>((m)->buf_addr) + (m)->data_off)

#define rte_pktmbuf_mtod_offset(m, t, o) \
    reinterpret_cast<t>(static_cast<uint8_t*>((m)->buf_addr) + (m)->data_off + (o))
#endif

namespace hft {
namespace dpdk {

/**
 * Zero-Copy Packet Handler
 *
 * This class processes packets directly from DPDK mbufs without copying.
 * The key optimization is casting pointers directly to struct types,
 * avoiding memcpy operations.
 *
 * Thread model:
 * - Producer thread: Calls process_mbuf() from DPDK poll loop
 * - Consumer thread: Reads from ring buffer for downstream processing
 */
class PacketHandler {
public:
    using MessageBuffer = spsc::RingBuffer<NormalizedMessage, 65536>;

    explicit PacketHandler(MessageBuffer& output_buffer)
        : output_buffer_(output_buffer)
        , running_(false)
        , packets_processed_(0)
        , bytes_processed_(0)
        , invalid_packets_(0) {

        // Set up ITCH parser callbacks
        setup_parser_callbacks();

        // Set up MoldUDP64 session callback
        session_.set_message_callback(
            [this](const uint8_t* data, uint16_t length, SequenceNumber seq) {
                parse_itch_message(data, length);
            }
        );
    }

    /**
     * Process a single mbuf (zero-copy)
     * This is called from the DPDK poll loop on the producer core
     *
     * Zero-copy principle:
     * We cast the mbuf data pointer directly to our header structs.
     * No memcpy is performed - we read directly from DMA'd memory.
     */
    bool process_mbuf(rte_mbuf* mbuf) {
        if (!mbuf || mbuf->pkt_len < header_sizes::TOTAL_MIN) {
            ++invalid_packets_;
            return false;
        }

        // Get pointer to start of packet data (zero-copy)
        const uint8_t* pkt_data = static_cast<const uint8_t*>(
            rte_pktmbuf_mtod(mbuf, void*)
        );

        size_t offset = 0;

        // Parse Ethernet header (zero-copy cast)
        const auto* eth = reinterpret_cast<const EthernetHeader*>(pkt_data);
        offset += sizeof(EthernetHeader);

        // Check if IPv4
        if (endian::ntoh16(eth->ether_type) != ETHER_TYPE_IPV4) {
            ++invalid_packets_;
            return false;
        }

        // Parse IPv4 header (zero-copy cast)
        const auto* ip = reinterpret_cast<const IPv4Header*>(pkt_data + offset);

        // Get actual IP header length (can have options)
        uint8_t ip_hdr_len = get_ip_header_length(ip);
        offset += ip_hdr_len;

        // Check if UDP
        if (ip->protocol != IP_PROTO_UDP) {
            ++invalid_packets_;
            return false;
        }

        // Parse UDP header (zero-copy cast)
        const auto* udp = reinterpret_cast<const UDPHeader*>(pkt_data + offset);
        offset += sizeof(UDPHeader);

        // Remaining data is MoldUDP64 + ITCH messages
        size_t payload_len = mbuf->pkt_len - offset;

        // Process MoldUDP64 packet
        if (!session_.process_packet(pkt_data + offset, payload_len)) {
            ++invalid_packets_;
            return false;
        }

        ++packets_processed_;
        bytes_processed_ += mbuf->pkt_len;

        return true;
    }

    /**
     * Process raw packet data (for PCAP playback or testing)
     */
    bool process_raw_packet(const uint8_t* data, size_t len) {
        if (len < header_sizes::TOTAL_MIN) {
            ++invalid_packets_;
            return false;
        }

        size_t offset = 0;

        // Parse Ethernet header
        const auto* eth = reinterpret_cast<const EthernetHeader*>(data);
        offset += sizeof(EthernetHeader);

        if (endian::ntoh16(eth->ether_type) != ETHER_TYPE_IPV4) {
            ++invalid_packets_;
            return false;
        }

        // Parse IPv4 header
        const auto* ip = reinterpret_cast<const IPv4Header*>(data + offset);
        uint8_t ip_hdr_len = get_ip_header_length(ip);
        offset += ip_hdr_len;

        if (ip->protocol != IP_PROTO_UDP) {
            ++invalid_packets_;
            return false;
        }

        // Skip UDP header
        offset += sizeof(UDPHeader);

        // Process MoldUDP64 packet
        size_t payload_len = len - offset;
        if (!session_.process_packet(data + offset, payload_len)) {
            ++invalid_packets_;
            return false;
        }

        ++packets_processed_;
        bytes_processed_ += len;

        return true;
    }

    /**
     * Process raw ITCH binary data (for file-based testing)
     * This is for processing raw ITCH files without network headers
     */
    size_t process_itch_file_data(const uint8_t* data, size_t len) {
        size_t offset = 0;
        size_t messages_processed = 0;

        while (offset + 2 < len) {  // Need at least 2 bytes for length
            // ITCH file format: 2-byte big-endian length prefix
            uint16_t msg_len = endian::read_be16(data + offset);
            offset += 2;

            if (offset + msg_len > len) {
                break;  // Incomplete message
            }

            // Parse the ITCH message
            size_t parsed = parser_.parse_message(data + offset, msg_len);
            if (parsed > 0) {
                ++messages_processed;
            }

            offset += msg_len;
        }

        return messages_processed;
    }

    // Control
    void start() { running_.store(true, std::memory_order_release); }
    void stop() { running_.store(false, std::memory_order_release); }
    bool is_running() const { return running_.load(std::memory_order_acquire); }

    // Statistics
    struct Stats {
        uint64_t packets_processed;
        uint64_t bytes_processed;
        uint64_t invalid_packets;
        uint64_t messages_pushed;
        uint64_t buffer_full_count;
        itch5::Parser::Stats parser_stats;
        moldudp64::Session::Stats session_stats;
    };

    Stats get_stats() const {
        Stats s;
        s.packets_processed = packets_processed_;
        s.bytes_processed = bytes_processed_;
        s.invalid_packets = invalid_packets_;
        s.messages_pushed = messages_pushed_;
        s.buffer_full_count = buffer_full_count_;
        s.parser_stats = parser_.get_stats();
        s.session_stats = session_.get_stats();
        return s;
    }

    // Access to session for gap detection
    const moldudp64::Session& get_session() const { return session_; }
    bool has_gaps() const { return session_.has_gaps(); }

private:
    void setup_parser_callbacks() {
        // Add Order callback
        parser_.set_add_order_callback(
            [this](const itch5::AddOrder* msg, Timestamp ts, Price price, Quantity qty) {
                NormalizedMessage norm;
                norm.type = MessageType::AddOrder;
                norm.timestamp = ts;
                norm.order_ref = endian::ntoh64(msg->order_reference_number);
                std::memcpy(norm.stock.data(), msg->stock, 8);
                norm.side = (msg->buy_sell_indicator == 'B') ? Side::Buy : Side::Sell;
                norm.price = price;
                norm.quantity = qty;

                push_message(norm);
            }
        );

        // Order Executed callback
        parser_.set_order_executed_callback(
            [this](const itch5::OrderExecuted* msg, Timestamp ts) {
                NormalizedMessage norm;
                norm.type = MessageType::OrderExecuted;
                norm.timestamp = ts;
                norm.order_ref = endian::ntoh64(msg->order_reference_number);
                norm.executed_quantity = endian::ntoh32(msg->executed_shares);

                push_message(norm);
            }
        );

        // Order Delete callback
        parser_.set_order_delete_callback(
            [this](const itch5::OrderDelete* msg, Timestamp ts) {
                NormalizedMessage norm;
                norm.type = MessageType::OrderDelete;
                norm.timestamp = ts;
                norm.order_ref = endian::ntoh64(msg->order_reference_number);

                push_message(norm);
            }
        );

        // Order Cancel callback
        parser_.set_order_cancel_callback(
            [this](const itch5::OrderCancel* msg, Timestamp ts) {
                NormalizedMessage norm;
                norm.type = MessageType::OrderCancel;
                norm.timestamp = ts;
                norm.order_ref = endian::ntoh64(msg->order_reference_number);
                norm.quantity = endian::ntoh32(msg->cancelled_shares);

                push_message(norm);
            }
        );

        // Order Replace callback
        parser_.set_order_replace_callback(
            [this](const itch5::OrderReplace* msg, Timestamp ts, Price price, Quantity qty) {
                NormalizedMessage norm;
                norm.type = MessageType::OrderReplace;
                norm.timestamp = ts;
                norm.order_ref = endian::ntoh64(msg->original_order_reference_number);
                norm.new_order_ref = endian::ntoh64(msg->new_order_reference_number);
                norm.price = price;
                norm.quantity = qty;

                push_message(norm);
            }
        );

        // Trade callback
        parser_.set_trade_callback(
            [this](const itch5::Trade* msg, Timestamp ts, Price price, Quantity qty) {
                NormalizedMessage norm;
                norm.type = MessageType::Trade;
                norm.timestamp = ts;
                norm.order_ref = endian::ntoh64(msg->order_reference_number);
                std::memcpy(norm.stock.data(), msg->stock, 8);
                norm.side = (msg->buy_sell_indicator == 'B') ? Side::Buy : Side::Sell;
                norm.price = price;
                norm.quantity = qty;

                push_message(norm);
            }
        );
    }

    void parse_itch_message(const uint8_t* data, uint16_t length) {
        parser_.parse_message(data, length);
    }

    void push_message(const NormalizedMessage& msg) {
        if (!output_buffer_.try_push(msg)) {
            ++buffer_full_count_;
            // In production, might want to log or handle this differently
        } else {
            ++messages_pushed_;
        }
    }

    MessageBuffer& output_buffer_;
    itch5::Parser parser_;
    moldudp64::Session session_;

    std::atomic<bool> running_;

    // Statistics
    uint64_t packets_processed_ = 0;
    uint64_t bytes_processed_ = 0;
    uint64_t invalid_packets_ = 0;
    uint64_t messages_pushed_ = 0;
    uint64_t buffer_full_count_ = 0;
};

} // namespace dpdk
} // namespace hft
