#pragma once

#include <stdexcept>
#include <string>

#define CHECK(expression) do { \
    if (!(expression)) throw std::runtime_error( \
        std::string(__FILE__) + ':' + std::to_string(__LINE__) + ": " #expression); \
} while (false)

template<class Error = std::exception, class Function>
void rejects(Function&& function) {
    bool caught = false;
    try { function(); } catch (const Error&) { caught = true; }
    CHECK(caught);
}
