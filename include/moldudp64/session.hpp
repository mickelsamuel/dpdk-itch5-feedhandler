#pragma once

#include "header.hpp"
#include "../common/types.hpp"
#include "../common/endian.hpp"

#include <cstdint>
#include <cstring>
#include <functional>
#include <vector>
#include <deque>
#include <optional>

namespace hft {
namespace moldudp64 {

/**
 * Gap information for retransmission requests
 */
struct Gap {
    SequenceNumber start;       // First missing sequence number
    SequenceNumber end;         // Last missing sequence number (inclusive)
    uint64_t detected_at_ns;    // When the gap was detected (for timeout)
};

/**
 * Session state
 */
enum class SessionState {
    Unknown,        // No packets received yet
    Active,         // Normal operation
    Stale,          // Gap detected, waiting for retransmission
    EndOfSession,   // End of session received
    Error           // Unrecoverable error
};

/**
 * MoldUDP64 Session Manager
 *
 * Handles:
 * - Sequence number tracking
 * - Gap detection
 * - Heartbeat processing
 * - Session state management
 *
 * Architecture note:
 * Retransmission requests should be handled on a separate thread/connection
 * to avoid stalling the critical path. This class only detects gaps and
 * marks the session as stale.
 */
class Session {
public:
    // Callback for when a gap is detected
    using GapCallback = std::function<void(const Gap&)>;

    // Callback for each message in a packet
    using MessageCallback = std::function<void(const uint8_t* data, uint16_t length, SequenceNumber seq)>;

    explicit Session(std::array<char, 10> session_id = {})
        : session_id_(session_id)
        , expected_sequence_(1)  // First sequence number is typically 1
        , state_(SessionState::Unknown)
        , packets_received_(0)
        , messages_received_(0)
        , gaps_detected_(0)
        , heartbeats_received_(0) {}

    /**
     * Process a MoldUDP64 packet
     * Returns true if packet was processed successfully
     */
    bool process_packet(const uint8_t* data, size_t len) {
        Header header;
        if (!HeaderParser::parse(data, len, header)) {
            return false;
        }

        ++packets_received_;

        // Check session ID (first packet establishes it)
        if (state_ == SessionState::Unknown) {
            std::memcpy(session_id_.data(), header.session, 10);
            state_ = SessionState::Active;
        } else {
            // Verify session ID matches
            if (std::memcmp(session_id_.data(), header.session, 10) != 0) {
                // Different session - this shouldn't happen in normal operation
                state_ = SessionState::Error;
                return false;
            }
        }

        // Handle special packet types
        if (HeaderParser::is_heartbeat(header)) {
            ++heartbeats_received_;
            return true;
        }

        if (HeaderParser::is_end_of_session(header)) {
            state_ = SessionState::EndOfSession;
            return true;
        }

        // Check for gaps
        if (header.sequence_number > expected_sequence_) {
            // Gap detected!
            Gap gap;
            gap.start = expected_sequence_;
            gap.end = header.sequence_number - 1;
            gap.detected_at_ns = 0;  // Caller should set this

            pending_gaps_.push_back(gap);
            ++gaps_detected_;
            state_ = SessionState::Stale;

            if (gap_callback_) {
                gap_callback_(gap);
            }
        } else if (header.sequence_number < expected_sequence_) {
            // Duplicate or old packet - could be retransmission
            // Check if this fills a gap
            check_gap_fill(header.sequence_number,
                          header.sequence_number + header.message_count - 1);

            // Still process the messages (might be retransmission)
        }

        // Process messages in the packet
        if (message_callback_) {
            size_t offset = HeaderParser::get_messages_offset();
            SequenceNumber current_seq = header.sequence_number;

            for (uint16_t i = 0; i < header.message_count; ++i) {
                if (offset + sizeof(MessageBlock) > len) {
                    // Truncated packet
                    break;
                }

                uint16_t msg_len = HeaderParser::read_message_length(data + offset);
                offset += sizeof(MessageBlock);

                if (offset + msg_len > len) {
                    // Message extends past packet boundary
                    break;
                }

                // Invoke callback with message data
                message_callback_(data + offset, msg_len, current_seq);
                ++messages_received_;

                offset += msg_len;
                ++current_seq;
            }
        } else {
            // Just count messages without processing
            messages_received_ += header.message_count;
        }

        // Update expected sequence number
        SequenceNumber next_expected = header.sequence_number + header.message_count;
        if (next_expected > expected_sequence_) {
            expected_sequence_ = next_expected;
        }

        // Check if all gaps are filled
        if (state_ == SessionState::Stale && pending_gaps_.empty()) {
            state_ = SessionState::Active;
        }

        return true;
    }

    /**
     * Process retransmission response
     * This should be called when gap-fill data arrives
     */
    void process_retransmission(SequenceNumber start_seq, const uint8_t* data, size_t len,
                                uint16_t message_count) {
        check_gap_fill(start_seq, start_seq + message_count - 1);

        // Process the retransmitted messages
        if (message_callback_) {
            size_t offset = 0;
            SequenceNumber current_seq = start_seq;

            for (uint16_t i = 0; i < message_count && offset < len; ++i) {
                uint16_t msg_len = HeaderParser::read_message_length(data + offset);
                offset += sizeof(MessageBlock);

                if (offset + msg_len > len) break;

                message_callback_(data + offset, msg_len, current_seq);
                offset += msg_len;
                ++current_seq;
            }
        }

        // Check if session can transition back to active
        if (pending_gaps_.empty()) {
            state_ = SessionState::Active;
        }
    }

    // Set callbacks
    void set_gap_callback(GapCallback cb) { gap_callback_ = std::move(cb); }
    void set_message_callback(MessageCallback cb) { message_callback_ = std::move(cb); }

    // Getters
    SessionState get_state() const { return state_; }
    SequenceNumber get_expected_sequence() const { return expected_sequence_; }
    const std::vector<Gap>& get_pending_gaps() const { return pending_gaps_; }
    bool has_gaps() const { return !pending_gaps_.empty(); }

    // Statistics
    struct Stats {
        uint64_t packets_received;
        uint64_t messages_received;
        uint64_t gaps_detected;
        uint64_t heartbeats_received;
    };

    Stats get_stats() const {
        return {packets_received_, messages_received_, gaps_detected_, heartbeats_received_};
    }

    // Reset session state (for reuse)
    void reset() {
        expected_sequence_ = 1;
        state_ = SessionState::Unknown;
        pending_gaps_.clear();
        packets_received_ = 0;
        messages_received_ = 0;
        gaps_detected_ = 0;
        heartbeats_received_ = 0;
    }

    // Check if session is healthy (no gaps)
    bool is_healthy() const {
        return state_ == SessionState::Active && pending_gaps_.empty();
    }

private:
    void check_gap_fill(SequenceNumber start, SequenceNumber end) {
        // Check if this range fills any pending gaps
        for (auto it = pending_gaps_.begin(); it != pending_gaps_.end(); ) {
            if (start <= it->start && end >= it->end) {
                // Gap completely filled
                it = pending_gaps_.erase(it);
            } else if (start <= it->start && end >= it->start) {
                // Gap partially filled from the start
                it->start = end + 1;
                if (it->start > it->end) {
                    it = pending_gaps_.erase(it);
                } else {
                    ++it;
                }
            } else if (start <= it->end && end >= it->end) {
                // Gap partially filled from the end
                it->end = start - 1;
                if (it->start > it->end) {
                    it = pending_gaps_.erase(it);
                } else {
                    ++it;
                }
            } else {
                ++it;
            }
        }
    }

    std::array<char, 10> session_id_;
    SequenceNumber expected_sequence_;
    SessionState state_;
    std::vector<Gap> pending_gaps_;

    // Statistics
    uint64_t packets_received_;
    uint64_t messages_received_;
    uint64_t gaps_detected_;
    uint64_t heartbeats_received_;

    // Callbacks
    GapCallback gap_callback_;
    MessageCallback message_callback_;
};

/**
 * Multi-session manager for handling multiple MoldUDP64 streams
 */
class SessionManager {
public:
    // Get or create session by session ID
    Session& get_session(const std::array<char, 10>& session_id) {
        // For simplicity, we use a single session
        // In production, you'd want to track multiple sessions
        (void)session_id;  // Suppress unused warning

        if (sessions_.empty()) {
            sessions_.emplace_back(session_id);
        }
        return sessions_.back();
    }

    // Get all sessions with gaps (for monitoring)
    std::vector<const Session*> get_stale_sessions() const {
        std::vector<const Session*> stale;
        for (const auto& session : sessions_) {
            if (session.get_state() == SessionState::Stale) {
                stale.push_back(&session);
            }
        }
        return stale;
    }

private:
    std::deque<Session> sessions_;
};

} // namespace moldudp64
} // namespace hft
