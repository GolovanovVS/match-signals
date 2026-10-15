#pragma once

#include <match_signals/engine.hpp>

using namespace match_signals;
using namespace std::chrono_literals;

inline RiskLimits limits() {
    return {decimal("20"), decimal("30"), decimal("20"), 2s, 30s};
}

inline MarketState market(Time now) {
    MarketState result;
    result.market_id = "home-win";
    result.match_id = "match-1";
    result.quotes = {{
        {"asset-yes", Level{decimal("0.59"), decimal("50")}, Level{decimal("0.60"), decimal("50")},
         decimal("0.01"), decimal("5"), now},
        {"asset-no", Level{decimal("0.39"), decimal("50")}, Level{decimal("0.40"), decimal("50")},
         decimal("0.01"), decimal("5"), now}
    }};
    return result;
}

inline SportsEvent event(std::string id, int type, std::int64_t time) {
    return {std::move(id), type, Side::Home, time, 1min, "FIRST_HALF", {}, {}};
}

inline SportsUpdate full(Time now) {
    SportsUpdate result;
    result.match_id = "match-1";
    result.kind = FeedKind::Full;
    result.state.home_team = "home";
    result.state.away_team = "away";
    result.state.period = "FIRST_HALF";
    result.state.clock = 1min;
    result.state.score = {{0, 0}};
    result.state.source_connected = true;
    result.events = {event("attack", 1126, 1'000)};
    result.received_at = now;
    return result;
}

inline ProbabilityWin forecast(const MatchContext& context) {
    return {context.match_id, context.revision, 0.6, 0.2, 0.2};
}

inline std::optional<TradeDecision> buy(const ProbabilityWin&, const MarketState& input,
                                       const PortfolioState&, const RiskLimits&) {
    return TradeDecision{input.market_id, Action::Buy, Outcome::Yes, decimal("5"), decimal("0.60")};
}
