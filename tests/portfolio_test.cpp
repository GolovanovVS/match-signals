#include "check.hpp"
#include "fixtures.hpp"

int main() {
    Time now = Time{} + 10s;
    TradingEngine engine(forecast, buy, limits(), [&] { return now; });
    engine.set_portfolio(decimal("10"), {{"asset-no", decimal("10")}});
    engine.update_market(market(now));
    const auto decision = engine.on_sports_update(full(now)).at(0);
    engine.reserve_order("buy-1", decision, decimal("0.1"));
    CHECK(engine.portfolio().cash == decimal("10"));
    CHECK(engine.portfolio().available_cash() == decimal("6.9"));
    CHECK(engine.portfolio().position("asset-yes") == 0);
    rejects<std::invalid_argument>([&] { engine.reserve_order("buy-1", decision); });
    rejects<std::invalid_argument>([&] { engine.set_portfolio(0, {}); });
    CHECK(engine.update_order({"buy-1", decimal("2"), decimal("1.1"), false}));
    CHECK(engine.portfolio().cash == decimal("8.9"));
    CHECK(engine.portfolio().available_cash() == decimal("6.9"));
    CHECK(engine.portfolio().position("asset-yes") == decimal("2"));
    CHECK(!engine.update_order({"buy-1", decimal("2"), decimal("1.1"), false}));
    rejects<std::invalid_argument>([&] { engine.update_order({"buy-1", decimal("1"), decimal("1.1"), false}); });
    rejects<std::invalid_argument>([&] { engine.update_order({"buy-1", decimal("3"), decimal("9"), false}); });
    CHECK(engine.portfolio().cash == decimal("8.9"));
    CHECK(engine.update_order({"buy-1", decimal("2"), decimal("1.1"), true}));
    CHECK(engine.portfolio().available_cash() == decimal("8.9"));
    CHECK(!engine.update_order({"buy-1", decimal("2"), decimal("1.1"), true}));
    rejects<std::invalid_argument>([&] { engine.update_order({"buy-1", decimal("3"), decimal("1.7"), true}); });

    auto sell = decision;
    sell.action = Action::Sell;
    sell.token = Outcome::No;
    sell.limit_price = decimal("0.39");
    engine.reserve_order("sell-1", sell);
    CHECK(engine.portfolio().available_position("asset-no") == decimal("5"));
    CHECK(engine.update_order({"sell-1", decimal("5"), decimal("2"), true}));
    CHECK(engine.portfolio().cash == decimal("10.9"));
    CHECK(engine.portfolio().position("asset-no") == decimal("5"));
    CHECK(!engine.update_order({"sell-1", decimal("5"), decimal("2"), false}));

    auto bad = decision;
    bad.count = decimal("40");
    rejects<std::invalid_argument>([&] { engine.reserve_order("too-large", bad); });
    bad = decision;
    bad.limit_price = decimal("0.601");
    rejects<std::invalid_argument>([&] { engine.reserve_order("off-tick", bad); });
    bad = decision;
    bad.count = decimal("5.001");
    rejects<std::invalid_argument>([&] { engine.reserve_order("off-step", bad); });
    engine.update_market(market(now));
    rejects<std::invalid_argument>([&] { engine.reserve_order("old-revision", decision); });
    auto current = engine.evaluate("match-1").at(0);
    now += 3s;
    rejects<std::invalid_argument>([&] { engine.reserve_order("expired", current); });

    // Pending orders consume cash and position limits before any fill arrives.
    now = Time{} + 10s;
    TradingEngine limited(forecast, buy, limits(), [&] { return now; });
    limited.set_portfolio(decimal("5"), {});
    limited.update_market(market(now));
    auto proposal = limited.on_sports_update(full(now)).at(0);
    limited.reserve_order("first", proposal);
    rejects<std::invalid_argument>([&] { limited.reserve_order("second", proposal); });
    limited.update_order({"first", 0, 0, true});
    limited.set_portfolio(decimal("100"), {{"asset-yes", decimal("27")}});
    rejects<std::invalid_argument>([&] { limited.reserve_order("position-limit", proposal); });
    rejects<std::invalid_argument>([&] { limited.update_order({"unknown", 0, 0, true}); });
}
