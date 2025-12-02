#pragma once

#include <cstdint>
#include <array>

namespace hft {
namespace itch5 {

// ITCH 5.0 Message Type Identifiers
namespace msg_type {
    constexpr char SystemEvent = 'S';
    constexpr char StockDirectory = 'R';
    constexpr char StockTradingAction = 'H';
    constexpr char RegSHORestriction = 'Y';
    constexpr char MarketParticipantPosition = 'L';
    constexpr char MWCBDecline = 'V';
    constexpr char MWCBStatus = 'W';
    constexpr char IPOQuotingPeriod = 'K';
    constexpr char LULDAuctionCollar = 'J';
    constexpr char OperationalHalt = 'h';
    constexpr char AddOrder = 'A';
    constexpr char AddOrderMPID = 'F';
    constexpr char OrderExecuted = 'E';
    constexpr char OrderExecutedWithPrice = 'C';
    constexpr char OrderCancel = 'X';
    constexpr char OrderDelete = 'D';
    constexpr char OrderReplace = 'U';
    constexpr char Trade = 'P';
    constexpr char CrossTrade = 'Q';
    constexpr char BrokenTrade = 'B';
    constexpr char NOII = 'I';
    constexpr char RPII = 'N';
}

// All ITCH messages are packed (no padding between fields)
#pragma pack(push, 1)

// Base message header (common to all ITCH messages)
// Note: Length field is NOT part of ITCH message, it's from MoldUDP64
// Size: 1 + 2 + 2 + 6 = 11 bytes
struct MessageHeader {
    char message_type;          // 1 byte - identifies message type
    uint16_t stock_locate;      // 2 bytes - locate code identifying security
    uint16_t tracking_number;   // 2 bytes - NASDAQ internal tracking
    uint8_t timestamp[6];       // 6 bytes - nanoseconds since midnight
};
static_assert(sizeof(MessageHeader) == 11, "MessageHeader must be 11 bytes");

// System Event Message (S) - 12 bytes total
struct SystemEvent {
    char message_type;          // 'S'
    uint16_t stock_locate;
    uint16_t tracking_number;
    uint8_t timestamp[6];
    char event_code;            // 'O'=Start, 'S'=Start Hours, 'Q'=Start Market, etc.
};
static_assert(sizeof(SystemEvent) == 12, "SystemEvent must be 12 bytes");

// Stock Directory Message (R) - 39 bytes total
struct StockDirectory {
    char message_type;          // 'R'
    uint16_t stock_locate;
    uint16_t tracking_number;
    uint8_t timestamp[6];
    char stock[8];              // Stock symbol (right-padded with spaces)
    char market_category;       // 'Q'=NASDAQ Global Select, 'G'=Global, etc.
    char financial_status;      // 'D'=Deficient, 'E'=Delinquent, etc.
    uint32_t round_lot_size;
    char round_lots_only;       // 'Y' or 'N'
    char issue_classification;
    char issue_sub_type[2];
    char authenticity;          // 'P'=Live/Production, 'T'=Test
    char short_sale_threshold;
    char ipo_flag;
    char luld_reference_price_tier;
    char etp_flag;
    uint32_t etp_leverage_factor;
    char inverse_indicator;
};
static_assert(sizeof(StockDirectory) == 39, "StockDirectory must be 39 bytes");

// Stock Trading Action Message (H) - 25 bytes total
struct StockTradingAction {
    char message_type;          // 'H'
    uint16_t stock_locate;
    uint16_t tracking_number;
    uint8_t timestamp[6];
    char stock[8];
    char trading_state;         // 'H'=Halted, 'P'=Paused, 'Q'=Quotation, 'T'=Trading
    char reserved;
    char reason[4];
};
static_assert(sizeof(StockTradingAction) == 25, "StockTradingAction must be 25 bytes");

// Reg SHO Short Sale Price Test Restriction (Y) - 20 bytes total
struct RegSHORestriction {
    char message_type;          // 'Y'
    uint16_t stock_locate;
    uint16_t tracking_number;
    uint8_t timestamp[6];
    char stock[8];
    char reg_sho_action;        // '0'=No restriction, '1'=Activated, '2'=Continued
};
static_assert(sizeof(RegSHORestriction) == 20, "RegSHORestriction must be 20 bytes");

// Market Participant Position Message (L) - 26 bytes total
struct MarketParticipantPosition {
    char message_type;          // 'L'
    uint16_t stock_locate;
    uint16_t tracking_number;
    uint8_t timestamp[6];
    char mpid[4];               // Market Participant ID
    char stock[8];
    char primary_market_maker;  // 'Y' or 'N'
    char market_maker_mode;     // 'N'=Normal, 'P'=Passive, 'S'=Syndicate, etc.
    char market_participant_state; // 'A'=Active, 'E'=Excused, etc.
};
static_assert(sizeof(MarketParticipantPosition) == 26, "MarketParticipantPosition must be 26 bytes");

// MWCB Decline Level Message (V) - 35 bytes total
struct MWCBDecline {
    char message_type;          // 'V'
    uint16_t stock_locate;
    uint16_t tracking_number;
    uint8_t timestamp[6];
    uint64_t level_1;           // Price (8 decimal places)
    uint64_t level_2;
    uint64_t level_3;
};
static_assert(sizeof(MWCBDecline) == 35, "MWCBDecline must be 35 bytes");

// MWCB Status Message (W) - 12 bytes total
struct MWCBStatus {
    char message_type;          // 'W'
    uint16_t stock_locate;
    uint16_t tracking_number;
    uint8_t timestamp[6];
    char breached_level;        // '1', '2', or '3'
};
static_assert(sizeof(MWCBStatus) == 12, "MWCBStatus must be 12 bytes");

// IPO Quoting Period Update (K) - 28 bytes total
struct IPOQuotingPeriod {
    char message_type;          // 'K'
    uint16_t stock_locate;
    uint16_t tracking_number;
    uint8_t timestamp[6];
    char stock[8];
    uint32_t ipo_quotation_release_time;
    char ipo_quotation_release_qualifier; // 'A'=Anticipated, 'C'=Cancelled
    uint32_t ipo_price;         // Price (4 decimal places)
};
static_assert(sizeof(IPOQuotingPeriod) == 28, "IPOQuotingPeriod must be 28 bytes");

// LULD Auction Collar Message (J) - 35 bytes total
struct LULDAuctionCollar {
    char message_type;          // 'J'
    uint16_t stock_locate;
    uint16_t tracking_number;
    uint8_t timestamp[6];
    char stock[8];
    uint32_t auction_collar_reference_price;
    uint32_t upper_auction_collar_price;
    uint32_t lower_auction_collar_price;
    uint32_t auction_collar_extension;
};
static_assert(sizeof(LULDAuctionCollar) == 35, "LULDAuctionCollar must be 35 bytes");

// Operational Halt Message (h) - 21 bytes total
struct OperationalHalt {
    char message_type;          // 'h'
    uint16_t stock_locate;
    uint16_t tracking_number;
    uint8_t timestamp[6];
    char stock[8];
    char market_code;           // 'Q'=NASDAQ, 'B'=BX, 'X'=PSX
    char operational_halt_action; // 'H'=Halted, 'T'=Resumed
};
static_assert(sizeof(OperationalHalt) == 21, "OperationalHalt must be 21 bytes");

// ==================== CRITICAL ORDER MESSAGES ====================

// Add Order (No MPID Attribution) Message (A) - 36 bytes total
// This is a critical message for order book construction
struct AddOrder {
    char message_type;          // 'A'
    uint16_t stock_locate;
    uint16_t tracking_number;
    uint8_t timestamp[6];
    uint64_t order_reference_number;
    char buy_sell_indicator;    // 'B'=Buy, 'S'=Sell
    uint32_t shares;
    char stock[8];
    uint32_t price;             // Price (4 decimal places)
};
static_assert(sizeof(AddOrder) == 36, "AddOrder must be 36 bytes");

// Add Order (MPID Attribution) Message (F) - 40 bytes total
struct AddOrderMPID {
    char message_type;          // 'F'
    uint16_t stock_locate;
    uint16_t tracking_number;
    uint8_t timestamp[6];
    uint64_t order_reference_number;
    char buy_sell_indicator;    // 'B'=Buy, 'S'=Sell
    uint32_t shares;
    char stock[8];
    uint32_t price;             // Price (4 decimal places)
    char attribution[4];        // Market Participant ID
};
static_assert(sizeof(AddOrderMPID) == 40, "AddOrderMPID must be 40 bytes");

// Order Executed Message (E) - 31 bytes total
struct OrderExecuted {
    char message_type;          // 'E'
    uint16_t stock_locate;
    uint16_t tracking_number;
    uint8_t timestamp[6];
    uint64_t order_reference_number;
    uint32_t executed_shares;
    uint64_t match_number;
};
static_assert(sizeof(OrderExecuted) == 31, "OrderExecuted must be 31 bytes");

// Order Executed With Price Message (C) - 36 bytes total
struct OrderExecutedWithPrice {
    char message_type;          // 'C'
    uint16_t stock_locate;
    uint16_t tracking_number;
    uint8_t timestamp[6];
    uint64_t order_reference_number;
    uint32_t executed_shares;
    uint64_t match_number;
    char printable;             // 'Y' or 'N'
    uint32_t execution_price;   // Price (4 decimal places)
};
static_assert(sizeof(OrderExecutedWithPrice) == 36, "OrderExecutedWithPrice must be 36 bytes");

// Order Cancel Message (X) - 23 bytes total
struct OrderCancel {
    char message_type;          // 'X'
    uint16_t stock_locate;
    uint16_t tracking_number;
    uint8_t timestamp[6];
    uint64_t order_reference_number;
    uint32_t cancelled_shares;
};
static_assert(sizeof(OrderCancel) == 23, "OrderCancel must be 23 bytes");

// Order Delete Message (D) - 19 bytes total
struct OrderDelete {
    char message_type;          // 'D'
    uint16_t stock_locate;
    uint16_t tracking_number;
    uint8_t timestamp[6];
    uint64_t order_reference_number;
};
static_assert(sizeof(OrderDelete) == 19, "OrderDelete must be 19 bytes");

// Order Replace Message (U) - 35 bytes total
struct OrderReplace {
    char message_type;          // 'U'
    uint16_t stock_locate;
    uint16_t tracking_number;
    uint8_t timestamp[6];
    uint64_t original_order_reference_number;
    uint64_t new_order_reference_number;
    uint32_t shares;
    uint32_t price;             // Price (4 decimal places)
};
static_assert(sizeof(OrderReplace) == 35, "OrderReplace must be 35 bytes");

// Trade Message (Non-Cross) (P) - 44 bytes total
struct Trade {
    char message_type;          // 'P'
    uint16_t stock_locate;
    uint16_t tracking_number;
    uint8_t timestamp[6];
    uint64_t order_reference_number;
    char buy_sell_indicator;    // 'B'=Buy, 'S'=Sell
    uint32_t shares;
    char stock[8];
    uint32_t price;             // Price (4 decimal places)
    uint64_t match_number;
};
static_assert(sizeof(Trade) == 44, "Trade must be 44 bytes");

// Cross Trade Message (Q) - 40 bytes total
struct CrossTrade {
    char message_type;          // 'Q'
    uint16_t stock_locate;
    uint16_t tracking_number;
    uint8_t timestamp[6];
    uint64_t shares;
    char stock[8];
    uint32_t cross_price;       // Price (4 decimal places)
    uint64_t match_number;
    char cross_type;            // 'O'=Opening, 'C'=Closing, etc.
};
static_assert(sizeof(CrossTrade) == 40, "CrossTrade must be 40 bytes");

// Broken Trade Message (B) - 19 bytes total
struct BrokenTrade {
    char message_type;          // 'B'
    uint16_t stock_locate;
    uint16_t tracking_number;
    uint8_t timestamp[6];
    uint64_t match_number;
};
static_assert(sizeof(BrokenTrade) == 19, "BrokenTrade must be 19 bytes");

// Net Order Imbalance Indicator (NOII) Message (I) - 50 bytes total
struct NOII {
    char message_type;          // 'I'
    uint16_t stock_locate;
    uint16_t tracking_number;
    uint8_t timestamp[6];
    uint64_t paired_shares;
    uint64_t imbalance_shares;
    char imbalance_direction;   // 'B'=Buy, 'S'=Sell, 'N'=No imbalance, 'O'=Insufficient
    char stock[8];
    uint32_t far_price;
    uint32_t near_price;
    uint32_t current_reference_price;
    char cross_type;
    char price_variation_indicator;
};
static_assert(sizeof(NOII) == 50, "NOII must be 50 bytes");

// Retail Price Improvement Indicator (RPII) Message (N) - 20 bytes total
struct RPII {
    char message_type;          // 'N'
    uint16_t stock_locate;
    uint16_t tracking_number;
    uint8_t timestamp[6];
    char stock[8];
    char interest_flag;         // 'B'=Buy, 'S'=Sell, 'A'=Both, 'N'=None
};
static_assert(sizeof(RPII) == 20, "RPII must be 20 bytes");

#pragma pack(pop)

// Get message size based on message type (for validation)
inline size_t get_message_size(char msg_type) {
    switch (msg_type) {
        case msg_type::SystemEvent:              return sizeof(SystemEvent);
        case msg_type::StockDirectory:           return sizeof(StockDirectory);
        case msg_type::StockTradingAction:       return sizeof(StockTradingAction);
        case msg_type::RegSHORestriction:        return sizeof(RegSHORestriction);
        case msg_type::MarketParticipantPosition: return sizeof(MarketParticipantPosition);
        case msg_type::MWCBDecline:              return sizeof(MWCBDecline);
        case msg_type::MWCBStatus:               return sizeof(MWCBStatus);
        case msg_type::IPOQuotingPeriod:         return sizeof(IPOQuotingPeriod);
        case msg_type::LULDAuctionCollar:        return sizeof(LULDAuctionCollar);
        case msg_type::OperationalHalt:          return sizeof(OperationalHalt);
        case msg_type::AddOrder:                 return sizeof(AddOrder);
        case msg_type::AddOrderMPID:             return sizeof(AddOrderMPID);
        case msg_type::OrderExecuted:            return sizeof(OrderExecuted);
        case msg_type::OrderExecutedWithPrice:   return sizeof(OrderExecutedWithPrice);
        case msg_type::OrderCancel:              return sizeof(OrderCancel);
        case msg_type::OrderDelete:              return sizeof(OrderDelete);
        case msg_type::OrderReplace:             return sizeof(OrderReplace);
        case msg_type::Trade:                    return sizeof(Trade);
        case msg_type::CrossTrade:               return sizeof(CrossTrade);
        case msg_type::BrokenTrade:              return sizeof(BrokenTrade);
        case msg_type::NOII:                     return sizeof(NOII);
        case msg_type::RPII:                     return sizeof(RPII);
        default:                                 return 0;
    }
}

} // namespace itch5
} // namespace hft
