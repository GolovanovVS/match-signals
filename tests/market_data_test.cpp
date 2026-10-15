#include <match_signals/market_data.hpp>

#include "check.hpp"

using namespace match_signals;
using namespace std::chrono_literals;

namespace {

std::string book(const std::string& asset) {
    return "{\"market\":\"condition\",\"asset_id\":\"" + asset + R"(",
        "tick_size":"0.01","min_order_size":"5",
        "bids":[{"price":"0.55","size":"10"},{"price":"0.59","size":"20"}],
        "asks":[{"price":"0.70","size":"10"},{"price":"0.60","size":"20"}]})";
}

std::string replace(std::string input, const std::string& before, const std::string& after) {
    input.replace(input.find(before), before.size(), after);
    return input;
}

}  // namespace

int main() {
    Time now = Time{} + 10s;
    auto quote = parse_order_book(book("111"), "condition", "111", now);
    CHECK(quote.bid->price == decimal("0.59"));
    CHECK(quote.ask->price == decimal("0.60"));
    CHECK(quote.tick_size == decimal("0.01") && quote.received_at == now);
    rejects<std::invalid_argument>([&] { parse_order_book(book("111"), "wrong", "111", now); });
    rejects<std::invalid_argument>([&] { parse_order_book(book("111"), "condition", "222", now); });
    for (const auto& bad : {
        std::string("{"),
        replace(book("111"), "\"0.01\"", "\"0\""),
        replace(book("111"), "\"0.55\"", "0.55"),
        replace(book("111"), "\"0.59\"", "\"0.80\""),
        replace(book("111"), "\"0.55\"", "\"0.551\""),
        replace(book("111"), "\"10\"", "\"-1\"")
    }) {
        rejects<std::invalid_argument>([&] { parse_order_book(bad, "condition", "111", now); });
    }
    rejects<std::invalid_argument>([&] {
        parse_order_book(std::string(64, '[') + std::string(64, ']'), "condition", "111", now);
    });
    quote = parse_order_book(R"({"market":"condition","asset_id":"111","tick_size":"0.01",
                                "min_order_size":"5","bids":[],"asks":[]})", "condition", "111", now);
    CHECK(!quote.bid && !quote.ask);

    int fetched = 0;
    bool fail = false;
    MarketDataClient client({"condition", "match-1", Target::HomeWin, {"111", "222"}}, 100ms,
                            [&] { return now; }, [&](const std::string& asset) {
        ++fetched;
        if (fail) throw std::runtime_error("network unavailable");
        now += 10ms;
        return book(asset);
    });
    const auto snapshot = client.poll();
    CHECK(snapshot && fetched == 2 && snapshot->quotes[0].asset_id == "111" && snapshot->quotes[1].asset_id == "222");
    CHECK(snapshot->quotes[0].received_at == Time{} + 10s);
    CHECK(snapshot->quotes[1].received_at == Time{} + 10s + 10ms);
    CHECK(!client.poll() && fetched == 2);
    now = Time{} + 10s + 99ms;
    CHECK(!client.poll());
    now += 1ms;
    CHECK(client.poll() && fetched == 4);
    now = Time{} + 10s + 200ms;
    fail = true;
    rejects<std::runtime_error>([&] { client.poll(); });
    CHECK(!client.poll() && fetched == 5);
    now += 100ms;
    fail = false;
    CHECK(client.poll() && fetched == 7);

    int slow_calls = 0;
    MarketDataClient slow({"condition", "match-1", Target::HomeWin, {"111", "222"}}, 100ms,
                         [&] { return now; }, [&](const std::string& asset) {
        ++slow_calls;
        now += 250ms;
        return book(asset);
    });
    CHECK(slow.poll() && slow_calls == 2);
    CHECK(!slow.poll());
}
