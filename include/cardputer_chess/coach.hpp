#pragma once

#include "cardputer_chess/engine.hpp"

#include <cstdint>

namespace cardputer_chess {

enum class CoachMode : std::uint8_t { Off, OnDemand, Always };

enum class MoveQuality : std::uint8_t {
    Unavailable,
    Best,
    Excellent,
    Good,
    Inaccuracy,
    OutsideTopThree,
};

struct CoachFeedback {
    MoveQuality quality = MoveQuality::Unavailable;
    std::uint8_t rank = 0;
    std::int16_t bestScoreCp = 0;
    std::int16_t moveScoreCp = 0;
    std::int16_t lossCp = 0;
};

const char* coachModeName(CoachMode mode);
const char* moveQualityName(MoveQuality quality);
CoachFeedback classifyCoachMove(const SearchResult& analysis, const Move& move);

}  // namespace cardputer_chess
