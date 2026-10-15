#include "check.hpp"
#include "fixtures.hpp"

#include <limits>

int main() {
    Time now = Time{} + 10s;
    int calls = 0;
    auto model = [&](const MatchContext& context) { ++calls; return forecast(context); };
    TradingEngine engine(model, buy, limits(), [&] { return now; });
    engine.set_portfolio(decimal("100"), {});
    engine.update_market(market(now));
    auto update = full(now);
    auto decisions = engine.on_sports_update(update);
    CHECK(decisions.size() == 1);
    CHECK(decisions[0].based_on_revision == 1 && decisions[0].market_revision == 1);
    CHECK(engine.portfolio().cash == decimal("100"));
    CHECK(engine.portfolio().orders.empty());
    CHECK(engine.on_sports_update(update).empty() && calls == 1);

    update.kind = FeedKind::Delta;
    update.state = {};
    update.events = {event("late", 1030, 500), event("danger", 1029, 2'000)};
    CHECK(engine.on_sports_update(update).size() == 1 && calls == 2);
    CHECK(engine.match("match-1").history[0].id == "late");
    CHECK(engine.match("match-1").history[2].id == "danger");

    update.kind = FeedKind::Correction;
    update.state.score = {{9, 9}};
    update.events = {event("danger", 1126, 1'500)};
    update.events[0].attributes["reason"] = "corrected";
    engine.on_sports_update(update);
    CHECK(engine.match("match-1").state.score[0] == 0);
    CHECK(engine.match("match-1").history[2].type == 1126);
    CHECK(engine.match("match-1").history[2].attributes.at("reason") == "corrected");

    update.events = {event("delete", 1044, 2'500)};
    update.events[0].attributes["refuuid"] = "danger";
    engine.on_sports_update(update);
    CHECK(engine.match("match-1").history.size() == 2);
    update.kind = FeedKind::Delta;
    update.state = {};
    update.events = {event("danger", 1029, 2'000)};
    CHECK(engine.on_sports_update(update).empty());
    CHECK(engine.match("match-1").history.size() == 2);

    // A malformed batch must not partially append an otherwise valid event.
    update.events = {event("new", 1126, 3'000), event("", 1126, 4'000)};
    rejects<std::invalid_argument>([&] { engine.on_sports_update(update); });
    CHECK(engine.match("match-1").history.size() == 2);
    update.kind = FeedKind::Correction;
    update.events = {event("unknown", 1126, 5'000)};
    rejects<std::logic_error>([&] { engine.on_sports_update(update); });
    CHECK(!engine.match("match-1").synchronized && engine.evaluate("match-1").empty());
    rejects<std::logic_error>([&] { engine.on_sports_update(update); });
    CHECK(engine.on_sports_update(full(now)).size() == 1);

    update = full(now);
    update.kind = FeedKind::Delta;
    update.state.period = "HALFTIME";
    const int before = calls;
    CHECK(engine.on_sports_update(update).empty() && calls == before + 1);
    engine.feed_disconnected();
    CHECK(engine.evaluate("match-1").empty());
    engine.on_sports_update(full(now));
    now += 3s;
    CHECK(engine.evaluate("match-1").empty());
    engine.update_market(market(now));
    CHECK(engine.evaluate("match-1").size() == 1);
    now += 31s;
    CHECK(engine.evaluate("match-1").empty());

    // A model that finishes after the quote expires must not emit an order.
    now = Time{} + 10s;
    TradingEngine slow([&](const MatchContext& context) { now += 3s; return forecast(context); },
                       buy, limits(), [&] { return now; });
    slow.set_portfolio(decimal("100"), {});
    slow.update_market(market(now));
    CHECK(slow.on_sports_update(full(now)).empty());

    TradingEngine invalid([](const MatchContext& context) {
        auto result = forecast(context);
        result.home_win = std::numeric_limits<double>::quiet_NaN();
        return result;
    }, buy, limits(), [&] { return now; });
    rejects<std::invalid_argument>([&] { invalid.on_sports_update(full(now)); });
    CHECK(invalid.match("match-1").history.size() == 1);

    TradingEngine* pointer = nullptr;
    TradingEngine reentrant([&](const MatchContext& context) {
        pointer->feed_disconnected();
        return forecast(context);
    }, buy, limits(), [&] { return now; });
    pointer = &reentrant;
    rejects<std::logic_error>([&] { reentrant.on_sports_update(full(now)); });
}
