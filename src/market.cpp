#include <match_signals/engine.hpp>

#include <limits>
#include <utility>

#include "checked.hpp"

namespace match_signals {
using detail::require;

namespace {

void validate_quote(const Quote& quote) {
    require(!quote.asset_id.empty() && quote.tick_size > 0 && quote.tick_size < scale
                && quote.min_quantity > 0,
            "invalid quote identifiers or constraints");
    for (const auto& level : {quote.bid, quote.ask}) {
        if (level) require(level->price > 0 && level->price < scale && level->quantity > 0
                               && level->price % quote.tick_size == 0,
                           "invalid book level");
    }
    require(!quote.bid || !quote.ask || quote.bid->price <= quote.ask->price, "crossed order book");
}

}  // namespace

void TradingEngine::update_market(MarketState input) {
    require_mutable();
    require(!input.market_id.empty() && !input.match_id.empty(), "market identifiers are required");
    require(input.target == Target::HomeWin || input.target == Target::Draw || input.target == Target::AwayWin,
            "invalid market target");
    validate_quote(input.quotes[0]);
    validate_quote(input.quotes[1]);
    require(input.quotes[0].asset_id != input.quotes[1].asset_id, "outcomes must have different asset IDs");
    for (const auto& quote : input.quotes) require(quote.received_at <= now_(), "future quote");
    auto existing = markets_.find(input.market_id);
    if (existing != markets_.end()) {
        const auto& old = existing->second;
        require(input.match_id == old.match_id && input.target == old.target
                    && input.quotes[0].asset_id == old.quotes[0].asset_id
                    && input.quotes[1].asset_id == old.quotes[1].asset_id,
                "cannot change a registered market's identity");
        require(input.quotes[0].received_at >= old.quotes[0].received_at
                    && input.quotes[1].received_at >= old.quotes[1].received_at,
                "quote reception time went backwards");
        require(old.revision != std::numeric_limits<std::uint64_t>::max(), "market revision overflow");
        input.revision = old.revision + 1;
    } else {
        input.revision = 1;
    }
    const auto id = input.market_id;
    markets_.insert_or_assign(id, std::move(input));
}

}  // namespace match_signals
