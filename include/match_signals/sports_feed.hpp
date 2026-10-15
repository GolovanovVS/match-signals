#pragma once

#include <match_signals/sports.hpp>

#include <memory>
#include <string_view>
#include <variant>

namespace match_signals {

struct FeedAlive {};
struct FeedLogin { bool accepted = false; };
struct FeedStopped { std::string match_id; };
using FeedMessage = std::variant<SportsUpdate, FeedAlive, FeedLogin, FeedStopped>;

class SportsXmlDecoder {
public:
    std::vector<FeedMessage> push(std::string_view bytes, Time received_at);
    void reset();

private:
    struct Snapshot {
        SportsUpdate update;
        std::string id;
        int next_page = 0;
        int total_pages = 0;
        std::size_t bytes = 0;
    };
    std::string buffer_;
    std::map<std::string, Snapshot> snapshots_;
    std::size_t pending_bytes_ = 0;
};

struct SportsFeedConfig {
    std::string url = "https://livedata.betradar.com:2017";
    std::string login_xml;
    std::vector<std::string> match_ids;
};

class SportsFeedClient {
public:
    explicit SportsFeedClient(SportsFeedConfig config);
    ~SportsFeedClient();
    SportsFeedClient(const SportsFeedClient&) = delete;
    SportsFeedClient& operator=(const SportsFeedClient&) = delete;

    void connect();
    std::optional<SportsUpdate> poll(std::chrono::milliseconds wait);
    void close() noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace match_signals
