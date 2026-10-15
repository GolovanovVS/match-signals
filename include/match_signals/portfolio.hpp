#pragma once

#include <match_signals/trade.hpp>

#include <map>
#include <string>

namespace match_signals {

struct OpenOrder {
    TradeDecision decision;
    std::string asset_id;
    Units filled_count = 0;
    Units cash_total = 0;
    Units cash_bound = 0;
    Units fee_budget = 0;
    bool terminal = false;
};

struct PortfolioState {
    Units cash = 0;
    std::map<std::string, Units> positions;
    std::map<std::string, OpenOrder> orders;

    Units available_cash() const;
    Units position(const std::string& asset_id) const;
    Units available_position(const std::string& asset_id) const;
    Units pending_buys(const std::string& asset_id) const;
};

struct OrderReport {
    std::string order_id;
    Units filled_count = 0;
    Units cash_total = 0;
    bool terminal = false;
};

}  // namespace match_signals
