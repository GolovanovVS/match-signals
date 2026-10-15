#include <match_signals/sports_feed.hpp>

#include "checked.hpp"
#include "curl.hpp"

#include <algorithm>
#include <deque>
#include <unordered_set>

namespace match_signals {
namespace {

bool numeric_id(const std::string& value) {
    return !value.empty() && std::all_of(value.begin(), value.end(), [](char c) { return c >= '0' && c <= '9'; });
}

}  // namespace

struct SportsFeedClient::Impl {
    SportsFeedConfig config;
    std::unique_ptr<detail::CurlHandle> curl;
    SportsXmlDecoder decoder;
    std::deque<SportsUpdate> updates;
    Time last_received{}, last_sent{};
    bool authenticated = false;

    void send(const std::string& xml) {
        detail::tls_send(*curl, xml + '\n');
        last_sent = std::chrono::steady_clock::now();
    }

    void receive(std::chrono::milliseconds wait) {
        if (auto bytes = detail::tls_receive(*curl, wait)) {
            const auto now = std::chrono::steady_clock::now();
            last_received = now;
            for (auto& message : decoder.push(*bytes, now)) {
                if (auto login = std::get_if<FeedLogin>(&message)) {
                    if (!login->accepted || authenticated) throw std::runtime_error("sports login rejected or repeated");
                    authenticated = true;
                } else if (auto update = std::get_if<SportsUpdate>(&message)) {
                    if (!authenticated) throw std::runtime_error("sports update arrived before login");
                    detail::require(std::find(config.match_ids.begin(), config.match_ids.end(), update->match_id)
                                        != config.match_ids.end(), "unsubscribed sports match");
                    updates.push_back(std::move(*update));
                } else if (std::holds_alternative<FeedStopped>(message)) {
                    throw std::runtime_error("sports match subscription stopped");
                }
            }
        }
        const auto now = std::chrono::steady_clock::now();
        if (now - last_received > std::chrono::seconds(20)) throw std::runtime_error("sports heartbeat timeout");
        if (authenticated && now - last_sent >= std::chrono::seconds(30)) send("<ct/>");
    }
};

SportsFeedClient::SportsFeedClient(SportsFeedConfig config) : impl_(std::make_unique<Impl>()) {
    detail::require(!config.login_xml.empty() && config.login_xml.size() <= 64 * 1024
                        && config.login_xml.find("\r\n\r\n") == std::string::npos, "invalid login XML");
    detail::require(!config.match_ids.empty() && config.match_ids.size() < 50, "subscribe to 1..49 booked matches");
    std::unordered_set<std::string> ids;
    for (const auto& id : config.match_ids) {
        detail::require(numeric_id(id) && ids.insert(id).second, "invalid or duplicate match ID");
    }
    impl_->config = std::move(config);
}

SportsFeedClient::~SportsFeedClient() = default;

void SportsFeedClient::connect() {
    close();
    try {
        impl_->curl = std::make_unique<detail::CurlHandle>();
        detail::tls_connect(*impl_->curl, impl_->config.url);
        impl_->last_received = std::chrono::steady_clock::now();
        impl_->send(impl_->config.login_xml);
        const auto deadline = impl_->last_received + std::chrono::seconds(10);
        while (!impl_->authenticated) {
            if (std::chrono::steady_clock::now() >= deadline) throw std::runtime_error("sports login timeout");
            impl_->receive(std::chrono::milliseconds(100));
        }
        std::string subscription = "<matchsubscription>";
        for (const auto& id : impl_->config.match_ids) subscription += "<match matchid=\"" + id + "\" feedtype=\"delta\"/>";
        impl_->send(subscription + "</matchsubscription>");
    } catch (...) {
        close();
        throw;
    }
}

std::optional<SportsUpdate> SportsFeedClient::poll(std::chrono::milliseconds wait) {
    if (!impl_->curl) throw std::logic_error("connect the sports client before polling");
    try {
        if (impl_->updates.empty()) impl_->receive(wait);
        if (impl_->updates.empty()) return std::nullopt;
        auto update = std::move(impl_->updates.front());
        impl_->updates.pop_front();
        return update;
    } catch (...) {
        close();
        throw;
    }
}

void SportsFeedClient::close() noexcept {
    impl_->curl.reset();
    impl_->decoder.reset();
    impl_->updates.clear();
    impl_->authenticated = false;
}

}  // namespace match_signals
