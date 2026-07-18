#include "cardputer_chess/coach.hpp"

#include <algorithm>
#include <limits>

namespace cardputer_chess {

const char* coachModeName(CoachMode mode) {
    switch (mode) {
        case CoachMode::Off: return "Off";
        case CoachMode::OnDemand: return "On demand";
        case CoachMode::Always: return "Always";
    }
    return "Off";
}

const char* moveQualityName(MoveQuality quality) {
    switch (quality) {
        case MoveQuality::Unavailable: return "No analysis";
        case MoveQuality::Best: return "Best move";
        case MoveQuality::Excellent: return "Excellent";
        case MoveQuality::Good: return "Good";
        case MoveQuality::Inaccuracy: return "Inaccuracy";
        case MoveQuality::OutsideTopThree: return "Outside top 3";
    }
    return "No analysis";
}

CoachFeedback classifyCoachMove(const SearchResult& analysis, const Move& move) {
    CoachFeedback feedback;
    if (!analysis.hasMove || analysis.lineCount == 0) return feedback;

    feedback.bestScoreCp = analysis.lines[0].scoreCp;
    const std::uint8_t lineCount = std::min<std::uint8_t>(analysis.lineCount,
                                                          kMaxAnalysisLines);
    for (std::uint8_t index = 0; index < lineCount; ++index) {
        if (!(analysis.lines[index].bestMove == move)) continue;
        feedback.rank = static_cast<std::uint8_t>(index + 1U);
        feedback.moveScoreCp = analysis.lines[index].scoreCp;
        const int loss = static_cast<int>(feedback.bestScoreCp) -
                         static_cast<int>(feedback.moveScoreCp);
        feedback.lossCp = static_cast<std::int16_t>(std::clamp<int>(
            loss, 0, std::numeric_limits<std::int16_t>::max()));
        if (feedback.lossCp <= 15) {
            feedback.quality = MoveQuality::Best;
        } else if (feedback.lossCp <= 40) {
            feedback.quality = MoveQuality::Excellent;
        } else if (feedback.lossCp <= 90) {
            feedback.quality = MoveQuality::Good;
        } else {
            feedback.quality = MoveQuality::Inaccuracy;
        }
        return feedback;
    }

    feedback.quality = MoveQuality::OutsideTopThree;
    return feedback;
}

}  // namespace cardputer_chess
