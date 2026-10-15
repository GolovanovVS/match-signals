#pragma once

#include <match_signals/clock.hpp>

#include <curl/curl.h>

#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace match_signals::detail {

void check_curl(CURLcode result);

class CurlHandle {
public:
    CurlHandle();
    CURL* get() const noexcept { return handle_.get(); }

    template<class Value>
    void set(CURLoption option, Value value) { check_curl(curl_easy_setopt(get(), option, value)); }

private:
    std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> handle_{nullptr, curl_easy_cleanup};
};

std::string http_get(CurlHandle& curl, const std::string& url);
void tls_connect(CurlHandle& curl, const std::string& url);
void tls_send(CurlHandle& curl, std::string_view bytes);
std::optional<std::string> tls_receive(CurlHandle& curl, std::chrono::milliseconds wait);

}  // namespace match_signals::detail
