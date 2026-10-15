#pragma once

#include <match_signals/market.hpp>

#include <functional>
#include <memory>
#include <string_view>

namespace match_signals {

struct MarketSubscription {
    std::string market_id;
    std::string match_id;
    Target target = Target::HomeWin;
    std::array<std::string, 2> asset_ids;
};

Quote parse_order_book(std::string_view json, const std::string& market_id,
                       const std::string& asset_id, Time received_at);

class MarketDataClient {
public:
    using FetchBook = std::function<std::string(const std::string& asset_id)>;

    MarketDataClient(MarketSubscription subscription, std::chrono::milliseconds interval,
                     Now now = std::chrono::steady_clock::now, FetchBook fetch = {});
    ~MarketDataClient();
    MarketDataClient(const MarketDataClient&) = delete;
    MarketDataClient& operator=(const MarketDataClient&) = delete;

    std::optional<MarketState> poll();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace match_signals
