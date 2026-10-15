#include <match_signals/engine.hpp>

#include <utility>

#include "checked.hpp"

namespace match_signals {
using detail::require;
using detail::add;

namespace {

std::size_t index(Outcome outcome) {
    require(outcome == Outcome::Yes || outcome == Outcome::No, "invalid outcome");
    return outcome == Outcome::Yes ? 0U : 1U;
}

}  // namespace

Units PortfolioState::position(const std::string& asset_id) const {
    auto found = positions.find(asset_id);
    return found == positions.end() ? 0 : found->second;
}

Units PortfolioState::available_cash() const {
    Units reserved = 0;
    for (const auto& entry : orders) {
        const auto& order = entry.second;
        if (!order.terminal && order.decision.action == Action::Buy) {
            reserved = add(reserved, order.cash_bound - order.cash_total);
        }
    }
    require(reserved <= cash, "cash reservations exceed balance");
    return cash - reserved;
}

Units PortfolioState::available_position(const std::string& asset_id) const {
    Units reserved = 0;
    for (const auto& entry : orders) {
        const auto& order = entry.second;
        if (!order.terminal && order.decision.action == Action::Sell && order.asset_id == asset_id) {
            reserved = add(reserved, order.decision.count - order.filled_count);
        }
    }
    require(reserved <= position(asset_id), "token reservations exceed balance");
    return position(asset_id) - reserved;
}

Units PortfolioState::pending_buys(const std::string& asset_id) const {
    Units pending = 0;
    for (const auto& entry : orders) {
        const auto& order = entry.second;
        if (!order.terminal && order.decision.action == Action::Buy && order.asset_id == asset_id) {
            pending = add(pending, order.decision.count - order.filled_count);
        }
    }
    return pending;
}

void TradingEngine::set_portfolio(Units cash, std::map<std::string, Units> positions) {
    require_mutable();
    require(cash >= 0, "negative cash balance");
    for (const auto& item : positions) require(!item.first.empty() && item.second >= 0, "invalid position");
    for (const auto& item : portfolio_.orders) require(item.second.terminal, "cannot replace a portfolio with active orders");
    portfolio_.cash = cash;
    portfolio_.positions = std::move(positions);
    portfolio_ready_ = true;
}

void TradingEngine::validate_decision(const TradeDecision& decision, Units fee_budget) const {
    const auto& input = market(decision.market_id);
    const auto& context = match(input.match_id);
    require(portfolio_ready_ && fresh(context, input), "decision inputs are unavailable or stale");
    require(decision.based_on_revision == context.revision && decision.market_revision == input.revision,
            "decision revision is stale");
    require(decision.action == Action::Buy || decision.action == Action::Sell, "invalid action");
    const auto& quote = input.quotes[index(decision.token)];
    require(decision.count >= quote.min_quantity && decision.count <= limits_.max_order_quantity
                && decision.count % 10'000 == 0, "invalid share quantity");
    require(decision.limit_price > 0 && decision.limit_price < scale
                && decision.limit_price % quote.tick_size == 0 && fee_budget >= 0,
            "invalid price or fee budget");
    const auto bound = add(notional(decision.limit_price, decision.count, true), fee_budget);
    require(bound <= limits_.max_order_notional, "order notional limit exceeded");
    if (decision.action == Action::Buy) {
        require(bound <= portfolio_.available_cash(), "insufficient available cash");
        const auto exposure = add(add(portfolio_.position(quote.asset_id), portfolio_.pending_buys(quote.asset_id)), decision.count);
        require(exposure <= limits_.max_position_quantity, "position limit exceeded");
    } else {
        require(decision.count <= portfolio_.available_position(quote.asset_id), "insufficient available tokens");
    }
}

void TradingEngine::reserve_order(const std::string& order_id, const TradeDecision& decision, Units fee_budget) {
    require_mutable();
    require(!order_id.empty() && portfolio_.orders.count(order_id) == 0, "order ID is missing or reused");
    validate_decision(decision, fee_budget);
    const auto& asset = market(decision.market_id).quotes[index(decision.token)].asset_id;
    OpenOrder order;
    order.decision = decision;
    order.asset_id = asset;
    order.fee_budget = fee_budget;
    order.cash_bound = add(notional(decision.limit_price, decision.count, true), fee_budget);
    portfolio_.orders.emplace(order_id, std::move(order));
}

bool TradingEngine::update_order(const OrderReport& report) {
    require_mutable();
    auto found = portfolio_.orders.find(report.order_id);
    require(found != portfolio_.orders.end(), "unknown order report");
    const auto& previous = found->second;
    require(report.filled_count >= previous.filled_count && report.filled_count <= previous.decision.count
                && report.cash_total >= previous.cash_total, "order totals went backwards or exceeded quantity");
    if (report.filled_count == previous.filled_count && report.cash_total == previous.cash_total
        && (report.terminal == previous.terminal || previous.filled_count == previous.decision.count)) return false;
    require(!previous.terminal, "cannot change a terminal order");
    require(report.filled_count > 0 || report.cash_total == 0, "cash reported without an execution");
    const Units quantity_delta = report.filled_count - previous.filled_count;
    const Units cash_delta = report.cash_total - previous.cash_total;
    Units next_cash = portfolio_.cash;
    Units next_position = portfolio_.position(previous.asset_id);
    if (previous.decision.action == Action::Buy) {
        const auto maximum = add(notional(previous.decision.limit_price, report.filled_count, true), previous.fee_budget);
        require(report.cash_total <= maximum && cash_delta <= next_cash, "buy execution exceeds price or cash bound");
        next_cash -= cash_delta;
        next_position = add(next_position, quantity_delta);
    } else {
        const auto minimum = notional(previous.decision.limit_price, report.filled_count, false);
        require(add(report.cash_total, previous.fee_budget) >= minimum && quantity_delta <= next_position,
                "sell execution violates price or position bound");
        next_cash = add(next_cash, cash_delta);
        next_position -= quantity_delta;
    }
    OpenOrder next = previous;
    next.filled_count = report.filled_count;
    next.cash_total = report.cash_total;
    next.terminal = report.terminal || report.filled_count == previous.decision.count;
    portfolio_.positions.insert_or_assign(next.asset_id, next_position);
    portfolio_.cash = next_cash;
    found->second = std::move(next);
    return true;
}

}  // namespace match_signals
