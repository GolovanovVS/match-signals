#include <match_signals/engine.hpp>

#include <cmath>
#include <utility>

#include "checked.hpp"

namespace match_signals {
using detail::require;

namespace {

bool usable_period(const std::string& period) {
    return period == "FIRST_HALF" || period == "SECOND_HALF";
}

bool within_age(Time received, Time now, std::chrono::milliseconds age) {
    return received <= now && now - received <= age;
}

}  // namespace

TradingEngine::TradingEngine(ProbabilityModel probability_model, DecisionModel decision_model,
                             RiskLimits limits, Now now)
    : probability_model_(std::move(probability_model)), decision_model_(std::move(decision_model)),
      limits_(limits), now_(std::move(now)) {
    require(probability_model_ && decision_model_ && now_, "models and clock are required");
    require(limits.max_order_quantity > 0 && limits.max_position_quantity > 0
                && limits.max_order_notional > 0 && limits.max_quote_age.count() >= 0
                && limits.max_event_age.count() >= 0,
            "invalid risk limits");
}

void TradingEngine::require_mutable() const {
    if (evaluating_) throw std::logic_error("models must not mutate or reenter the engine");
}

bool TradingEngine::fresh(const MatchContext& context, const MarketState& input) const {
    const auto now = now_();
    return context.synchronized && context.state.source_connected && usable_period(context.state.period)
        && within_age(context.received_at, now, limits_.max_event_age)
        && within_age(input.quotes[0].received_at, now, limits_.max_quote_age)
        && within_age(input.quotes[1].received_at, now, limits_.max_quote_age);
}

std::vector<TradeDecision> TradingEngine::evaluate(const std::string& match_id) {
    require_mutable();
    const auto& context = match(match_id);
    if (!context.synchronized || !context.state.source_connected
        || !within_age(context.received_at, now_(), limits_.max_event_age)) return {};
    evaluating_ = true;
    try {
        const auto forecast = probability_model_(context);
        const auto probability = [](double value) { return std::isfinite(value) && value >= 0 && value <= 1; };
        require(forecast.match_id == context.match_id && forecast.revision == context.revision
                    && probability(forecast.home_win) && probability(forecast.draw) && probability(forecast.away_win)
                    && std::abs(forecast.home_win + forecast.draw + forecast.away_win - 1.0) <= 1e-6,
                "invalid probability model output");
        std::vector<TradeDecision> decisions;
        if (portfolio_ready_) {
            for (const auto& entry : markets_) {
                const auto& input = entry.second;
                if (input.match_id != match_id || !fresh(context, input)) continue;
                auto decision = decision_model_(forecast, input, portfolio_, limits_);
                if (!decision) continue;
                require(decision->market_id == input.market_id, "decision references the wrong market");
                decision->based_on_revision = context.revision;
                decision->market_revision = input.revision;
                if (!fresh(context, input)) continue;
                validate_decision(*decision, 0);
                decisions.push_back(std::move(*decision));
            }
        }
        evaluating_ = false;
        return decisions;
    } catch (...) {
        evaluating_ = false;
        throw;
    }
}

const MatchContext& TradingEngine::match(const std::string& match_id) const { return matches_.at(match_id); }
const MarketState& TradingEngine::market(const std::string& market_id) const { return markets_.at(market_id); }

}  // namespace match_signals
