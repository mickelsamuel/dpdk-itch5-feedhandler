#pragma once

#include "../common/types.hpp"
#include "../common/endian.hpp"

#include <cstdint>
#include <cstring>
#include <array>

namespace hft {
namespace moldudp64 {

/**
 * MoldUDP64 Protocol Header
 *
 * MoldUDP64 is NASDAQ's session layer protocol that wraps ITCH messages.
 * It provides:
 * - Session identification (10-byte session name)
 * - Sequence numbering for gap detection
 * - Message count per packet
 *
 * Packet structure:
 * [MoldUDP64 Header (20 bytes)] [Message 1] [Message 2] ... [Message N]
 *
 * Each message is prefixed with a 2-byte length field.
 */

#pragma pack(push, 1)

struct Header {
    char session[10];           // Session identifier (ASCII)
    uint64_t sequence_number;   // Sequence number of first message in packet (big-endian)
    uint16_t message_count;     // Number of messages in this packet (big-endian)
};
static_assert(sizeof(Header) == 20, "MoldUDP64 header must be 20 bytes");

// Each message in MoldUDP64 is prefixed with its length
struct MessageBlock {
    uint16_t length;            // Length of following message (big-endian, excludes this field)
    // uint8_t data[length];    // ITCH message data follows
};
static_assert(sizeof(MessageBlock) == 2, "MessageBlock prefix must be 2 bytes");

#pragma pack(pop)

// Special sequence numbers
constexpr uint64_t HEARTBEAT_SEQUENCE = 0;  // Sequence 0 with count 0 = heartbeat
constexpr uint64_t END_OF_SESSION = 0xFFFFFFFFFFFFFFFF;

// Helper class for parsing MoldUDP64 headers
class HeaderParser {
public:
    // Parse header from raw bytes
    static bool parse(const uint8_t* data, size_t len, Header& header) {
        if (len < sizeof(Header)) {
            return false;
        }

        // Copy session (no byte swap needed for chars)
        std::memcpy(header.session, data, 10);

        // Parse sequence number (big-endian)
        header.sequence_number = endian::read_be64(data + 10);

        // Parse message count (big-endian)
        header.message_count = endian::read_be16(data + 18);

        return true;
    }

    // Get session name as string (trimmed)
    static std::array<char, 11> get_session_string(const Header& header) {
        std::array<char, 11> result{};
        std::memcpy(result.data(), header.session, 10);
        result[10] = '\0';
        return result;
    }

    // Check if this is a heartbeat packet
    static bool is_heartbeat(const Header& header) {
        return header.sequence_number == HEARTBEAT_SEQUENCE &&
               header.message_count == 0;
    }

    // Check if this is end of session
    static bool is_end_of_session(const Header& header) {
        return header.sequence_number == END_OF_SESSION;
    }

    // Get offset to first message (after header)
    static constexpr size_t get_messages_offset() {
        return sizeof(Header);
    }

    // Read message length at given position
    static uint16_t read_message_length(const uint8_t* data) {
        return endian::read_be16(data);
    }
};

} // namespace moldudp64
} // namespace hft
