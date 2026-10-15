#include "sports_xml.hpp"
#include "checked.hpp"

#include <iterator>

namespace match_signals {
namespace {

template<class Value>
void merge(std::optional<Value>& previous, const std::optional<Value>& next) {
    detail::require(!previous || !next || previous == next, "snapshot metadata changed between pages");
    if (next) previous = next;
}

void merge_patch(MatchPatch& a, const MatchPatch& b) {
    merge(a.home_team, b.home_team);
    merge(a.away_team, b.away_team);
    merge(a.period, b.period);
    merge(a.clock, b.clock);
    merge(a.score, b.score);
    merge(a.red_cards, b.red_cards);
    merge(a.source_connected, b.source_connected);
}

}  // namespace

void SportsXmlDecoder::reset() {
    buffer_.clear();
    snapshots_.clear();
    pending_bytes_ = 0;
}

std::vector<FeedMessage> SportsXmlDecoder::push(std::string_view bytes, Time received_at) {
    try {
        detail::require(bytes.size() <= detail::max_feed_bytes
                            && buffer_.size() <= detail::max_feed_bytes - bytes.size(),
                        "sports frame buffer limit exceeded");
        buffer_.append(bytes);
        std::vector<FeedMessage> messages;
        std::size_t consumed = 0;
        while (true) {
            const auto end = buffer_.find(">\r\n\r\n", consumed);
            if (end == std::string::npos) break;
            const auto size = end - consumed + 1;
            auto frame = detail::parse_sports_xml(std::string_view(buffer_).substr(consumed, size), received_at);
            consumed = end + 5;
            if (!frame.message) continue;
            auto update = std::get_if<SportsUpdate>(&*frame.message);
            if (!update) {
                messages.push_back(std::move(*frame.message));
                continue;
            }
            if (frame.total_pages == 0) {
                detail::require(snapshots_.count(update->match_id) == 0,
                                "delta interrupted a paginated snapshot");
                messages.push_back(std::move(*frame.message));
                continue;
            }
            const auto match_id = update->match_id;
            detail::require(pending_bytes_ <= detail::max_feed_bytes - size, "snapshot size limit exceeded");
            pending_bytes_ += size;
            if (frame.page == 0) {
                detail::require(snapshots_.count(update->match_id) == 0, "overlapping match snapshots");
                snapshots_.emplace(match_id, Snapshot{std::move(*update), frame.snapshot_id, 1, frame.total_pages, size});
            } else {
                auto found = snapshots_.find(update->match_id);
                detail::require(found != snapshots_.end(), "missing first snapshot page");
                auto& snapshot = found->second;
                detail::require(snapshot.id == frame.snapshot_id && snapshot.next_page == frame.page
                                    && snapshot.total_pages == frame.total_pages, "snapshot pages out of order");
                merge_patch(snapshot.update.state, update->state);
                auto& events = snapshot.update.events;
                events.insert(events.end(), std::make_move_iterator(update->events.begin()),
                              std::make_move_iterator(update->events.end()));
                snapshot.update.received_at = received_at;
                snapshot.bytes += size;
                ++snapshot.next_page;
            }
            auto complete = snapshots_.find(match_id);
            if (complete->second.next_page == complete->second.total_pages) {
                pending_bytes_ -= complete->second.bytes;
                messages.emplace_back(std::move(complete->second.update));
                snapshots_.erase(complete);
            }
        }
        buffer_.erase(0, consumed);
        return messages;
    } catch (...) {
        reset();
        throw;
    }
}

}  // namespace match_signals
