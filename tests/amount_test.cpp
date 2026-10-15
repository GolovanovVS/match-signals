#include <match_signals/amount.hpp>

#include "check.hpp"

#include <limits>

using namespace match_signals;

int main() {
    CHECK(decimal("0") == 0);
    CHECK(decimal("12.03450000") == 12'034'500);
    CHECK(decimal_string(decimal("12.034500")) == "12.0345");
    CHECK(decimal_string(1) == "0.000001");
    CHECK(decimal("9223372036854.775807") == std::numeric_limits<Units>::max());
    for (auto text : {"", ".5", "1.", "-1", "nan", "1e2", "1.0000001", "9223372036854.775808"}) {
        rejects<std::invalid_argument>([&] { decimal(text); });
    }
    CHECK(notional(decimal("0.333333"), decimal("3"), true) == 999'999);
    CHECK(notional(1, 1, false) == 0);
    CHECK(notional(1, 1, true) == 1);
    CHECK(notional(scale, std::numeric_limits<Units>::max(), true) == std::numeric_limits<Units>::max());
    rejects<std::invalid_argument>([] { notional(scale + 1, 1, false); });
}
