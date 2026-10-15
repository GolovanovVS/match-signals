#pragma once

#include <match_signals/amount.hpp>
#include <match_signals/clock.hpp>

#include <array>
#include <optional>
#include <string>

namespace match_signals {

struct Level {
    Units price = 0;
    Units quantity = 0;
};

struct Quote {
    std::string asset_id;
    std::optional<Level> bid;
    std::optional<Level> ask;
    Units tick_size = 0;
    Units min_quantity = 0;
    Time received_at{};
};

enum class Target { HomeWin, Draw, AwayWin };

struct MarketState {
    std::string market_id;
    std::string match_id;
    Target target = Target::HomeWin;
    std::array<Quote, 2> quotes;
    std::uint64_t revision = 0;
};

}  // namespace match_signals
