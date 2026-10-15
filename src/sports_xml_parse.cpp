#include "sports_xml.hpp"
#include "checked.hpp"

#include <libxml/parser.h>

#include <charconv>
#include <limits>
#include <memory>

namespace match_signals::detail {
namespace {

std::optional<std::string> attribute(xmlNode* node, const char* name) {
    std::unique_ptr<xmlChar, decltype(xmlFree)> value(xmlGetProp(node, BAD_CAST name), xmlFree);
    if (!value) return std::nullopt;
    return std::string(reinterpret_cast<const char*>(value.get()));
}

std::string required(xmlNode* node, const char* name) {
    auto value = attribute(node, name);
    require(value && !value->empty(), "missing XML attribute");
    return *value;
}

bool named(xmlNode* node, const char* name) {
    return node->type == XML_ELEMENT_NODE && xmlStrEqual(node->name, BAD_CAST name);
}

std::int64_t integer(std::string_view value) {
    std::int64_t result = 0;
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result);
    require(!value.empty() && value.front() >= '0' && value.front() <= '9'
                && parsed.ec == std::errc{} && parsed.ptr == value.data() + value.size() && result >= 0,
            "invalid XML integer");
    return result;
}

int small_integer(std::string_view value) {
    const auto number = integer(value);
    require(number <= std::numeric_limits<int>::max(), "XML integer overflow");
    return static_cast<int>(number);
}

std::chrono::milliseconds clock_part(std::string_view value) {
    while (!value.empty() && value.front() == ' ') value.remove_prefix(1);
    while (!value.empty() && value.back() == ' ') value.remove_suffix(1);
    std::vector<std::int64_t> parts;
    while (true) {
        const auto colon = value.find(':');
        parts.push_back(integer(value.substr(0, colon)));
        require(parts.size() <= 3, "invalid match clock");
        if (colon == std::string_view::npos) break;
        value.remove_prefix(colon + 1);
    }
    require(parts.size() >= 2 || parts.front() == 0, "invalid match clock");
    require(parts.back() < 60 && (parts.size() != 3 || parts[1] < 60), "invalid match clock");
    std::int64_t seconds = 0;
    for (const auto part : parts) {
        require(part <= 24 * 60 * 60, "match clock too large");
        seconds = seconds * 60 + part;
    }
    require(seconds <= 24 * 60 * 60, "match clock too large");
    return std::chrono::milliseconds(seconds * 1000);
}

std::chrono::milliseconds match_clock(std::string_view value) {
    const auto plus = value.find('+');
    const auto base = clock_part(value.substr(0, plus));
    return plus == std::string_view::npos ? base : base + clock_part(value.substr(plus + 1));
}

std::array<int, 2> pair(xmlNode* node) {
    return {small_integer(required(node, "t1")), small_integer(required(node, "t2"))};
}

SportsEvent parse_event(xmlNode* node, Time received_at) {
    SportsEvent result;
    result.id = required(node, "uuid");
    result.type = small_integer(required(node, "type"));
    result.source_time_ms = integer(required(node, "stime"));
    result.clock = match_clock(required(node, "mtime"));
    const auto side = required(node, "side");
    require(side == "home" || side == "away" || side == "none", "invalid XML event side");
    result.side = side == "home" ? Side::Home : side == "away" ? Side::Away : Side::None;
    result.period = attribute(node, "matchstatus").value_or("");
    result.received_at = received_at;
    for (auto field = node->properties; field; field = field->next) {
        const auto* name = reinterpret_cast<const char*>(field->name);
        // Numeric IDs can change on reconnect; UUID is the stable identity.
        if (std::string_view(name) != "id") result.attributes.emplace(name, *attribute(node, name));
    }
    return result;
}

SportsUpdate parse_match(xmlNode* root, Time received_at) {
    SportsUpdate update;
    update.match_id = required(root, "matchid");
    update.received_at = received_at;
    const auto feed = required(root, "feedtype");
    require(feed == "full" || feed == "full_paginated" || feed == "partial" || feed == "delta" || feed == "deltaupdate",
            "unsupported sports feed type");
    update.kind = feed == "delta" ? FeedKind::Delta : feed == "deltaupdate" ? FeedKind::Correction : FeedKind::Full;
    update.state.home_team = attribute(root, "t1id");
    update.state.away_team = attribute(root, "t2id");
    if (auto clock = attribute(root, "matchtime")) update.state.clock = match_clock(*clock);
    if (auto connected = attribute(root, "connectionstatus")) {
        require(*connected == "0" || *connected == "1", "invalid connection status");
        update.state.source_connected = *connected == "1";
    }
    for (auto child = root->children; child; child = child->next) {
        if (named(child, "status")) update.state.period = required(child, "name");
        if (named(child, "score") && attribute(child, "type") == "current") update.state.score = pair(child);
        if (named(child, "red")) update.state.red_cards = pair(child);
        if (named(child, "events")) {
            for (auto event = child->children; event; event = event->next) {
                if (named(event, "event")) update.events.push_back(parse_event(event, received_at));
            }
        }
    }
    return update;
}

}  // namespace

XmlFrame parse_sports_xml(std::string_view xml, Time received_at) {
    require(!xml.empty() && xml.size() <= max_feed_bytes, "sports XML frame size limit exceeded");
    require(xml.find("<!DOCTYPE") == std::string_view::npos && xml.find("<!ENTITY") == std::string_view::npos,
            "DTD and entities are forbidden in sports XML");
    std::unique_ptr<xmlDoc, decltype(&xmlFreeDoc)> doc(
        xmlReadMemory(xml.data(), static_cast<int>(xml.size()), nullptr, nullptr,
                      XML_PARSE_NONET | XML_PARSE_NOBLANKS | XML_PARSE_NOERROR | XML_PARSE_NOWARNING), xmlFreeDoc);
    require(doc && !doc->intSubset && !doc->extSubset, "invalid sports XML");
    auto root = xmlDocGetRootElement(doc.get());
    require(root && !root->ns, "invalid sports XML root");
    XmlFrame result;
    if (named(root, "ct")) result.message = FeedAlive{};
    else if (named(root, "login")) result.message = FeedLogin{required(root, "result") == "valid"};
    else if (named(root, "matchstop")) result.message = FeedStopped{required(root, "matchid")};
    else if (named(root, "matchdata")) {
        SportsUpdate update;
        update.match_id = required(root, "matchid");
        update.state.clock = match_clock(required(root, "mtime"));
        update.received_at = received_at;
        result.message = std::move(update);
    } else if (named(root, "match")) {
        result.message = parse_match(root, received_at);
        const auto feed = attribute(root, "feedtype");
        if (feed == "full_paginated" || feed == "partial") {
            result.snapshot_id = required(root, "uuid");
            result.page = small_integer(required(root, "page"));
            result.total_pages = small_integer(required(root, "totalpages"));
            require(result.total_pages > 0 && result.total_pages <= 256
                        && result.page < result.total_pages, "invalid snapshot pagination");
        }
    } else if (attribute(root, "result") == "invalid") {
        throw std::runtime_error("sports source rejected a request");
    }
    return result;
}

}  // namespace match_signals::detail
