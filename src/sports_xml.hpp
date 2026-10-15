#pragma once

#include <match_signals/sports_feed.hpp>

namespace match_signals::detail {

inline constexpr std::size_t max_feed_bytes = 8 * 1024 * 1024;

struct XmlFrame {
    std::optional<FeedMessage> message;
    std::string snapshot_id;
    int page = 0;
    int total_pages = 0;
};

XmlFrame parse_sports_xml(std::string_view xml, Time received_at);

}  // namespace match_signals::detail
