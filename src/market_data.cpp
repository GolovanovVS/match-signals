#include <match_signals/market_data.hpp>

#include "checked.hpp"
#include "curl.hpp"

#include <algorithm>

namespace match_signals {

struct MarketDataClient::Impl {
    MarketSubscription subscription;
    std::chrono::milliseconds interval;
    Now now;
    FetchBook fetch;
    std::unique_ptr<detail::CurlHandle> curl;
    std::optional<Time> next_poll;

    void advance_schedule(Time time) {
        if (!next_poll) next_poll = time + interval;
        else if (time >= *next_poll) {
            *next_poll += interval * ((time - *next_poll) / interval + 1);
        }
    }
};

MarketDataClient::MarketDataClient(MarketSubscription subscription, std::chrono::milliseconds interval,
                                 Now now, FetchBook fetch) : impl_(std::make_unique<Impl>()) {
    detail::require(!subscription.market_id.empty() && !subscription.match_id.empty()
                        && subscription.asset_ids[0] != subscription.asset_ids[1], "invalid market subscription");
    for (const auto& asset : subscription.asset_ids) {
        detail::require(!asset.empty() && asset.size() <= 256
                            && std::all_of(asset.begin(), asset.end(), [](char c) { return c >= '0' && c <= '9'; }),
                        "Polymarket asset ID must be a decimal string");
    }
    detail::require(subscription.target == Target::HomeWin || subscription.target == Target::Draw
                        || subscription.target == Target::AwayWin, "invalid market target");
    detail::require(interval.count() > 0 && interval <= std::chrono::hours(24) && now, "invalid poll interval or clock");
    impl_->subscription = std::move(subscription);
    impl_->interval = interval;
    impl_->now = std::move(now);
    impl_->fetch = std::move(fetch);
    if (!impl_->fetch) impl_->curl = std::make_unique<detail::CurlHandle>();
}

MarketDataClient::~MarketDataClient() = default;

std::optional<MarketState> MarketDataClient::poll() {
    const auto started = impl_->now();
    if (impl_->next_poll && started < *impl_->next_poll) return std::nullopt;
    // Retry failures on the next interval rather than hammering the API in a tight loop.
    impl_->advance_schedule(started);
    MarketState result;
    result.market_id = impl_->subscription.market_id;
    result.match_id = impl_->subscription.match_id;
    result.target = impl_->subscription.target;
    try {
        for (std::size_t i = 0; i < result.quotes.size(); ++i) {
            const auto& asset = impl_->subscription.asset_ids[i];
            const auto requested_at = impl_->now();
            const auto json = impl_->fetch ? impl_->fetch(asset)
                : detail::http_get(*impl_->curl, "https://clob.polymarket.com/book?token_id=" + asset);
            // Include network time in freshness checks instead of making a slow response look new.
            result.quotes[i] = parse_order_book(json, result.market_id, asset, requested_at);
        }
    } catch (...) {
        impl_->advance_schedule(impl_->now());
        throw;
    }
    impl_->advance_schedule(impl_->now());
    return result;
}

}  // namespace match_signals
