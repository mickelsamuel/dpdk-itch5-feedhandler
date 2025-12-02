#pragma once

#include <cstdint>
#include <cstddef>
#include <array>

namespace hft {

// Price type: Store as fixed-point integer to avoid floating-point overhead
// ITCH uses 4 decimal places, we use 6 for additional precision
using Price = int64_t;
constexpr int64_t PRICE_SCALE = 1'000'000;

// Quantity type
using Quantity = uint32_t;

// Order reference number
using OrderRef = uint64_t;

// Stock symbol (8 characters, space-padded)
using StockSymbol = std::array<char, 8>;

// Timestamp in nanoseconds since midnight
using Timestamp = uint64_t;

// Sequence number for MoldUDP64
using SequenceNumber = uint64_t;

// Message count in MoldUDP64 packet
using MessageCount = uint16_t;

// Side of the order
enum class Side : char {
    Buy = 'B',
    Sell = 'S'
};

// Order types for normalized messages
enum class MessageType : uint8_t {
    Unknown = 0,
    AddOrder = 1,
    AddOrderMPID = 2,
    OrderExecuted = 3,
    OrderExecutedWithPrice = 4,
    OrderCancel = 5,
    OrderDelete = 6,
    OrderReplace = 7,
    Trade = 8,
    CrossTrade = 9,
    BrokenTrade = 10,
    SystemEvent = 11,
    StockDirectory = 12,
    StockTradingAction = 13,
    RegSHO = 14,
    MarketParticipantPosition = 15,
    MWCB = 16,
    IPOQuotingPeriod = 17,
    LULD = 18,
    OperationalHalt = 19
};

// Normalized order message for downstream consumers
struct NormalizedMessage {
    MessageType type;
    Timestamp timestamp;
    OrderRef order_ref;
    StockSymbol stock;
    Side side;
    Price price;
    Quantity quantity;
    Quantity executed_quantity;
    OrderRef new_order_ref;  // For replace messages

    // Default constructor
    NormalizedMessage()
        : type(MessageType::Unknown)
        , timestamp(0)
        , order_ref(0)
        , stock{}
        , side(Side::Buy)
        , price(0)
        , quantity(0)
        , executed_quantity(0)
        , new_order_ref(0) {}
};

// Cache line size for preventing false sharing
#ifdef __cpp_lib_hardware_interference_size
    constexpr size_t CACHE_LINE_SIZE = std::hardware_destructive_interference_size;
#else
    constexpr size_t CACHE_LINE_SIZE = 64;
#endif

} // namespace hft
