#include <match_signals/engine.hpp>
#include <match_signals/market_data.hpp>
#include <match_signals/sports_feed.hpp>

#include <iostream>

using namespace match_signals;
using namespace std::chrono_literals;

int main() {
    const auto now = std::chrono::steady_clock::now();

    // Deterministic model responses for this example; supply real inference callbacks here.
    ProbabilityModel probabilities = [](const MatchContext& context) {
        return ProbabilityWin{context.match_id, context.revision, 0.7, 0.2, 0.1};
    };
    DecisionModel decision = [](const ProbabilityWin&, const MarketState& market,
                                const PortfolioState&, const RiskLimits&) {
        return std::optional<TradeDecision>{{market.market_id, Action::Buy, Outcome::Yes,
                                            decimal("5"), decimal("0.60")}};
    };
    TradingEngine engine(probabilities, decision,
                         {decimal("10"), decimal("20"), decimal("10"), 2s, 30s});
    engine.set_portfolio(decimal("100"), {});

    MarketState market;
    market.market_id = "demo-condition";
    market.match_id = "100";
    market.quotes[0] = parse_order_book(R"({"market":"demo-condition","asset_id":"111",
        "tick_size":"0.01","min_order_size":"5",
        "bids":[{"price":"0.59","size":"50"}],"asks":[{"price":"0.60","size":"50"}]})",
        market.market_id, "111", now);
    market.quotes[1] = parse_order_book(R"({"market":"demo-condition","asset_id":"222",
        "tick_size":"0.01","min_order_size":"5",
        "bids":[{"price":"0.39","size":"50"}],"asks":[{"price":"0.40","size":"50"}]})",
        market.market_id, "222", now);
    engine.update_market(std::move(market));

    SportsXmlDecoder decoder;
    const auto messages = decoder.push(R"(<match matchid="100" t1id="home" t2id="away" feedtype="full"
        connectionstatus="1" matchtime="00:12:34"><status name="FIRST_HALF"/>
        <score type="current" t1="0" t2="0"/><events>
        <event uuid="demo-attack" type="1126" side="home" stime="1000" mtime="12:32" matchstatus="FIRST_HALF"/>
        <event uuid="demo-danger" type="1029" side="home" stime="2000" mtime="12:34" matchstatus="FIRST_HALF"/>
        </events></match>)" "\r\n\r\n", now);

    for (const auto& message : messages) {
        if (const auto update = std::get_if<SportsUpdate>(&message)) {
            for (const auto& proposal : engine.on_sports_update(*update)) {
                std::cout << "BUY YES count=" << decimal_string(proposal.count)
                          << " limit=" << decimal_string(proposal.limit_price)
                          << " revision=" << proposal.based_on_revision << '\n';
                engine.reserve_order("demo-order", proposal);
                std::cout << "Reserved: cash=" << decimal_string(engine.portfolio().cash)
                          << " available=" << decimal_string(engine.portfolio().available_cash()) << '\n';
                // A simulated confirmed fill; no request is sent to an exchange.
                engine.update_order({"demo-order", decimal("5"), decimal("3"), true});
                std::cout << "Filled: cash=" << decimal_string(engine.portfolio().cash)
                          << " position=" << decimal_string(engine.portfolio().position("111")) << '\n';
            }
        }
    }
}
