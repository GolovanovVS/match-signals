#pragma once

#include <match_signals/clock.hpp>

#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

namespace match_signals {

enum class Side { Home, Away, None };
enum class FeedKind { Full, Delta, Correction };

struct SportsEvent {
    std::string id;
    int type = 0;
    Side side = Side::None;
    std::int64_t source_time_ms = 0;
    std::chrono::milliseconds clock{};
    std::string period;
    std::map<std::string, std::string> attributes;
    Time received_at{};
};

struct MatchState {
    std::string home_team;
    std::string away_team;
    std::string period;
    std::chrono::milliseconds clock{};
    std::array<int, 2> score{};
    std::optional<std::array<int, 2>> red_cards;
    bool source_connected = false;
};

struct MatchPatch {
    std::optional<std::string> home_team;
    std::optional<std::string> away_team;
    std::optional<std::string> period;
    std::optional<std::chrono::milliseconds> clock;
    std::optional<std::array<int, 2>> score;
    std::optional<std::array<int, 2>> red_cards;
    std::optional<bool> source_connected;
};

struct SportsUpdate {
    std::string match_id;
    FeedKind kind = FeedKind::Delta;
    MatchPatch state;
    std::vector<SportsEvent> events;
    Time received_at{};
};

struct MatchContext {
    std::string match_id;
    std::uint64_t revision = 0;
    MatchState state;
    std::vector<SportsEvent> history;
    Time received_at{};
    bool synchronized = false;
    std::unordered_set<std::string> deleted_events;
};

}  // namespace match_signals
