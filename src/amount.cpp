#include <match_signals/amount.hpp>

#include "checked.hpp"

namespace match_signals {
using detail::require;
using detail::add;
using detail::multiply;

Units decimal(std::string_view text) {
    require(!text.empty(), "empty decimal");
    Units whole = 0;
    Units fraction = 0;
    int digits = 0;
    bool dot = false;
    bool whole_digit = false;
    for (char c : text) {
        if (c == '.' && !dot && whole_digit) { dot = true; continue; }
        require(c >= '0' && c <= '9', "invalid decimal");
        if (!dot) {
            whole = add(multiply(whole, 10), c - '0');
            whole_digit = true;
        } else {
            if (digits < 6) {
                fraction = fraction * 10 + c - '0';
                ++digits;
            } else {
                require(c == '0', "decimal exceeds six-place precision");
            }
        }
    }
    require(!dot || digits > 0, "missing fractional digits");
    while (digits++ < 6) fraction *= 10;
    return add(multiply(whole, scale), fraction);
}

std::string decimal_string(Units value) {
    require(value >= 0, "negative amount");
    std::string result = std::to_string(value / scale);
    if (value % scale == 0) return result;
    auto fraction = std::to_string(scale + value % scale).substr(1);
    while (fraction.back() == '0') fraction.pop_back();
    return result + '.' + fraction;
}

Units notional(Units price, Units quantity, bool round_up) {
    require(price >= 0 && price <= scale && quantity >= 0, "invalid notional arguments");
    const Units tail = (quantity % scale) * price;
    return add(multiply(quantity / scale, price), tail / scale + (round_up && tail % scale != 0));
}

}  // namespace match_signals
