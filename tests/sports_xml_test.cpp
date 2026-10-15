#include <match_signals/sports_feed.hpp>

#include "check.hpp"
#include "fixtures.hpp"

namespace {

const std::string attack = R"(<event uuid="a" id="123" type="1126" side="home" stime="1000"
    mtime="01:00" matchstatus="FIRST_HALF" posx="40" info="A &amp; B"/>)";
const std::string danger = R"(<event uuid="b" type="1029" side="home" stime="2000"
    mtime="01:01" matchstatus="FIRST_HALF"/>)";

std::string snapshot(const std::string& extra = "", const std::string& events = attack,
                     const std::string& kind = "full") {
    return "<match matchid=\"match-1\" t1id=\"home\" t2id=\"away\" feedtype=\"" + kind + "\" " + extra + R"(
        connectionstatus="1" matchtime="45:00 +1:32"><status name="FIRST_HALF"/>
        <score type="current" t1="0" t2="0"/><red t1="0" t2="1"/><events>)" + events + "</events></match>\r\n\r\n";
}

}  // namespace

int main() {
    const auto now = Time{} + 10s;
    const auto xml = snapshot();
    SportsXmlDecoder decoder;
    CHECK(decoder.push(std::string_view(xml).substr(0, 17), now).empty());
    const auto messages = decoder.push(std::string_view(xml).substr(17), now);
    CHECK(messages.size() == 1);
    const auto& update = std::get<SportsUpdate>(messages[0]);
    CHECK(update.kind == FeedKind::Full);
    CHECK(update.state.clock == 46min + 32s);
    CHECK(update.state.red_cards->at(1) == 1);
    CHECK(update.events.size() == 1 && update.events[0].id == "a");
    CHECK(update.events[0].attributes.at("info") == "A & B");
    CHECK(update.events[0].attributes.count("id") == 0);

    const auto administrative = decoder.push("<ct/>\r\n\r\n<login result=\"valid\"/>\r\n\r\n"
                                             "<matchstop matchid=\"match-1\"/>\r\n\r\n", now);
    CHECK(administrative.size() == 3);
    CHECK(std::holds_alternative<FeedAlive>(administrative[0]));
    CHECK(std::get<FeedLogin>(administrative[1]).accepted);
    CHECK(std::get<FeedStopped>(administrative[2]).match_id == "match-1");

    const auto first = snapshot("uuid=\"snapshot-1\" page=\"0\" totalpages=\"2\"", attack, "full_paginated");
    const auto second = snapshot("uuid=\"snapshot-1\" page=\"1\" totalpages=\"2\"", danger, "partial");
    CHECK(decoder.push(first, now).empty());
    const auto assembled = decoder.push(second, now);
    CHECK(assembled.size() == 1);
    CHECK(std::get<SportsUpdate>(assembled[0]).events.size() == 2);
    rejects<std::invalid_argument>([&] { decoder.push(second, now); });
    CHECK(decoder.push(first + second, now).size() == 1);
    CHECK(decoder.push(first, now).empty());
    rejects<std::invalid_argument>([&] {
        decoder.push(snapshot("uuid=\"wrong\" page=\"1\" totalpages=\"2\"", danger, "partial"), now);
    });
    CHECK(decoder.push(first, now).empty());
    rejects<std::invalid_argument>([&] { decoder.push(snapshot("", danger, "delta"), now); });
    CHECK(decoder.push(xml, now).size() == 1);

    for (const auto& bad : {
        std::string("<!DOCTYPE ct [<!ENTITY local SYSTEM 'file:///etc/passwd'>]><ct/>\r\n\r\n"),
        std::string("<match feedtype=\"unsupported\" matchid=\"match-1\"/>\r\n\r\n"),
        snapshot("", R"(<event uuid="bad" type="1126" side="home" stime="1000" mtime="01:99"/>)"),
        snapshot("", R"(<event id="123" type="1126" side="home" stime="1000" mtime="01:00"/>)")
    }) {
        rejects<std::invalid_argument>([&] { decoder.push(bad, now); });
    }
    rejects<std::invalid_argument>([&] { decoder.push(std::string(8 * 1024 * 1024 + 1, 'x'), now); });

    // Complete provider frame -> typed history -> both model callbacks -> decision.
    TradingEngine engine(forecast, buy, limits(), [&] { return now; });
    engine.set_portfolio(decimal("100"), {});
    engine.update_market(market(now));
    CHECK(engine.on_sports_update(update).size() == 1);
    const auto corrections = decoder.push(R"(<match matchid="match-1" feedtype="deltaupdate" matchtime="00:00">
        <score type="current" t1="9" t2="9"/><events><event uuid="a" type="1126" side="home" stime="1000"
        mtime="01:00" matchstatus="FIRST_HALF" player1="17"/></events></match>)" "\r\n\r\n", now);
    engine.on_sports_update(std::get<SportsUpdate>(corrections[0]));
    CHECK(engine.match("match-1").state.score[0] == 0);
    CHECK(engine.match("match-1").history[0].attributes.at("player1") == "17");
    CHECK(engine.match("match-1").history[0].attributes.at("posx") == "40");
}
