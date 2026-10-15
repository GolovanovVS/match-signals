#include "curl.hpp"
#include "checked.hpp"

#include <poll.h>

#include <algorithm>
#include <cerrno>
#include <limits>

namespace match_signals::detail {
namespace {

constexpr std::size_t max_http_bytes = 8 * 1024 * 1024;

std::size_t append_body(char* data, std::size_t size, std::size_t count, void* output) noexcept {
    if (size != 0 && count > max_http_bytes / size) return 0;
    const auto length = size * count;
    auto& body = *static_cast<std::string*>(output);
    if (body.size() > max_http_bytes - length) return 0;
    try { body.append(data, length); } catch (...) { return 0; }
    return length;
}

bool wait_socket(CurlHandle& curl, short events, Time deadline) {
    curl_socket_t socket = CURL_SOCKET_BAD;
    check_curl(curl_easy_getinfo(curl.get(), CURLINFO_ACTIVESOCKET, &socket));
    if (socket == CURL_SOCKET_BAD) throw std::runtime_error("TLS socket is closed");
    while (true) {
        const auto remaining = std::chrono::ceil<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now());
        if (remaining.count() < 0) return false;
        pollfd descriptor{socket, events, 0};
        const int status = ::poll(&descriptor, 1, static_cast<int>(std::min<std::int64_t>(remaining.count(), 10'000)));
        if (status < 0 && errno == EINTR) continue;
        if (status < 0) throw std::runtime_error("TLS socket poll failed");
        if (status == 0) return false;
        if (descriptor.revents & (POLLERR | POLLNVAL)) throw std::runtime_error("TLS socket failed");
        return true;
    }
}

}  // namespace

void check_curl(CURLcode result) {
    if (result != CURLE_OK) throw std::runtime_error(std::string("curl: ") + curl_easy_strerror(result));
}

CurlHandle::CurlHandle() {
    static const auto initialized = curl_global_init(CURL_GLOBAL_DEFAULT);
    check_curl(initialized);
    handle_.reset(curl_easy_init());
    if (!handle_) throw std::runtime_error("curl initialization failed");
    set(CURLOPT_NOSIGNAL, 1L);
    set(CURLOPT_CONNECTTIMEOUT_MS, 5'000L);
    set(CURLOPT_TIMEOUT_MS, 10'000L);
    set(CURLOPT_SSL_VERIFYPEER, 1L);
    set(CURLOPT_SSL_VERIFYHOST, 2L);
    set(CURLOPT_FOLLOWLOCATION, 0L);
    set(CURLOPT_PROTOCOLS, static_cast<long>(CURLPROTO_HTTPS));
}

std::string http_get(CurlHandle& curl, const std::string& url) {
    require(url.rfind("https://", 0) == 0, "HTTPS is required");
    std::string body;
    curl.set(CURLOPT_URL, url.c_str());
    curl.set(CURLOPT_WRITEFUNCTION, &append_body);
    curl.set(CURLOPT_WRITEDATA, &body);
    const auto status = curl_easy_perform(curl.get());
    curl.set(CURLOPT_WRITEDATA, nullptr);
    check_curl(status);
    long response = 0;
    check_curl(curl_easy_getinfo(curl.get(), CURLINFO_RESPONSE_CODE, &response));
    if (response != 200) throw std::runtime_error("market data HTTP status " + std::to_string(response));
    return body;
}

void tls_connect(CurlHandle& curl, const std::string& url) {
    require(url.rfind("https://", 0) == 0, "TLS is required");
    curl.set(CURLOPT_URL, url.c_str());
    curl.set(CURLOPT_CONNECT_ONLY, 1L);
    check_curl(curl_easy_perform(curl.get()));
}

void tls_send(CurlHandle& curl, std::string_view bytes) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!bytes.empty()) {
        std::size_t sent = 0;
        const auto status = curl_easy_send(curl.get(), bytes.data(), bytes.size(), &sent);
        if (status == CURLE_AGAIN) {
            if (!wait_socket(curl, POLLOUT, deadline)) throw std::runtime_error("sports send timeout");
            continue;
        }
        check_curl(status);
        if (sent == 0) throw std::runtime_error("sports socket closed while sending");
        bytes.remove_prefix(sent);
    }
}

std::optional<std::string> tls_receive(CurlHandle& curl, std::chrono::milliseconds wait) {
    require(wait.count() >= 0 && wait <= std::chrono::seconds(10), "invalid sports poll wait");
    const auto deadline = std::chrono::steady_clock::now() + wait;
    while (true) {
        char bytes[16 * 1024];
        std::size_t received = 0;
        const auto status = curl_easy_recv(curl.get(), bytes, sizeof(bytes), &received);
        if (status == CURLE_AGAIN) {
            if (!wait_socket(curl, POLLIN, deadline)) return std::nullopt;
            continue;
        }
        check_curl(status);
        if (received == 0) throw std::runtime_error("sports source disconnected");
        return std::string(bytes, received);
    }
}

}  // namespace match_signals::detail
