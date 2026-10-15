#pragma once

#include <match_signals/market.hpp>
#include <match_signals/portfolio.hpp>
#include <match_signals/sports.hpp>

#include <functional>
#include <optional>

namespace match_signals {

struct RiskLimits {
    Units max_order_quantity = 0;
    Units max_position_quantity = 0;
    Units max_order_notional = 0;
    std::chrono::milliseconds max_quote_age{};
    std::chrono::milliseconds max_event_age{};
};

struct ProbabilityWin {
    std::string match_id;
    std::uint64_t revision = 0;
    double home_win = 0;
    double draw = 0;
    double away_win = 0;

    double yes_probability(Target target) const;
};

using ProbabilityModel = std::function<ProbabilityWin(const MatchContext&)>;
using DecisionModel = std::function<std::optional<TradeDecision>(
    const ProbabilityWin&, const MarketState&, const PortfolioState&, const RiskLimits&)>;

}  // namespace match_signals
