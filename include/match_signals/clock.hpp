#pragma once

#include <chrono>
#include <functional>

namespace match_signals {

using Time = std::chrono::steady_clock::time_point;
using Now = std::function<Time()>;

}  // namespace match_signals
