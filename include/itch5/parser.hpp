#pragma once

#include "messages.hpp"
#include "../common/types.hpp"
#include "../common/endian.hpp"

#include <cstring>
#include <functional>
#include <cstdio>

namespace hft {
namespace itch5 {

// Callback types for different message categories
using AddOrderCallback = std::function<void(const AddOrder*, Timestamp, Price, Quantity)>;
using AddOrderMPIDCallback = std::function<void(const AddOrderMPID*, Timestamp, Price, Quantity)>;
using OrderExecutedCallback = std::function<void(const OrderExecuted*, Timestamp)>;
using OrderExecutedWithPriceCallback = std::function<void(const OrderExecutedWithPrice*, Timestamp, Price)>;
using OrderCancelCallback = std::function<void(const OrderCancel*, Timestamp)>;
using OrderDeleteCallback = std::function<void(const OrderDelete*, Timestamp)>;
using OrderReplaceCallback = std::function<void(const OrderReplace*, Timestamp, Price, Quantity)>;
using TradeCallback = std::function<void(const Trade*, Timestamp, Price, Quantity)>;

// Zero-copy ITCH 5.0 parser
// This parser casts raw memory directly to message structs without copying
// It handles endianness conversion on the fly
class Parser {
public:
    Parser() = default;

    // Set callbacks for message types
    void set_add_order_callback(AddOrderCallback cb) { add_order_cb_ = std::move(cb); }
    void set_add_order_mpid_callback(AddOrderMPIDCallback cb) { add_order_mpid_cb_ = std::move(cb); }
    void set_order_executed_callback(OrderExecutedCallback cb) { order_executed_cb_ = std::move(cb); }
    void set_order_executed_with_price_callback(OrderExecutedWithPriceCallback cb) { order_executed_with_price_cb_ = std::move(cb); }
    void set_order_cancel_callback(OrderCancelCallback cb) { order_cancel_cb_ = std::move(cb); }
    void set_order_delete_callback(OrderDeleteCallback cb) { order_delete_cb_ = std::move(cb); }
    void set_order_replace_callback(OrderReplaceCallback cb) { order_replace_cb_ = std::move(cb); }
    void set_trade_callback(TradeCallback cb) { trade_cb_ = std::move(cb); }

    // Parse a single ITCH message from raw memory (zero-copy)
    // Returns the number of bytes consumed, or 0 on error
    // The data pointer should point to the start of the ITCH message (after MoldUDP64 length field)
    size_t parse_message(const uint8_t* data, size_t len) {
        if (len < 1) return 0;

        char msg_type = static_cast<char>(data[0]);
        size_t expected_size = get_message_size(msg_type);

        if (expected_size == 0) {
            // Unknown message type - skip it
            // In production, you'd log this
            return 0;
        }

        if (len < expected_size) {
            // Incomplete message
            return 0;
        }

        // Zero-copy: cast directly to struct pointer
        // The struct is packed, so this is safe
        switch (msg_type) {
            case msg_type::AddOrder:
                parse_add_order(reinterpret_cast<const AddOrder*>(data));
                break;

            case msg_type::AddOrderMPID:
                parse_add_order_mpid(reinterpret_cast<const AddOrderMPID*>(data));
                break;

            case msg_type::OrderExecuted:
                parse_order_executed(reinterpret_cast<const OrderExecuted*>(data));
                break;

            case msg_type::OrderExecutedWithPrice:
                parse_order_executed_with_price(reinterpret_cast<const OrderExecutedWithPrice*>(data));
                break;

            case msg_type::OrderCancel:
                parse_order_cancel(reinterpret_cast<const OrderCancel*>(data));
                break;

            case msg_type::OrderDelete:
                parse_order_delete(reinterpret_cast<const OrderDelete*>(data));
                break;

            case msg_type::OrderReplace:
                parse_order_replace(reinterpret_cast<const OrderReplace*>(data));
                break;

            case msg_type::Trade:
                parse_trade(reinterpret_cast<const Trade*>(data));
                break;

            // Non-order messages - count but don't process for now
            case msg_type::SystemEvent:
            case msg_type::StockDirectory:
            case msg_type::StockTradingAction:
            case msg_type::RegSHORestriction:
            case msg_type::MarketParticipantPosition:
            case msg_type::MWCBDecline:
            case msg_type::MWCBStatus:
            case msg_type::IPOQuotingPeriod:
            case msg_type::LULDAuctionCollar:
            case msg_type::OperationalHalt:
            case msg_type::CrossTrade:
            case msg_type::BrokenTrade:
            case msg_type::NOII:
            case msg_type::RPII:
                ++stats_.other_messages;
                break;

            default:
                ++stats_.unknown_messages;
                break;
        }

        ++stats_.total_messages;
        return expected_size;
    }

    // Convert normalized message to downstream format
    // This can be used to push to the ring buffer
    static NormalizedMessage normalize_add_order(const AddOrder* msg) {
        NormalizedMessage norm;
        norm.type = MessageType::AddOrder;
        norm.timestamp = endian::read_be48(msg->timestamp);
        norm.order_ref = endian::ntoh64(msg->order_reference_number);
        std::memcpy(norm.stock.data(), msg->stock, 8);
        norm.side = (msg->buy_sell_indicator == 'B') ? Side::Buy : Side::Sell;
        norm.price = convert_price(endian::ntoh32(msg->price));
        norm.quantity = endian::ntoh32(msg->shares);
        return norm;
    }

    // Statistics
    struct Stats {
        uint64_t total_messages = 0;
        uint64_t add_orders = 0;
        uint64_t order_executed = 0;
        uint64_t order_deleted = 0;
        uint64_t order_cancelled = 0;
        uint64_t order_replaced = 0;
        uint64_t trades = 0;
        uint64_t other_messages = 0;
        uint64_t unknown_messages = 0;
    };

    const Stats& get_stats() const { return stats_; }
    void reset_stats() { stats_ = Stats{}; }

private:
    // Convert ITCH price (4 decimal places) to our internal format (6 decimal places)
    static Price convert_price(uint32_t itch_price) {
        // ITCH uses 4 decimal places, we use 6
        // So multiply by 100 to shift 2 more decimal places
        return static_cast<Price>(itch_price) * 100;
    }

    void parse_add_order(const AddOrder* msg) {
        ++stats_.add_orders;
        if (add_order_cb_) {
            Timestamp ts = endian::read_be48(msg->timestamp);
            Price price = convert_price(endian::ntoh32(msg->price));
            Quantity qty = endian::ntoh32(msg->shares);
            add_order_cb_(msg, ts, price, qty);
        }
    }

    void parse_add_order_mpid(const AddOrderMPID* msg) {
        ++stats_.add_orders;
        if (add_order_mpid_cb_) {
            Timestamp ts = endian::read_be48(msg->timestamp);
            Price price = convert_price(endian::ntoh32(msg->price));
            Quantity qty = endian::ntoh32(msg->shares);
            add_order_mpid_cb_(msg, ts, price, qty);
        }
    }

    void parse_order_executed(const OrderExecuted* msg) {
        ++stats_.order_executed;
        if (order_executed_cb_) {
            Timestamp ts = endian::read_be48(msg->timestamp);
            order_executed_cb_(msg, ts);
        }
    }

    void parse_order_executed_with_price(const OrderExecutedWithPrice* msg) {
        ++stats_.order_executed;
        if (order_executed_with_price_cb_) {
            Timestamp ts = endian::read_be48(msg->timestamp);
            Price price = convert_price(endian::ntoh32(msg->execution_price));
            order_executed_with_price_cb_(msg, ts, price);
        }
    }

    void parse_order_cancel(const OrderCancel* msg) {
        ++stats_.order_cancelled;
        if (order_cancel_cb_) {
            Timestamp ts = endian::read_be48(msg->timestamp);
            order_cancel_cb_(msg, ts);
        }
    }

    void parse_order_delete(const OrderDelete* msg) {
        ++stats_.order_deleted;
        if (order_delete_cb_) {
            Timestamp ts = endian::read_be48(msg->timestamp);
            order_delete_cb_(msg, ts);
        }
    }

    void parse_order_replace(const OrderReplace* msg) {
        ++stats_.order_replaced;
        if (order_replace_cb_) {
            Timestamp ts = endian::read_be48(msg->timestamp);
            Price price = convert_price(endian::ntoh32(msg->price));
            Quantity qty = endian::ntoh32(msg->shares);
            order_replace_cb_(msg, ts, price, qty);
        }
    }

    void parse_trade(const Trade* msg) {
        ++stats_.trades;
        if (trade_cb_) {
            Timestamp ts = endian::read_be48(msg->timestamp);
            Price price = convert_price(endian::ntoh32(msg->price));
            Quantity qty = endian::ntoh32(msg->shares);
            trade_cb_(msg, ts, price, qty);
        }
    }

    // Callbacks
    AddOrderCallback add_order_cb_;
    AddOrderMPIDCallback add_order_mpid_cb_;
    OrderExecutedCallback order_executed_cb_;
    OrderExecutedWithPriceCallback order_executed_with_price_cb_;
    OrderCancelCallback order_cancel_cb_;
    OrderDeleteCallback order_delete_cb_;
    OrderReplaceCallback order_replace_cb_;
    TradeCallback trade_cb_;

    // Statistics
    Stats stats_;
};

} // namespace itch5
} // namespace hft
