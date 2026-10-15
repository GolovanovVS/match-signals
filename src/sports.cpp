#include <match_signals/engine.hpp>

#include <algorithm>
#include <limits>
#include <utility>

#include "checked.hpp"

namespace match_signals {
using detail::require;

namespace {

bool same_event(const SportsEvent& a, const SportsEvent& b) {
    return a.id == b.id && a.type == b.type && a.side == b.side
        && a.source_time_ms == b.source_time_ms && a.clock == b.clock
        && a.period == b.period && a.attributes == b.attributes;
}

bool same_state(const MatchState& a, const MatchState& b) {
    return a.home_team == b.home_team && a.away_team == b.away_team
        && a.period == b.period && a.clock == b.clock && a.score == b.score
        && a.red_cards == b.red_cards && a.source_connected == b.source_connected;
}

void apply_patch(MatchState& state, const MatchPatch& patch) {
    if (patch.home_team) state.home_team = *patch.home_team;
    if (patch.away_team) state.away_team = *patch.away_team;
    if (patch.period) state.period = *patch.period;
    if (patch.clock) state.clock = *patch.clock;
    if (patch.score) state.score = *patch.score;
    if (patch.red_cards) state.red_cards = patch.red_cards;
    if (patch.source_connected) state.source_connected = *patch.source_connected;
    require(state.clock.count() >= 0 && state.score[0] >= 0 && state.score[1] >= 0,
            "invalid match state");
    if (state.red_cards) {
        require((*state.red_cards)[0] >= 0 && (*state.red_cards)[1] >= 0, "invalid red cards");
    }
}

void validate_events(const SportsUpdate& update) {
    std::unordered_set<std::string> ids;
    for (const auto& event : update.events) {
        require(!event.id.empty() && event.type > 0 && event.source_time_ms >= 0
                    && event.clock.count() >= 0, "invalid sports event");
        require(event.side == Side::Home || event.side == Side::Away || event.side == Side::None,
                "invalid event side");
        require(ids.insert(event.id).second, "duplicate UUID within a message");
        if (event.type == 1044) {
            const auto target = event.attributes.find("refuuid");
            require(target != event.attributes.end() && !target->second.empty(),
                    "deletion requires refuuid");
        }
    }
}

bool apply_events(MatchContext& context, const SportsUpdate& update) {
    bool changed = false;
    bool reorder = false;
    for (auto event : update.events) {
        event.received_at = update.received_at;
        if (event.type == 1044) {
            const auto& id = event.attributes.at("refuuid");
            changed |= context.deleted_events.insert(id).second;
            const auto end = std::remove_if(context.history.begin(), context.history.end(),
                [&](const SportsEvent& old) { return old.id == id; });
            changed |= end != context.history.end();
            context.history.erase(end, context.history.end());
            continue;
        }
        if (context.deleted_events.count(event.id)) continue;
        const auto found = std::find_if(context.history.begin(), context.history.end(),
            [&](const SportsEvent& old) { return old.id == event.id; });
        if (update.kind == FeedKind::Correction) {
            for (const auto& attribute : found->attributes) event.attributes.emplace(attribute);
        }
        if (found == context.history.end()) {
            reorder |= !context.history.empty()
                && context.history.back().source_time_ms > event.source_time_ms;
            context.history.push_back(std::move(event));
            changed = true;
        } else if (!same_event(*found, event)) {
            reorder |= found->source_time_ms != event.source_time_ms;
            *found = std::move(event);
            changed = true;
        }
    }
    if (reorder) std::stable_sort(context.history.begin(), context.history.end(),
        [](const SportsEvent& a, const SportsEvent& b) { return a.source_time_ms < b.source_time_ms; });
    return changed;
}

}  // namespace

std::vector<TradeDecision> TradingEngine::on_sports_update(const SportsUpdate& update) {
    require_mutable();
    require(!update.match_id.empty() && update.received_at <= now_(), "invalid sports update");
    require(update.kind == FeedKind::Full || update.kind == FeedKind::Delta || update.kind == FeedKind::Correction,
            "invalid feed kind");
    auto previous = matches_.find(update.match_id);
    if (update.kind != FeedKind::Full
        && (previous == matches_.end() || !previous->second.synchronized)) {
        throw std::logic_error("a full match snapshot is required before deltas");
    }
    if (previous != matches_.end()) require(update.received_at >= previous->second.received_at,
                                            "sports reception time went backwards");

    validate_events(update);
    MatchContext snapshot;
    snapshot.match_id = update.match_id;
    MatchContext& context = update.kind == FeedKind::Full ? snapshot : previous->second;
    MatchState next_state = context.state;
    if (update.kind == FeedKind::Full) {
        require(update.state.home_team && update.state.away_team && update.state.period
                    && update.state.clock && update.state.score && update.state.source_connected,
                "incomplete full match snapshot");
        context.synchronized = true;
    }
    // Correction envelopes contain historical score/clock values, not current state.
    if (update.kind != FeedKind::Correction) apply_patch(next_state, update.state);
    require(!next_state.home_team.empty() && !next_state.away_team.empty()
                && !next_state.period.empty(), "incomplete match identity");
    require(context.revision != std::numeric_limits<std::uint64_t>::max(), "match revision overflow");
    if (update.kind == FeedKind::Correction) {
        for (const auto& event : update.events) {
            if (event.type == 1044 || context.deleted_events.count(event.id)) continue;
            const bool known = std::any_of(context.history.begin(), context.history.end(),
                [&](const SportsEvent& old) { return old.id == event.id; });
            if (!known) {
                context.synchronized = false;
                throw std::logic_error("unknown correction; request a full match snapshot");
            }
        }
    }
    bool changed = !same_state(context.state, next_state);
    changed |= apply_events(context, update);
    context.state = std::move(next_state);
    context.received_at = update.received_at;
    if (update.kind == FeedKind::Full) {
        changed = previous == matches_.end() || !previous->second.synchronized
            || !same_state(context.state, previous->second.state)
            || context.history.size() != previous->second.history.size()
            || context.deleted_events != previous->second.deleted_events
            || !std::equal(context.history.begin(), context.history.end(), previous->second.history.begin(), same_event);
        context.revision = previous == matches_.end() ? 0 : previous->second.revision;
        require(context.revision != std::numeric_limits<std::uint64_t>::max(), "match revision overflow");
        matches_.insert_or_assign(update.match_id, std::move(snapshot));
    }
    if (!changed) return {};
    ++matches_.at(update.match_id).revision;
    return evaluate(update.match_id);
}

void TradingEngine::feed_disconnected() {
    require_mutable();
    for (auto& entry : matches_) entry.second.synchronized = false;
}

}  // namespace match_signals
