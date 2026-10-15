#include <match_signals/models.hpp>

#include <stdexcept>

namespace match_signals {

double ProbabilityWin::yes_probability(Target target) const {
    switch (target) {
        case Target::HomeWin: return home_win;
        case Target::Draw: return draw;
        case Target::AwayWin: return away_win;
    }
    throw std::invalid_argument("invalid market target");
}

}  // namespace match_signals
