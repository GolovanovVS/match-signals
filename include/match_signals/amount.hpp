#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace match_signals {

// Cash, prices and share quantities use six decimal places, never floating point.
using Units = std::int64_t;
inline constexpr Units scale = 1'000'000;
Units decimal(std::string_view value);
std::string decimal_string(Units value);
Units notional(Units price, Units quantity, bool round_up);

}  // namespace match_signals
