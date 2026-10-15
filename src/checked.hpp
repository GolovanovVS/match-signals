#pragma once

#include <match_signals/amount.hpp>
#include <limits>
#include <stdexcept>

namespace match_signals::detail {

inline void require(bool valid, const char* message) {
    if (!valid) throw std::invalid_argument(message);
}

inline Units add(Units left, Units right) {
    require(left >= 0 && right >= 0 && left <= std::numeric_limits<Units>::max() - right,
            "amount overflow");
    return left + right;
}

inline Units multiply(Units left, Units right) {
    require(left >= 0 && right >= 0 && (right == 0 || left <= std::numeric_limits<Units>::max() / right),
            "amount overflow");
    return left * right;
}

}  // namespace match_signals::detail
