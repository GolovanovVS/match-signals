#pragma once

#include <match_signals/amount.hpp>

#include <cstdint>
#include <string>

namespace match_signals {

enum class Outcome { Yes, No };
enum class Action { Buy, Sell };

struct TradeDecision {
    std::string market_id;
    Action action = Action::Buy;
    Outcome token = Outcome::Yes;
    Units count = 0;
    Units limit_price = 0;
    std::uint64_t based_on_revision = 0;
    std::uint64_t market_revision = 0;
};

}  // namespace match_signals
