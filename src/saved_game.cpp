#include "cardputer_chess/saved_game.hpp"

#include <algorithm>

namespace cardputer_chess {
namespace {

constexpr std::array<std::uint8_t, 4> kMagic = {{'C', 'P', 'C', 'H'}};
constexpr std::size_t kHeaderBytes = 20;
constexpr std::size_t kChecksumBytes = 4;

void writeU16(std::uint8_t* output, std::uint16_t value) {
    output[0] = static_cast<std::uint8_t>(value & 0xFFU);
    output[1] = static_cast<std::uint8_t>(value >> 8U);
}

void writeU32(std::uint8_t* output, std::uint32_t value) {
    for (int byte = 0; byte < 4; ++byte) {
        output[byte] = static_cast<std::uint8_t>(value >> (byte * 8));
    }
}

void writeU64(std::uint8_t* output, std::uint64_t value) {
    for (int byte = 0; byte < 8; ++byte) {
        output[byte] = static_cast<std::uint8_t>(value >> (byte * 8));
    }
}

std::uint16_t readU16(const std::uint8_t* input) {
    return static_cast<std::uint16_t>(input[0]) |
           static_cast<std::uint16_t>(static_cast<std::uint16_t>(input[1]) << 8U);
}

std::uint32_t readU32(const std::uint8_t* input) {
    std::uint32_t value = 0;
    for (int byte = 0; byte < 4; ++byte) {
        value |= static_cast<std::uint32_t>(input[byte]) << (byte * 8);
    }
    return value;
}

std::uint64_t readU64(const std::uint8_t* input) {
    std::uint64_t value = 0;
    for (int byte = 0; byte < 8; ++byte) {
        value |= static_cast<std::uint64_t>(input[byte]) << (byte * 8);
    }
    return value;
}

std::uint32_t checksum(const std::uint8_t* data, std::size_t size) {
    std::uint32_t crc = UINT32_C(0xFFFFFFFF);
    for (std::size_t index = 0; index < size; ++index) {
        crc ^= data[index];
        for (int bit = 0; bit < 8; ++bit) {
            const std::uint32_t mask =
                static_cast<std::uint32_t>(-
                    static_cast<std::int32_t>(crc & UINT32_C(1)));
            crc = (crc >> 1U) ^ (UINT32_C(0xEDB88320) & mask);
        }
    }
    return ~crc;
}

bool validPromotion(PieceType promotion) {
    return promotion == PieceType::None || promotion == PieceType::Knight ||
           promotion == PieceType::Bishop || promotion == PieceType::Rook ||
           promotion == PieceType::Queen;
}

}  // namespace

std::uint16_t encodeSavedMove(const Move& move) {
    return static_cast<std::uint16_t>(move.from & 0x3FU) |
           static_cast<std::uint16_t>((move.to & 0x3FU) << 6U) |
           static_cast<std::uint16_t>(
               (static_cast<std::uint8_t>(move.promotion) & 0x07U) << 12U);
}

bool resolveSavedMove(Position& position, std::uint16_t encodedMove, Move& move) {
    const std::uint8_t from = static_cast<std::uint8_t>(encodedMove & 0x3FU);
    const std::uint8_t to = static_cast<std::uint8_t>((encodedMove >> 6U) & 0x3FU);
    const PieceType promotion =
        static_cast<PieceType>((encodedMove >> 12U) & 0x07U);
    if (from == to || !validPromotion(promotion)) return false;

    MoveList legalMoves;
    position.generateLegalMoves(legalMoves);
    for (std::uint16_t index = 0; index < legalMoves.size; ++index) {
        const Move candidate = legalMoves[index];
        if (candidate.from == from && candidate.to == to &&
            candidate.promotion == promotion) {
            move = candidate;
            return true;
        }
    }
    return false;
}

std::size_t serializeSavedGame(const SavedGame& savedGame, std::uint8_t* output,
                               std::size_t outputSize) {
    if (output == nullptr || savedGame.moveCount > savedGame.moves.size() ||
        (savedGame.humanColor != Color::White &&
         savedGame.humanColor != Color::Black)) {
        return 0;
    }
    const std::size_t size =
        kHeaderBytes + static_cast<std::size_t>(savedGame.moveCount) * 2U +
        kChecksumBytes;
    if (outputSize < size) return 0;

    std::copy(kMagic.begin(), kMagic.end(), output);
    output[4] = kSavedGameFormatVersion;
    output[5] = static_cast<std::uint8_t>(savedGame.humanColor);
    writeU16(output + 6, savedGame.moveCount);
    writeU32(output + 8, savedGame.generation);
    writeU64(output + 12, savedGame.gameSeed);
    for (std::uint16_t index = 0; index < savedGame.moveCount; ++index) {
        writeU16(output + kHeaderBytes + static_cast<std::size_t>(index) * 2U,
                 savedGame.moves[index]);
    }
    writeU32(output + size - kChecksumBytes, checksum(output, size - kChecksumBytes));
    return size;
}

bool deserializeSavedGame(const std::uint8_t* input, std::size_t inputSize,
                          SavedGame& savedGame) {
    if (input == nullptr || inputSize < kHeaderBytes + kChecksumBytes ||
        !std::equal(kMagic.begin(), kMagic.end(), input) ||
        input[4] != kSavedGameFormatVersion || input[5] > 1U) {
        return false;
    }
    const std::uint16_t moveCount = readU16(input + 6);
    if (moveCount > kMaxGamePly) return false;
    const std::size_t expectedSize =
        kHeaderBytes + static_cast<std::size_t>(moveCount) * 2U + kChecksumBytes;
    if (inputSize != expectedSize ||
        readU32(input + inputSize - kChecksumBytes) !=
            checksum(input, inputSize - kChecksumBytes)) {
        return false;
    }

    SavedGame decoded;
    decoded.humanColor = static_cast<Color>(input[5]);
    decoded.moveCount = moveCount;
    decoded.generation = readU32(input + 8);
    decoded.gameSeed = readU64(input + 12);
    for (std::uint16_t index = 0; index < moveCount; ++index) {
        decoded.moves[index] =
            readU16(input + kHeaderBytes + static_cast<std::size_t>(index) * 2U);
    }
    savedGame = decoded;
    return true;
}

bool savedGameGenerationNewer(std::uint32_t candidate, std::uint32_t current) {
    return static_cast<std::int32_t>(candidate - current) > 0;
}

}  // namespace cardputer_chess
