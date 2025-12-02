/**
 * Unit tests for the ITCH 5.0 parser
 *
 * Tests:
 * - Message structure sizes
 * - Parsing of all message types
 * - Endianness handling
 * - Callback invocation
 */

#include "../include/itch5/messages.hpp"
#include "../include/itch5/parser.hpp"
#include "../include/common/endian.hpp"

#include <iostream>
#include <cstring>
#include <cassert>
#include <vector>

using namespace hft;
using namespace hft::itch5;

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

// Helper to create timestamp bytes
void set_timestamp(uint8_t* ts, uint64_t value) {
    ts[0] = (value >> 40) & 0xFF;
    ts[1] = (value >> 32) & 0xFF;
    ts[2] = (value >> 24) & 0xFF;
    ts[3] = (value >> 16) & 0xFF;
    ts[4] = (value >> 8) & 0xFF;
    ts[5] = value & 0xFF;
}

// Test message structure sizes match ITCH 5.0 spec
bool test_message_sizes() {
    TEST_ASSERT(sizeof(SystemEvent) == 12, "SystemEvent should be 12 bytes");
    TEST_ASSERT(sizeof(StockDirectory) == 39, "StockDirectory should be 39 bytes");
    TEST_ASSERT(sizeof(StockTradingAction) == 25, "StockTradingAction should be 25 bytes");
    TEST_ASSERT(sizeof(RegSHORestriction) == 20, "RegSHORestriction should be 20 bytes");
    TEST_ASSERT(sizeof(MarketParticipantPosition) == 26, "MarketParticipantPosition should be 26 bytes");
    TEST_ASSERT(sizeof(MWCBDecline) == 35, "MWCBDecline should be 35 bytes");
    TEST_ASSERT(sizeof(MWCBStatus) == 12, "MWCBStatus should be 12 bytes");
    TEST_ASSERT(sizeof(IPOQuotingPeriod) == 28, "IPOQuotingPeriod should be 28 bytes");
    TEST_ASSERT(sizeof(LULDAuctionCollar) == 35, "LULDAuctionCollar should be 35 bytes");
    TEST_ASSERT(sizeof(OperationalHalt) == 21, "OperationalHalt should be 21 bytes");
    TEST_ASSERT(sizeof(AddOrder) == 36, "AddOrder should be 36 bytes");
    TEST_ASSERT(sizeof(AddOrderMPID) == 40, "AddOrderMPID should be 40 bytes");
    TEST_ASSERT(sizeof(OrderExecuted) == 31, "OrderExecuted should be 31 bytes");
    TEST_ASSERT(sizeof(OrderExecutedWithPrice) == 36, "OrderExecutedWithPrice should be 36 bytes");
    TEST_ASSERT(sizeof(OrderCancel) == 23, "OrderCancel should be 23 bytes");
    TEST_ASSERT(sizeof(OrderDelete) == 19, "OrderDelete should be 19 bytes");
    TEST_ASSERT(sizeof(OrderReplace) == 35, "OrderReplace should be 35 bytes");
    TEST_ASSERT(sizeof(Trade) == 44, "Trade should be 44 bytes");
    TEST_ASSERT(sizeof(CrossTrade) == 40, "CrossTrade should be 40 bytes");
    TEST_ASSERT(sizeof(BrokenTrade) == 19, "BrokenTrade should be 19 bytes");
    TEST_ASSERT(sizeof(NOII) == 50, "NOII should be 50 bytes");
    TEST_ASSERT(sizeof(RPII) == 20, "RPII should be 20 bytes");

    TEST_PASS("test_message_sizes");
    return true;
}

// Test get_message_size function
bool test_get_message_size() {
    TEST_ASSERT(get_message_size('S') == sizeof(SystemEvent), "SystemEvent size lookup");
    TEST_ASSERT(get_message_size('R') == sizeof(StockDirectory), "StockDirectory size lookup");
    TEST_ASSERT(get_message_size('A') == sizeof(AddOrder), "AddOrder size lookup");
    TEST_ASSERT(get_message_size('F') == sizeof(AddOrderMPID), "AddOrderMPID size lookup");
    TEST_ASSERT(get_message_size('E') == sizeof(OrderExecuted), "OrderExecuted size lookup");
    TEST_ASSERT(get_message_size('D') == sizeof(OrderDelete), "OrderDelete size lookup");
    TEST_ASSERT(get_message_size('X') == sizeof(OrderCancel), "OrderCancel size lookup");
    TEST_ASSERT(get_message_size('U') == sizeof(OrderReplace), "OrderReplace size lookup");
    TEST_ASSERT(get_message_size('P') == sizeof(Trade), "Trade size lookup");
    TEST_ASSERT(get_message_size('Z') == 0, "Unknown message type should return 0");

    TEST_PASS("test_get_message_size");
    return true;
}

// Test endianness utilities
bool test_endian_utils() {
    // Test 16-bit swap
    uint16_t val16 = 0x1234;
    TEST_ASSERT(endian::swap16(val16) == 0x3412, "16-bit swap");

    // Test 32-bit swap
    uint32_t val32 = 0x12345678;
    TEST_ASSERT(endian::swap32(val32) == 0x78563412, "32-bit swap");

    // Test 64-bit swap
    uint64_t val64 = 0x123456789ABCDEF0ULL;
    TEST_ASSERT(endian::swap64(val64) == 0xF0DEBC9A78563412ULL, "64-bit swap");

    // Test 48-bit timestamp read
    uint8_t ts_bytes[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    uint64_t ts = endian::read_be48(ts_bytes);
    TEST_ASSERT(ts == 0x010203040506ULL, "48-bit timestamp read");

    TEST_PASS("test_endian_utils");
    return true;
}

// Test AddOrder parsing
bool test_parse_add_order() {
    Parser parser;

    bool callback_called = false;
    OrderRef received_order_ref = 0;
    Price received_price = 0;
    Quantity received_qty = 0;

    parser.set_add_order_callback(
        [&](const AddOrder* msg, Timestamp ts, Price price, Quantity qty) {
            callback_called = true;
            received_order_ref = endian::ntoh64(msg->order_reference_number);
            received_price = price;
            received_qty = qty;
        }
    );

    // Create a test AddOrder message
    AddOrder msg;
    std::memset(&msg, 0, sizeof(msg));
    msg.message_type = 'A';
    msg.stock_locate = endian::hton16(1);
    msg.tracking_number = endian::hton16(2);
    set_timestamp(msg.timestamp, 34200000000000ULL);  // 9:30 AM in nanoseconds
    msg.order_reference_number = endian::hton64(123456789ULL);
    msg.buy_sell_indicator = 'B';
    msg.shares = endian::hton32(100);
    std::memcpy(msg.stock, "AAPL    ", 8);
    msg.price = endian::hton32(1500000);  // $150.0000

    // Parse the message
    size_t consumed = parser.parse_message(reinterpret_cast<uint8_t*>(&msg), sizeof(msg));

    TEST_ASSERT(consumed == sizeof(AddOrder), "Should consume full message");
    TEST_ASSERT(callback_called, "Callback should be invoked");
    TEST_ASSERT(received_order_ref == 123456789ULL, "Order reference should match");
    TEST_ASSERT(received_price == 150000000, "Price should be $150.0000 * 100 (6 decimals)");
    TEST_ASSERT(received_qty == 100, "Quantity should match");

    auto stats = parser.get_stats();
    TEST_ASSERT(stats.total_messages == 1, "Total message count should be 1");
    TEST_ASSERT(stats.add_orders == 1, "Add order count should be 1");

    TEST_PASS("test_parse_add_order");
    return true;
}

// Test OrderExecuted parsing
bool test_parse_order_executed() {
    Parser parser;

    bool callback_called = false;
    OrderRef received_order_ref = 0;

    parser.set_order_executed_callback(
        [&](const OrderExecuted* msg, Timestamp ts) {
            callback_called = true;
            received_order_ref = endian::ntoh64(msg->order_reference_number);
        }
    );

    // Create a test OrderExecuted message
    OrderExecuted msg;
    std::memset(&msg, 0, sizeof(msg));
    msg.message_type = 'E';
    msg.stock_locate = endian::hton16(1);
    msg.tracking_number = endian::hton16(3);
    set_timestamp(msg.timestamp, 34200100000000ULL);
    msg.order_reference_number = endian::hton64(123456789ULL);
    msg.executed_shares = endian::hton32(50);
    msg.match_number = endian::hton64(999888777ULL);

    size_t consumed = parser.parse_message(reinterpret_cast<uint8_t*>(&msg), sizeof(msg));

    TEST_ASSERT(consumed == sizeof(OrderExecuted), "Should consume full message");
    TEST_ASSERT(callback_called, "Callback should be invoked");
    TEST_ASSERT(received_order_ref == 123456789ULL, "Order reference should match");

    TEST_PASS("test_parse_order_executed");
    return true;
}

// Test OrderDelete parsing
bool test_parse_order_delete() {
    Parser parser;

    bool callback_called = false;
    OrderRef received_order_ref = 0;

    parser.set_order_delete_callback(
        [&](const OrderDelete* msg, Timestamp ts) {
            callback_called = true;
            received_order_ref = endian::ntoh64(msg->order_reference_number);
        }
    );

    // Create a test OrderDelete message
    OrderDelete msg;
    std::memset(&msg, 0, sizeof(msg));
    msg.message_type = 'D';
    msg.stock_locate = endian::hton16(1);
    msg.tracking_number = endian::hton16(4);
    set_timestamp(msg.timestamp, 34200200000000ULL);
    msg.order_reference_number = endian::hton64(123456789ULL);

    size_t consumed = parser.parse_message(reinterpret_cast<uint8_t*>(&msg), sizeof(msg));

    TEST_ASSERT(consumed == sizeof(OrderDelete), "Should consume full message");
    TEST_ASSERT(callback_called, "Callback should be invoked");
    TEST_ASSERT(received_order_ref == 123456789ULL, "Order reference should match");

    TEST_PASS("test_parse_order_delete");
    return true;
}

// Test parsing multiple messages
bool test_parse_multiple_messages() {
    Parser parser;

    std::vector<OrderRef> received_orders;

    parser.set_add_order_callback(
        [&](const AddOrder* msg, Timestamp ts, Price price, Quantity qty) {
            received_orders.push_back(endian::ntoh64(msg->order_reference_number));
        }
    );

    // Create multiple messages
    std::vector<uint8_t> buffer;

    for (uint64_t i = 0; i < 10; ++i) {
        AddOrder msg;
        std::memset(&msg, 0, sizeof(msg));
        msg.message_type = 'A';
        msg.order_reference_number = endian::hton64(i);
        msg.buy_sell_indicator = 'B';
        msg.shares = endian::hton32(100);
        std::memcpy(msg.stock, "TEST    ", 8);
        msg.price = endian::hton32(1000000);

        const uint8_t* data = reinterpret_cast<const uint8_t*>(&msg);
        buffer.insert(buffer.end(), data, data + sizeof(msg));
    }

    // Parse all messages
    size_t offset = 0;
    while (offset < buffer.size()) {
        size_t consumed = parser.parse_message(buffer.data() + offset, buffer.size() - offset);
        if (consumed == 0) break;
        offset += consumed;
    }

    TEST_ASSERT(received_orders.size() == 10, "Should receive 10 orders");
    for (uint64_t i = 0; i < 10; ++i) {
        TEST_ASSERT(received_orders[i] == i, "Order reference should match sequence");
    }

    auto stats = parser.get_stats();
    TEST_ASSERT(stats.total_messages == 10, "Total message count should be 10");
    TEST_ASSERT(stats.add_orders == 10, "Add order count should be 10");

    TEST_PASS("test_parse_multiple_messages");
    return true;
}

// Test normalize_add_order
bool test_normalize_add_order() {
    AddOrder msg;
    std::memset(&msg, 0, sizeof(msg));
    msg.message_type = 'A';
    msg.stock_locate = endian::hton16(1);
    set_timestamp(msg.timestamp, 34200000000000ULL);
    msg.order_reference_number = endian::hton64(12345ULL);
    msg.buy_sell_indicator = 'S';
    msg.shares = endian::hton32(500);
    std::memcpy(msg.stock, "MSFT    ", 8);
    msg.price = endian::hton32(2500000);  // $250.0000

    NormalizedMessage norm = Parser::normalize_add_order(&msg);

    TEST_ASSERT(norm.type == MessageType::AddOrder, "Type should be AddOrder");
    TEST_ASSERT(norm.timestamp == 34200000000000ULL, "Timestamp should match");
    TEST_ASSERT(norm.order_ref == 12345ULL, "Order ref should match");
    TEST_ASSERT(norm.side == Side::Sell, "Side should be Sell");
    TEST_ASSERT(norm.price == 250000000, "Price should be $250 * 1000000");
    TEST_ASSERT(norm.quantity == 500, "Quantity should match");
    TEST_ASSERT(std::memcmp(norm.stock.data(), "MSFT    ", 8) == 0, "Stock symbol should match");

    TEST_PASS("test_normalize_add_order");
    return true;
}

// Test incomplete message handling
bool test_incomplete_message() {
    Parser parser;

    // Create partial AddOrder message
    AddOrder msg;
    std::memset(&msg, 0, sizeof(msg));
    msg.message_type = 'A';

    // Only provide first 10 bytes (less than full message)
    size_t consumed = parser.parse_message(reinterpret_cast<uint8_t*>(&msg), 10);

    TEST_ASSERT(consumed == 0, "Should return 0 for incomplete message");

    auto stats = parser.get_stats();
    TEST_ASSERT(stats.total_messages == 0, "Should not count incomplete messages");

    TEST_PASS("test_incomplete_message");
    return true;
}

// Test unknown message type
bool test_unknown_message() {
    Parser parser;

    uint8_t unknown_msg[32];
    unknown_msg[0] = 'Z';  // Unknown type

    size_t consumed = parser.parse_message(unknown_msg, sizeof(unknown_msg));

    TEST_ASSERT(consumed == 0, "Should return 0 for unknown message type");

    TEST_PASS("test_unknown_message");
    return true;
}

int main() {
    std::cout << "=== ITCH 5.0 Parser Tests ===" << std::endl;
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

    run_test(test_message_sizes, "test_message_sizes");
    run_test(test_get_message_size, "test_get_message_size");
    run_test(test_endian_utils, "test_endian_utils");
    run_test(test_parse_add_order, "test_parse_add_order");
    run_test(test_parse_order_executed, "test_parse_order_executed");
    run_test(test_parse_order_delete, "test_parse_order_delete");
    run_test(test_parse_multiple_messages, "test_parse_multiple_messages");
    run_test(test_normalize_add_order, "test_normalize_add_order");
    run_test(test_incomplete_message, "test_incomplete_message");
    run_test(test_unknown_message, "test_unknown_message");

    std::cout << std::endl;
    std::cout << "=== Results ===" << std::endl;
    std::cout << "Passed: " << passed << std::endl;
    std::cout << "Failed: " << failed << std::endl;

    return failed == 0 ? 0 : 1;
}
