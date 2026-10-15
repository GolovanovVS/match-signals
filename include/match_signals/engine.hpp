#pragma once

#include <match_signals/models.hpp>

#include <map>
#include <vector>

namespace match_signals {

// shortcut: one owner thread, serialize calls externally before adding concurrent sources.
class TradingEngine {
public:
    TradingEngine(ProbabilityModel probability_model, DecisionModel decision_model,
                  RiskLimits limits, Now now = std::chrono::steady_clock::now);

    void set_portfolio(Units cash, std::map<std::string, Units> positions);
    void update_market(MarketState market);
    std::vector<TradeDecision> on_sports_update(const SportsUpdate& update);
    std::vector<TradeDecision> evaluate(const std::string& match_id);
    void feed_disconnected();

    void reserve_order(const std::string& order_id, const TradeDecision& decision,
                       Units fee_budget = 0);
    bool update_order(const OrderReport& report);

    const MatchContext& match(const std::string& match_id) const;
    const MarketState& market(const std::string& market_id) const;
    const PortfolioState& portfolio() const noexcept { return portfolio_; }

private:
    void require_mutable() const;
    void validate_decision(const TradeDecision& decision, Units fee_budget) const;
    bool fresh(const MatchContext& context, const MarketState& market) const;

    ProbabilityModel probability_model_;
    DecisionModel decision_model_;
    RiskLimits limits_;
    Now now_;
    std::map<std::string, MatchContext> matches_;
    std::map<std::string, MarketState> markets_;
    PortfolioState portfolio_;
    bool portfolio_ready_ = false;
    bool evaluating_ = false;
};

}  // namespace match_signals
