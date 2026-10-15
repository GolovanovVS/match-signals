#include <match_signals/market_data.hpp>

#include "checked.hpp"

#include <nlohmann/json.hpp>

namespace match_signals {
namespace {

Units amount(const nlohmann::json& object, const char* field) {
    return decimal(object.at(field).get<std::string>());
}

std::optional<Level> best_level(const nlohmann::json& levels, Units tick, bool bid) {
    detail::require(levels.is_array(), "order book levels must be an array");
    std::optional<Level> best;
    for (const auto& item : levels) {
        Level level{amount(item, "price"), amount(item, "size")};
        detail::require(level.price > 0 && level.price < scale && level.price % tick == 0 && level.quantity > 0,
                        "invalid order book level");
        if (!best || (bid ? level.price > best->price : level.price < best->price)) best = level;
    }
    return best;
}

}  // namespace

Quote parse_order_book(std::string_view text, const std::string& market_id,
                       const std::string& asset_id, Time received_at) {
    detail::require(!text.empty() && text.size() <= 8 * 1024 * 1024, "order book size limit exceeded");
    try {
        const auto json = nlohmann::json::parse(text, [](int depth, nlohmann::json::parse_event_t, nlohmann::json&) {
            detail::require(depth <= 32, "order book JSON nesting limit exceeded");
            return true;
        });
        detail::require(json.at("market").get<std::string>() == market_id
                            && json.at("asset_id").get<std::string>() == asset_id,
                        "order book identifiers do not match the subscription");
        Quote quote;
        quote.asset_id = asset_id;
        quote.received_at = received_at;
        quote.tick_size = amount(json, "tick_size");
        quote.min_quantity = amount(json, "min_order_size");
        detail::require(quote.tick_size > 0 && quote.tick_size < scale && quote.min_quantity > 0,
                        "invalid order book constraints");
        quote.bid = best_level(json.at("bids"), quote.tick_size, true);
        quote.ask = best_level(json.at("asks"), quote.tick_size, false);
        detail::require(!quote.bid || !quote.ask || quote.bid->price <= quote.ask->price, "crossed order book");
        return quote;
    } catch (const nlohmann::json::exception&) {
        throw std::invalid_argument("invalid order book JSON or field types");
    }
}

}  // namespace match_signals
