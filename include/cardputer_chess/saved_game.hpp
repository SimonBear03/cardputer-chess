#pragma once

#include "cardputer_chess/chess.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace cardputer_chess {

constexpr std::uint8_t kSavedGameFormatVersion = 1;
constexpr std::size_t kSavedGameMaxBytes = 24U + 2U * kMaxGamePly;

struct SavedGame {
    std::uint32_t generation = 0;
    std::uint64_t gameSeed = 0;
    Color humanColor = Color::White;
    std::uint16_t moveCount = 0;
    std::array<std::uint16_t, kMaxGamePly> moves{};
};

std::uint16_t encodeSavedMove(const Move& move);
bool resolveSavedMove(Position& position, std::uint16_t encodedMove, Move& move);

std::size_t serializeSavedGame(const SavedGame& savedGame, std::uint8_t* output,
                               std::size_t outputSize);
bool deserializeSavedGame(const std::uint8_t* input, std::size_t inputSize,
                          SavedGame& savedGame);
bool savedGameGenerationNewer(std::uint32_t candidate, std::uint32_t current);

}  // namespace cardputer_chess
