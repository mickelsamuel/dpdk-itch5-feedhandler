/**
 * Unit tests for MoldUDP64 session layer
 *
 * Tests:
 * - Header parsing
 * - Session tracking
 * - Gap detection
 * - Heartbeat handling
 */

#include "../include/moldudp64/header.hpp"
#include "../include/moldudp64/session.hpp"
#include "../include/common/endian.hpp"

#include <iostream>
#include <cstring>
#include <vector>

using namespace hft;
using namespace hft::moldudp64;

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

// Helper to create MoldUDP64 packet
std::vector<uint8_t> create_moldudp_packet(
    const char* session_id,
    uint64_t sequence_number,
    uint16_t message_count,
    const std::vector<std::vector<uint8_t>>& messages = {}
) {
    std::vector<uint8_t> packet;
    packet.resize(20);  // Header size

    // Session ID (10 bytes, space-padded)
    std::memset(packet.data(), ' ', 10);
    size_t session_len = std::min(strlen(session_id), size_t(10));
    std::memcpy(packet.data(), session_id, session_len);

    // Sequence number (big-endian)
    uint64_t seq_be = endian::hton64(sequence_number);
    std::memcpy(packet.data() + 10, &seq_be, 8);

    // Message count (big-endian)
    uint16_t count_be = endian::hton16(message_count);
    std::memcpy(packet.data() + 18, &count_be, 2);

    // Add messages with 2-byte big-endian length prefix
    for (const auto& msg : messages) {
        uint16_t len = static_cast<uint16_t>(msg.size());
        packet.push_back((len >> 8) & 0xFF);  // High byte first (big-endian)
        packet.push_back(len & 0xFF);          // Low byte second
        packet.insert(packet.end(), msg.begin(), msg.end());
    }

    return packet;
}

// Test header structure size
bool test_header_size() {
    TEST_ASSERT(sizeof(Header) == 20, "MoldUDP64 header should be 20 bytes");
    TEST_ASSERT(sizeof(MessageBlock) == 2, "MessageBlock prefix should be 2 bytes");

    TEST_PASS("test_header_size");
    return true;
}

// Test header parsing
bool test_header_parsing() {
    auto packet = create_moldudp_packet("NASDAQ", 12345, 5);

    Header header;
    bool result = HeaderParser::parse(packet.data(), packet.size(), header);

    TEST_ASSERT(result, "Header parsing should succeed");
    TEST_ASSERT(std::memcmp(header.session, "NASDAQ    ", 10) == 0, "Session ID should match");
    TEST_ASSERT(header.sequence_number == 12345, "Sequence number should match");
    TEST_ASSERT(header.message_count == 5, "Message count should match");

    TEST_PASS("test_header_parsing");
    return true;
}

// Test heartbeat detection
bool test_heartbeat_detection() {
    // Heartbeat: sequence 0, count 0
    auto heartbeat = create_moldudp_packet("NASDAQ", 0, 0);

    Header header;
    HeaderParser::parse(heartbeat.data(), heartbeat.size(), header);

    TEST_ASSERT(HeaderParser::is_heartbeat(header), "Should detect heartbeat");

    // Regular packet
    auto regular = create_moldudp_packet("NASDAQ", 1, 1);
    HeaderParser::parse(regular.data(), regular.size(), header);

    TEST_ASSERT(!HeaderParser::is_heartbeat(header), "Should not be heartbeat");

    TEST_PASS("test_heartbeat_detection");
    return true;
}

// Test end of session detection
bool test_end_of_session() {
    auto eos_packet = create_moldudp_packet("NASDAQ", 0xFFFFFFFFFFFFFFFFULL, 0);

    Header header;
    HeaderParser::parse(eos_packet.data(), eos_packet.size(), header);

    TEST_ASSERT(HeaderParser::is_end_of_session(header), "Should detect end of session");

    TEST_PASS("test_end_of_session");
    return true;
}

// Test session state machine - initial state
bool test_session_initial_state() {
    Session session;

    TEST_ASSERT(session.get_state() == SessionState::Unknown, "Initial state should be Unknown");
    TEST_ASSERT(session.get_expected_sequence() == 1, "Initial expected sequence should be 1");
    TEST_ASSERT(!session.has_gaps(), "Should have no gaps initially");

    TEST_PASS("test_session_initial_state");
    return true;
}

// Test session - normal operation
bool test_session_normal_operation() {
    Session session;

    std::vector<SequenceNumber> received_sequences;
    session.set_message_callback(
        [&](const uint8_t* data, uint16_t length, SequenceNumber seq) {
            received_sequences.push_back(seq);
        }
    );

    // Create test messages
    std::vector<uint8_t> msg1 = {'A', 0x00, 0x01};  // Fake ITCH message
    std::vector<uint8_t> msg2 = {'E', 0x00, 0x02};

    // Packet 1: seq 1-2
    auto packet1 = create_moldudp_packet("NASDAQ", 1, 2, {msg1, msg2});
    session.process_packet(packet1.data(), packet1.size());

    TEST_ASSERT(session.get_state() == SessionState::Active, "State should be Active");
    TEST_ASSERT(session.get_expected_sequence() == 3, "Expected sequence should be 3");
    TEST_ASSERT(received_sequences.size() == 2, "Should receive 2 messages");
    TEST_ASSERT(received_sequences[0] == 1, "First sequence should be 1");
    TEST_ASSERT(received_sequences[1] == 2, "Second sequence should be 2");

    // Packet 2: seq 3-4
    auto packet2 = create_moldudp_packet("NASDAQ", 3, 2, {msg1, msg2});
    session.process_packet(packet2.data(), packet2.size());

    TEST_ASSERT(session.get_expected_sequence() == 5, "Expected sequence should be 5");
    TEST_ASSERT(received_sequences.size() == 4, "Should receive 4 total messages");

    TEST_PASS("test_session_normal_operation");
    return true;
}

// Test session - gap detection
bool test_session_gap_detection() {
    Session session;

    std::vector<Gap> detected_gaps;
    session.set_gap_callback([&](const Gap& gap) {
        detected_gaps.push_back(gap);
    });

    std::vector<uint8_t> msg = {'A', 0x00, 0x01};

    // Packet 1: seq 1
    auto packet1 = create_moldudp_packet("NASDAQ", 1, 1, {msg});
    session.process_packet(packet1.data(), packet1.size());

    TEST_ASSERT(session.get_state() == SessionState::Active, "State should be Active");
    TEST_ASSERT(detected_gaps.empty(), "Should have no gaps");

    // Packet 2: seq 5 (gap of 2-4)
    auto packet2 = create_moldudp_packet("NASDAQ", 5, 1, {msg});
    session.process_packet(packet2.data(), packet2.size());

    TEST_ASSERT(session.get_state() == SessionState::Stale, "State should be Stale after gap");
    TEST_ASSERT(detected_gaps.size() == 1, "Should detect 1 gap");
    TEST_ASSERT(detected_gaps[0].start == 2, "Gap should start at 2");
    TEST_ASSERT(detected_gaps[0].end == 4, "Gap should end at 4");
    TEST_ASSERT(session.has_gaps(), "Session should report having gaps");

    auto stats = session.get_stats();
    TEST_ASSERT(stats.gaps_detected == 1, "Should have 1 gap detected in stats");

    TEST_PASS("test_session_gap_detection");
    return true;
}

// Test session - heartbeat handling
bool test_session_heartbeat_handling() {
    Session session;

    std::vector<uint8_t> msg = {'A', 0x00, 0x01};

    // First packet to establish session
    auto packet1 = create_moldudp_packet("NASDAQ", 1, 1, {msg});
    session.process_packet(packet1.data(), packet1.size());

    // Heartbeat
    auto heartbeat = create_moldudp_packet("NASDAQ", 0, 0);
    session.process_packet(heartbeat.data(), heartbeat.size());

    // Verify session state unchanged
    TEST_ASSERT(session.get_state() == SessionState::Active, "State should remain Active");
    TEST_ASSERT(session.get_expected_sequence() == 2, "Expected sequence unchanged");

    auto stats = session.get_stats();
    TEST_ASSERT(stats.heartbeats_received == 1, "Should count heartbeat");

    TEST_PASS("test_session_heartbeat_handling");
    return true;
}

// Test session - multiple gaps
bool test_session_multiple_gaps() {
    Session session;

    std::vector<Gap> detected_gaps;
    session.set_gap_callback([&](const Gap& gap) {
        detected_gaps.push_back(gap);
    });

    std::vector<uint8_t> msg = {'A', 0x00};

    // Seq 1
    auto p1 = create_moldudp_packet("NASDAQ", 1, 1, {msg});
    session.process_packet(p1.data(), p1.size());

    // Seq 5 (gap 2-4)
    auto p2 = create_moldudp_packet("NASDAQ", 5, 1, {msg});
    session.process_packet(p2.data(), p2.size());

    // Seq 10 (gap 6-9)
    auto p3 = create_moldudp_packet("NASDAQ", 10, 1, {msg});
    session.process_packet(p3.data(), p3.size());

    TEST_ASSERT(detected_gaps.size() == 2, "Should detect 2 gaps");
    TEST_ASSERT(detected_gaps[0].start == 2 && detected_gaps[0].end == 4, "First gap 2-4");
    TEST_ASSERT(detected_gaps[1].start == 6 && detected_gaps[1].end == 9, "Second gap 6-9");

    auto stats = session.get_stats();
    TEST_ASSERT(stats.gaps_detected == 2, "Stats should show 2 gaps");

    TEST_PASS("test_session_multiple_gaps");
    return true;
}

// Test session reset
bool test_session_reset() {
    Session session;

    std::vector<uint8_t> msg = {'A', 0x00};

    // Process some packets
    auto p1 = create_moldudp_packet("NASDAQ", 1, 1, {msg});
    session.process_packet(p1.data(), p1.size());

    auto p2 = create_moldudp_packet("NASDAQ", 5, 1, {msg});  // Create gap
    session.process_packet(p2.data(), p2.size());

    TEST_ASSERT(session.get_state() == SessionState::Stale, "Should be Stale before reset");
    TEST_ASSERT(session.has_gaps(), "Should have gaps before reset");

    // Reset
    session.reset();

    TEST_ASSERT(session.get_state() == SessionState::Unknown, "State should be Unknown after reset");
    TEST_ASSERT(session.get_expected_sequence() == 1, "Expected sequence should reset to 1");
    TEST_ASSERT(!session.has_gaps(), "Should have no gaps after reset");

    auto stats = session.get_stats();
    TEST_ASSERT(stats.packets_received == 0, "Stats should be reset");

    TEST_PASS("test_session_reset");
    return true;
}

// Test is_healthy
bool test_session_is_healthy() {
    Session session;

    std::vector<uint8_t> msg = {'A', 0x00};

    // Initially not healthy (Unknown state)
    TEST_ASSERT(!session.is_healthy(), "Should not be healthy initially");

    // After first packet - healthy
    auto p1 = create_moldudp_packet("NASDAQ", 1, 1, {msg});
    session.process_packet(p1.data(), p1.size());
    TEST_ASSERT(session.is_healthy(), "Should be healthy after first packet");

    // After gap - not healthy
    auto p2 = create_moldudp_packet("NASDAQ", 5, 1, {msg});
    session.process_packet(p2.data(), p2.size());
    TEST_ASSERT(!session.is_healthy(), "Should not be healthy after gap");

    TEST_PASS("test_session_is_healthy");
    return true;
}

// Test truncated packet handling
bool test_truncated_packet() {
    Session session;

    // Create packet but truncate it
    auto packet = create_moldudp_packet("NASDAQ", 1, 1);
    packet.resize(10);  // Truncate to less than header size

    bool result = session.process_packet(packet.data(), packet.size());

    TEST_ASSERT(!result, "Should fail on truncated packet");
    TEST_ASSERT(session.get_state() == SessionState::Unknown, "State should remain Unknown");

    TEST_PASS("test_truncated_packet");
    return true;
}

int main() {
    std::cout << "=== MoldUDP64 Session Layer Tests ===" << std::endl;
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

    run_test(test_header_size, "test_header_size");
    run_test(test_header_parsing, "test_header_parsing");
    run_test(test_heartbeat_detection, "test_heartbeat_detection");
    run_test(test_end_of_session, "test_end_of_session");
    run_test(test_session_initial_state, "test_session_initial_state");
    run_test(test_session_normal_operation, "test_session_normal_operation");
    run_test(test_session_gap_detection, "test_session_gap_detection");
    run_test(test_session_heartbeat_handling, "test_session_heartbeat_handling");
    run_test(test_session_multiple_gaps, "test_session_multiple_gaps");
    run_test(test_session_reset, "test_session_reset");
    run_test(test_session_is_healthy, "test_session_is_healthy");
    run_test(test_truncated_packet, "test_truncated_packet");

    std::cout << std::endl;
    std::cout << "=== Results ===" << std::endl;
    std::cout << "Passed: " << passed << std::endl;
    std::cout << "Failed: " << failed << std::endl;

    return failed == 0 ? 0 : 1;
}
