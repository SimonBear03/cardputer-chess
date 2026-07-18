#include "cardputer_chess/engine.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <limits>
#include <string_view>

#ifdef ARDUINO
#include <Arduino.h>
#endif

namespace cardputer_chess {
namespace {

constexpr int kInfinity = 32000;
constexpr int kMateScore = 30000;
constexpr int kMateThreshold = 29000;
constexpr std::uint8_t kBoundExact = 1;
constexpr std::uint8_t kBoundLower = 2;
constexpr std::uint8_t kBoundUpper = 3;

constexpr std::array<int, 7> kPieceValue = {0, 100, 320, 330, 500, 900, 20000};
constexpr std::array<int, 7> kEndgameValue = {0, 120, 305, 325, 520, 900, 20000};
constexpr std::array<int, 7> kPhaseWeight = {0, 0, 1, 1, 2, 4, 0};

constexpr std::array<LevelConfig, kLevelCount> kLevels = {{
    {"Beginner", 30, 3, 420, 6},
    {"Easy", 80, 4, 280, 5},
    {"Casual", 180, 5, 170, 4},
    {"Club", 400, 7, 90, 3},
    {"Strong", 900, 9, 45, 2},
    {"Expert", 2000, 12, 20, 2},
    {"Master", 4500, 16, 0, 1},
    {"Maximum", 10000, 24, 0, 1},
}};

int colorIndex(Color color) { return color == Color::White ? 0 : 1; }
int fileOf(int square) { return square & 7; }
int rankOf(int square) { return square >> 3; }

std::uint64_t nextRandom(std::uint64_t& state) {
    std::uint64_t value = (state += 0x9E3779B97F4A7C15ULL);
    value = (value ^ (value >> 30U)) * 0xBF58476D1CE4E5B9ULL;
    value = (value ^ (value >> 27U)) * 0x94D049BB133111EBULL;
    return value ^ (value >> 31U);
}

std::uint32_t encodeMove(const Move& move) {
    return static_cast<std::uint32_t>(move.from) |
           (static_cast<std::uint32_t>(move.to) << 6U) |
           (static_cast<std::uint32_t>(move.promotion) << 12U) |
           (static_cast<std::uint32_t>(move.flags) << 16U);
}

Move decodeMove(std::uint32_t value) {
    Move move;
    move.from = static_cast<std::uint8_t>(value & 0x3FU);
    move.to = static_cast<std::uint8_t>((value >> 6U) & 0x3FU);
    move.promotion = static_cast<PieceType>((value >> 12U) & 0x0FU);
    move.flags = static_cast<std::uint8_t>((value >> 16U) & 0xFFU);
    return move;
}

bool isQuiet(const Move& move) {
    return !move.isCapture() && !move.isPromotion() && !move.isCastle();
}

int clampScore(int score) {
    return std::max<int>(std::numeric_limits<std::int16_t>::min(),
                         std::min<int>(std::numeric_limits<std::int16_t>::max(), score));
}

}  // namespace

struct Engine::TTEntry {
    std::uint64_t key = 0;
    std::uint32_t move = 0;
    std::int16_t score = 0;
    std::int8_t depth = -1;
    std::uint8_t boundAndAge = 0;
};

const LevelConfig& levelConfig(int levelIndex) {
    const int clamped = std::max(0, std::min(kLevelCount - 1, levelIndex));
    return kLevels[static_cast<std::size_t>(clamped)];
}

Engine::Engine(std::size_t hashKilobytes) {
    static_assert(sizeof(TTEntry) == 16, "transposition entry must stay compact");
    const std::size_t requestedEntries =
        std::max<std::size_t>(1, (hashKilobytes * 1024U) / sizeof(TTEntry));
    tableSize_ = 1;
    while ((tableSize_ << 1U) <= requestedEntries) tableSize_ <<= 1U;
    tableMask_ = tableSize_ - 1U;
    table_ = std::make_unique<TTEntry[]>(tableSize_);
    clearHash();
}

Engine::~Engine() = default;

void Engine::clearHash() {
    if (table_ != nullptr) std::fill_n(table_.get(), tableSize_, TTEntry{});
    for (auto& color : history_) {
        for (auto& from : color) from.fill(0);
    }
    for (auto& pair : killerMoves_) pair = {{Move{}, Move{}}};
    age_ = 0;
}

void Engine::requestStop() { stopRequested_.store(true, std::memory_order_relaxed); }

std::uint64_t Engine::nowMs() {
#ifdef ARDUINO
    return static_cast<std::uint64_t>(millis());
#else
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());
#endif
}

bool Engine::shouldStop() {
    if (aborted_) return true;
    if (stopRequested_.load(std::memory_order_relaxed)) {
        aborted_ = true;
        return true;
    }
    if (maxNodes_ != 0 && nodes_ >= maxNodes_) {
        aborted_ = true;
        return true;
    }
    if (timed_ && nowMs() >= deadlineMs_) {
        aborted_ = true;
        return true;
    }
    return false;
}

int Engine::scoreToTable(int score, int ply) {
    if (score > kMateThreshold) return score + ply;
    if (score < -kMateThreshold) return score - ply;
    return score;
}

int Engine::scoreFromTable(int score, int ply) {
    if (score > kMateThreshold) return score - ply;
    if (score < -kMateThreshold) return score + ply;
    return score;
}

void Engine::storeHash(std::uint64_t key, int depth, int ply, int score,
                       std::uint8_t bound, Move move) {
    TTEntry& entry = table_[static_cast<std::size_t>(key) & tableMask_];
    const std::uint8_t entryAge = static_cast<std::uint8_t>(entry.boundAndAge >> 2U);
    const bool replace = entry.key != key || depth >= entry.depth || entryAge != age_;
    if (!replace) return;
    entry.key = key;
    entry.move = encodeMove(move);
    entry.score = static_cast<std::int16_t>(clampScore(scoreToTable(score, ply)));
    entry.depth = static_cast<std::int8_t>(std::max(-1, std::min(127, depth)));
    entry.boundAndAge = static_cast<std::uint8_t>((age_ << 2U) | (bound & 0x03U));
}

bool Engine::probeHash(std::uint64_t key, int depth, int ply, int alpha, int beta,
                       int& score, Move& move) const {
    const TTEntry& entry = table_[static_cast<std::size_t>(key) & tableMask_];
    if (entry.key != key) return false;
    move = decodeMove(entry.move);
    if (entry.depth < depth) return false;
    score = scoreFromTable(entry.score, ply);
    const std::uint8_t bound = entry.boundAndAge & 0x03U;
    if (bound == kBoundExact) return true;
    if (bound == kBoundLower && score >= beta) return true;
    if (bound == kBoundUpper && score <= alpha) return true;
    return false;
}

int Engine::moveOrderingScore(const Position& position, const Move& move, int ply,
                              Move hashMove) const {
    if (move == hashMove) return 30000;
    if (move.isPromotion()) {
        return 24000 + kPieceValue[static_cast<std::size_t>(move.promotion)];
    }
    if (move.isCapture()) {
        Piece captured = position.board_[move.to];
        if ((move.flags & MoveEnPassant) != 0U) {
            captured = makePiece(opposite(position.sideToMove_), PieceType::Pawn);
        }
        const Piece moving = position.board_[move.from];
        return 20000 +
               16 * kPieceValue[static_cast<std::size_t>(pieceType(captured))] -
               kPieceValue[static_cast<std::size_t>(pieceType(moving))];
    }
    if (move == killerMoves_[static_cast<std::size_t>(ply)][0]) return 19000;
    if (move == killerMoves_[static_cast<std::size_t>(ply)][1]) return 18000;
    return history_[static_cast<std::size_t>(colorIndex(position.sideToMove_))][move.from]
                   [move.to];
}

void Engine::scoreAndSortMoves(const Position& position, MoveList& list, int ply,
                               Move hashMove) {
    auto& scores = moveScores_[static_cast<std::size_t>(ply)];
    for (std::uint16_t index = 0; index < list.size; ++index) {
        scores[index] = static_cast<std::int16_t>(std::min(
            32767, moveOrderingScore(position, list[index], ply, hashMove)));
    }
    for (std::uint16_t index = 1; index < list.size; ++index) {
        const Move move = list[index];
        const std::int16_t score = scores[index];
        std::uint16_t current = index;
        while (current > 0 && scores[current - 1] < score) {
            list[current] = list[current - 1];
            scores[current] = scores[current - 1];
            --current;
        }
        list[current] = move;
        scores[current] = score;
    }
}

int Engine::evaluate(const Position& position) {
    std::array<int, 2> middlegame{{0, 0}};
    std::array<int, 2> endgame{{0, 0}};
    std::array<int, 2> bishops{{0, 0}};
    std::array<std::array<int, 8>, 2> pawnsByFile{};
    int phase = 0;

    for (int square = 0; square < 64; ++square) {
        const Piece piece = position.board_[static_cast<std::size_t>(square)];
        if (piece == 0) continue;
        const Color color = pieceColor(piece);
        const int side = colorIndex(color);
        const PieceType type = pieceType(piece);
        const int file = fileOf(square);
        const int rank = rankOf(square);
        const int relativeRank = color == Color::White ? rank : 7 - rank;
        const int centerDistance = std::abs(file * 2 - 7) + std::abs(rank * 2 - 7);
        int mgBonus = 0;
        int egBonus = 0;

        switch (type) {
            case PieceType::Pawn:
                mgBonus = relativeRank * 7 - std::abs(file * 2 - 7) * 2;
                egBonus = relativeRank * 12 - std::abs(file * 2 - 7);
                ++pawnsByFile[static_cast<std::size_t>(side)][static_cast<std::size_t>(file)];
                break;
            case PieceType::Knight:
                mgBonus = 34 - centerDistance * 5 - (relativeRank == 0 ? 12 : 0);
                egBonus = 22 - centerDistance * 3;
                break;
            case PieceType::Bishop:
                mgBonus = 26 - centerDistance * 3 + relativeRank * 2;
                egBonus = 20 - centerDistance * 2;
                ++bishops[static_cast<std::size_t>(side)];
                break;
            case PieceType::Rook:
                mgBonus = relativeRank == 6 ? 18 : 0;
                egBonus = 8 - std::abs(file * 2 - 7);
                break;
            case PieceType::Queen:
                mgBonus = 12 - centerDistance;
                egBonus = 18 - centerDistance * 2;
                break;
            case PieceType::King:
                mgBonus = -32 + centerDistance * 4;
                if ((color == Color::White && (square == 6 || square == 2)) ||
                    (color == Color::Black && (square == 62 || square == 58))) {
                    mgBonus += 30;
                }
                egBonus = 32 - centerDistance * 4;
                break;
            case PieceType::None: break;
        }
        middlegame[static_cast<std::size_t>(side)] +=
            kPieceValue[static_cast<std::size_t>(type)] + mgBonus;
        endgame[static_cast<std::size_t>(side)] +=
            kEndgameValue[static_cast<std::size_t>(type)] + egBonus;
        phase += kPhaseWeight[static_cast<std::size_t>(type)];
    }

    for (int side = 0; side < 2; ++side) {
        if (bishops[static_cast<std::size_t>(side)] >= 2) {
            middlegame[static_cast<std::size_t>(side)] += 32;
            endgame[static_cast<std::size_t>(side)] += 45;
        }
        for (int file = 0; file < 8; ++file) {
            const int count = pawnsByFile[static_cast<std::size_t>(side)]
                                         [static_cast<std::size_t>(file)];
            if (count > 1) {
                middlegame[static_cast<std::size_t>(side)] -= (count - 1) * 14;
                endgame[static_cast<std::size_t>(side)] -= (count - 1) * 20;
            }
            if (count > 0) {
                const bool left = file > 0 &&
                                  pawnsByFile[static_cast<std::size_t>(side)]
                                             [static_cast<std::size_t>(file - 1)] > 0;
                const bool right = file < 7 &&
                                   pawnsByFile[static_cast<std::size_t>(side)]
                                              [static_cast<std::size_t>(file + 1)] > 0;
                if (!left && !right) {
                    middlegame[static_cast<std::size_t>(side)] -= count * 10;
                    endgame[static_cast<std::size_t>(side)] -= count * 8;
                }
            }
        }
    }

    for (int square = 0; square < 64; ++square) {
        const Piece piece = position.board_[static_cast<std::size_t>(square)];
        if (pieceType(piece) != PieceType::Pawn) continue;
        const Color color = pieceColor(piece);
        const int side = colorIndex(color);
        const int file = fileOf(square);
        const int rank = rankOf(square);
        const int direction = color == Color::White ? 1 : -1;
        bool passed = true;
        for (int targetFile = std::max(0, file - 1); targetFile <= std::min(7, file + 1);
             ++targetFile) {
            for (int targetRank = rank + direction; targetRank >= 0 && targetRank < 8;
                 targetRank += direction) {
                if (position.board_[static_cast<std::size_t>(targetRank * 8 + targetFile)] ==
                    makePiece(opposite(color), PieceType::Pawn)) {
                    passed = false;
                    break;
                }
            }
        }
        if (passed) {
            const int relativeRank = color == Color::White ? rank : 7 - rank;
            middlegame[static_cast<std::size_t>(side)] += relativeRank * relativeRank * 2;
            endgame[static_cast<std::size_t>(side)] += relativeRank * relativeRank * 5;
        }
    }

    for (int side = 0; side < 2; ++side) {
        const Color color = side == 0 ? Color::White : Color::Black;
        const int king = position.kingSquares_[static_cast<std::size_t>(side)];
        const int direction = color == Color::White ? 1 : -1;
        const int shieldRank = rankOf(king) + direction;
        if (shieldRank >= 0 && shieldRank < 8) {
            for (int file = std::max(0, fileOf(king) - 1);
                 file <= std::min(7, fileOf(king) + 1); ++file) {
                if (position.board_[static_cast<std::size_t>(shieldRank * 8 + file)] ==
                    makePiece(color, PieceType::Pawn)) {
                    middlegame[static_cast<std::size_t>(side)] += 9;
                }
            }
        }
    }

    phase = std::min(24, phase);
    const int mgScore = middlegame[0] - middlegame[1];
    const int egScore = endgame[0] - endgame[1];
    int score = (mgScore * phase + egScore * (24 - phase)) / 24;
    score += position.sideToMove_ == Color::White ? 8 : -8;
    return position.sideToMove_ == Color::White ? score : -score;
}

int Engine::quiescence(Position& position, int ply, int alpha, int beta) {
    if (ply >= kMaxSearchPly - 1) return evaluate(position);
    ++nodes_;
    if ((nodes_ & 1023U) == 0U && shouldStop()) return 0;
    if (position.isFiftyMoveDraw() || position.isRepetition(2) ||
        position.isInsufficientMaterial()) {
        return 0;
    }

    const bool inCheck = position.inCheck(position.sideToMove_);
    int best = -kInfinity;
    if (!inCheck) {
        const int standPat = evaluate(position);
        best = standPat;
        if (standPat >= beta) return beta;
        if (standPat > alpha) alpha = standPat;
    }

    MoveList& moves = moveLists_[static_cast<std::size_t>(ply)];
    position.generateLegalMoves(moves);
    Move hashMove{};
    int ignored = 0;
    probeHash(position.key_, 0, ply, alpha, beta, ignored, hashMove);
    scoreAndSortMoves(position, moves, ply, hashMove);
    int searched = 0;
    for (std::uint16_t index = 0; index < moves.size; ++index) {
        const Move move = moves[index];
        if (!inCheck && !move.isCapture() && !move.isPromotion()) continue;
        Undo undo;
        if (!position.makeMove(move, undo)) continue;
        const int score = -quiescence(position, ply + 1, -beta, -alpha);
        position.unmakeMove(move, undo);
        if (aborted_) return 0;
        ++searched;
        best = std::max(best, score);
        if (score >= beta) return beta;
        if (score > alpha) alpha = score;
    }
    if (inCheck && searched == 0) return -kMateScore + ply;
    return best == -kInfinity ? alpha : alpha;
}

int Engine::alphaBeta(Position& position, int depth, int ply, int alpha, int beta,
                      bool allowNull) {
    if (ply >= kMaxSearchPly - 1) return evaluate(position);
    pvLength_[static_cast<std::size_t>(ply)] = 0;
    ++nodes_;
    if ((nodes_ & 1023U) == 0U && shouldStop()) return 0;
    if (position.isFiftyMoveDraw() || position.isRepetition(2) ||
        position.isInsufficientMaterial()) {
        return 0;
    }

    const bool inCheck = position.inCheck(position.sideToMove_);
    if (inCheck && depth < kMaxSearchPly - ply - 1) ++depth;
    if (depth <= 0) return quiescence(position, ply, alpha, beta);

    const int originalAlpha = alpha;
    int hashScore = 0;
    Move hashMove{};
    if (probeHash(position.key_, depth, ply, alpha, beta, hashScore, hashMove)) {
        return hashScore;
    }

    const int staticEval = evaluate(position);
    if (allowNull && depth >= 3 && !inCheck && staticEval >= beta &&
        position.hasNonPawnMaterial(position.sideToMove_)) {
        Undo undo;
        position.makeNullMove(undo);
        const int reduction = 2 + depth / 5;
        const int score = -alphaBeta(position, depth - 1 - reduction, ply + 1, -beta,
                                     -beta + 1, false);
        position.unmakeNullMove(undo);
        if (aborted_) return 0;
        if (score >= beta) return beta;
    }

    MoveList& moves = moveLists_[static_cast<std::size_t>(ply)];
    position.generateLegalMoves(moves);
    if (moves.size == 0) return inCheck ? -kMateScore + ply : 0;
    scoreAndSortMoves(position, moves, ply, hashMove);

    int bestScore = -kInfinity;
    Move bestMove{};
    int searched = 0;
    for (std::uint16_t index = 0; index < moves.size; ++index) {
        const Move move = moves[index];
        if (!inCheck && depth == 1 && isQuiet(move) && staticEval + 130 <= alpha) {
            continue;
        }
        Undo undo;
        if (!position.makeMove(move, undo)) continue;

        int score = 0;
        if (searched == 0) {
            score = -alphaBeta(position, depth - 1, ply + 1, -beta, -alpha, true);
        } else {
            int reduction = 0;
            if (depth >= 3 && searched >= 4 && isQuiet(move) && !inCheck) {
                reduction = 1;
                if (depth >= 6 && searched >= 10) ++reduction;
            }
            score = -alphaBeta(position, depth - 1 - reduction, ply + 1, -alpha - 1,
                               -alpha, true);
            if (!aborted_ && score > alpha && reduction > 0) {
                score = -alphaBeta(position, depth - 1, ply + 1, -alpha - 1, -alpha,
                                   true);
            }
            if (!aborted_ && score > alpha && score < beta) {
                score = -alphaBeta(position, depth - 1, ply + 1, -beta, -alpha, true);
            }
        }
        position.unmakeMove(move, undo);
        if (aborted_) return 0;
        ++searched;

        if (score > bestScore) {
            bestScore = score;
            bestMove = move;
        }
        if (score > alpha) {
            alpha = score;
            pvTable_[static_cast<std::size_t>(ply)][0] = move;
            const std::uint8_t childLength = pvLength_[static_cast<std::size_t>(ply + 1)];
            for (std::uint8_t child = 0; child < childLength && child + 1 < kMaxSearchPly;
                 ++child) {
                pvTable_[static_cast<std::size_t>(ply)][static_cast<std::size_t>(child + 1)] =
                    pvTable_[static_cast<std::size_t>(ply + 1)][child];
            }
            pvLength_[static_cast<std::size_t>(ply)] =
                static_cast<std::uint8_t>(std::min<int>(kMaxSearchPly - ply,
                                                        childLength + 1));
        }
        if (alpha >= beta) {
            if (isQuiet(move)) {
                auto& killers = killerMoves_[static_cast<std::size_t>(ply)];
                if (move != killers[0]) {
                    killers[1] = killers[0];
                    killers[0] = move;
                }
                auto& history = history_[static_cast<std::size_t>(
                    colorIndex(position.sideToMove_))][move.from][move.to];
                history = static_cast<std::int16_t>(
                    std::min<int>(16000, history + depth * depth));
            }
            storeHash(position.key_, depth, ply, beta, kBoundLower, move);
            return beta;
        }
    }

    if (searched == 0) bestScore = staticEval;
    const std::uint8_t bound = alpha > originalAlpha ? kBoundExact : kBoundUpper;
    storeHash(position.key_, depth, ply, alpha, bound, bestMove);
    return alpha;
}

int Engine::searchRoot(Position& position, int depth, int alpha, int beta) {
    pvLength_[0] = 0;
    MoveList& moves = moveLists_[0];
    position.generateLegalMoves(moves);
    rootMoveCount_ = moves.size;
    if (moves.size == 0) {
        return position.inCheck(position.sideToMove_) ? -kMateScore : 0;
    }
    Move hashMove{};
    const TTEntry& rootEntry = table_[static_cast<std::size_t>(position.key_) & tableMask_];
    if (rootEntry.key == position.key_) hashMove = decodeMove(rootEntry.move);
    scoreAndSortMoves(position, moves, 0, hashMove);

    const int originalAlpha = alpha;
    int bestScore = -kInfinity;
    Move bestMove{};
    for (std::uint16_t index = 0; index < moves.size; ++index) {
        const Move move = moves[index];
        Undo undo;
        if (!position.makeMove(move, undo)) continue;
        int score = 0;
        if (index == 0) {
            score = -alphaBeta(position, depth - 1, 1, -beta, -alpha, true);
        } else {
            score = -alphaBeta(position, depth - 1, 1, -alpha - 1, -alpha, true);
            if (!aborted_ && score > alpha && score < beta) {
                score = -alphaBeta(position, depth - 1, 1, -beta, -alpha, true);
            }
        }
        position.unmakeMove(move, undo);
        if (aborted_) return 0;
        rootMoves_[index] = RootMove{move, static_cast<std::int16_t>(clampScore(score))};
        if (score > bestScore) {
            bestScore = score;
            bestMove = move;
        }
        if (score > alpha) {
            alpha = score;
            pvTable_[0][0] = move;
            const std::uint8_t childLength = pvLength_[1];
            for (std::uint8_t child = 0; child < childLength && child + 1 < kMaxSearchPly;
                 ++child) {
                pvTable_[0][static_cast<std::size_t>(child + 1)] = pvTable_[1][child];
            }
            pvLength_[0] = static_cast<std::uint8_t>(
                std::min<int>(kMaxSearchPly, static_cast<int>(childLength) + 1));
        }
    }
    // Keep equal fail-hard bounds in their original order. A later move can
    // return the current alpha as a bound without actually matching the best
    // move; an unstable sort could otherwise displace the proven PV move.
    for (std::uint16_t index = 1; index < rootMoveCount_; ++index) {
        const RootMove candidate = rootMoves_[index];
        std::uint16_t current = index;
        while (current > 0 && rootMoves_[current - 1].score < candidate.score) {
            rootMoves_[current] = rootMoves_[current - 1];
            --current;
        }
        rootMoves_[current] = candidate;
    }
    const std::uint8_t bound = alpha > originalAlpha ? kBoundExact : kBoundUpper;
    storeHash(position.key_, depth, 0, bestScore, bound, bestMove);
    return bestScore;
}

void Engine::initializeBook() {
    if (bookInitialized_) return;
    bookInitialized_ = true;
    static constexpr std::array<std::string_view, 20> lines = {{
        "e2e4 e7e5 g1f3 b8c6 f1b5 a7a6 b5a4 g8f6 e1g1 f8e7",
        "e2e4 e7e5 g1f3 b8c6 f1c4 g8f6 d2d3 f8c5 e1g1",
        "e2e4 c7c5 g1f3 d7d6 d2d4 c5d4 f3d4 g8f6 b1c3",
        "e2e4 c7c5 g1f3 b8c6 d2d4 c5d4 f3d4 g8f6 b1c3",
        "e2e4 c7c6 d2d4 d7d5 b1c3 d5e4 c3e4",
        "e2e4 e7e6 d2d4 d7d5 b1c3 g8f6",
        "e2e4 g8f6 e4e5 f6d5 d2d4 d7d6",
        "d2d4 d7d5 c2c4 e7e6 b1c3 g8f6",
        "d2d4 g8f6 c2c4 e7e6 b1c3 f8b4",
        "d2d4 g8f6 c2c4 g7g6 b1c3 f8g7 e2e4 d7d6",
        "d2d4 f7f5 g2g3 g8f6 f1g2 g7g6",
        "c2c4 e7e5 b1c3 g8f6 g2g3 d7d5",
        "c2c4 c7c5 g1f3 g8f6 b1c3 b8c6",
        "g1f3 d7d5 d2d4 g8f6 c2c4 e7e6",
        "g1f3 g8f6 c2c4 g7g6 b1c3 f8g7",
        "b2b3 e7e5 c1b2 b8c6 e2e3 g8f6",
        "e2e4 e7e5 f2f4 e5f4 g1f3 g7g5",
        "d2d4 d7d5 c1f4 g8f6 e2e3 c7c5",
        "e2e4 d7d5 e4d5 d8d5 b1c3 d5d8",
        "c2c4 g8f6 b1c3 e7e5 g1f3 b8c6",
    }};

    for (std::string_view line : lines) {
        Position position = Position::startPosition();
        std::size_t begin = 0;
        while (begin < line.size()) {
            const std::size_t end = line.find(' ', begin);
            const std::string_view token = line.substr(
                begin, end == std::string_view::npos ? line.size() - begin : end - begin);
            Move move;
            if (!position.parseUciMove(token, move)) break;
            bool duplicate = false;
            for (std::uint16_t index = 0; index < bookSize_; ++index) {
                if (book_[index].key == position.key_ && book_[index].move == move) {
                    duplicate = true;
                    break;
                }
            }
            if (!duplicate && bookSize_ < book_.size()) {
                book_[bookSize_++] = BookEntry{position.key_, move};
            }
            Undo undo;
            if (!position.makeMove(move, undo)) break;
            if (end == std::string_view::npos) break;
            begin = end + 1;
        }
    }
}

bool Engine::findBookMove(Position& position, std::uint64_t seed, Move& move) {
    initializeBook();
    std::array<Move, 24> choices{};
    std::uint8_t count = 0;
    for (std::uint16_t index = 0; index < bookSize_ && count < choices.size(); ++index) {
        if (book_[index].key == position.key_) choices[count++] = book_[index].move;
    }
    if (count == 0) return false;
    seed ^= position.key_ ^ 0x424F4F4BULL;
    move = choices[static_cast<std::size_t>(nextRandom(seed) % count)];
    return true;
}

Move Engine::chooseSkillMove(const SearchLimits& limits, std::uint64_t seed) const {
    if (completedRootMoveCount_ == 0) return Move{};
    const int best = completedRootMoves_[0].score;
    std::uint16_t eligible = 1;
    const std::uint16_t maximum = std::min<std::uint16_t>(
        completedRootMoveCount_, std::max<std::uint8_t>(1, limits.candidateCount));
    while (eligible < maximum &&
           completedRootMoves_[eligible].score >= best - limits.errorWindowCp) {
        ++eligible;
    }
    if (eligible <= 1 || limits.errorWindowCp == 0) return completedRootMoves_[0].move;
    seed ^= 0x534B494C4CULL;
    const std::uint16_t choice = static_cast<std::uint16_t>(nextRandom(seed) % eligible);
    return completedRootMoves_[choice].move;
}

SearchResult Engine::search(const Position& position, int levelIndex,
                            std::uint64_t randomSeed) {
    const LevelConfig& level = levelConfig(levelIndex);
    SearchLimits limits;
    limits.moveTimeMs = level.moveTimeMs;
    limits.maxDepth = level.maxDepth;
    limits.errorWindowCp = level.errorWindowCp;
    limits.candidateCount = level.candidateCount;
    limits.randomSeed = randomSeed;
    return search(position, limits);
}

SearchResult Engine::search(const Position& rootPosition, const SearchLimits& limits) {
    Position position = rootPosition;
    SearchResult result;
    const std::uint64_t start = nowMs();
    stopRequested_.store(false, std::memory_order_relaxed);
    aborted_ = false;
    nodes_ = 0;
    maxNodes_ = limits.maxNodes;
    timed_ = limits.moveTimeMs > 0;
    deadlineMs_ = start + limits.moveTimeMs;
    completedRootMoveCount_ = 0;
    age_ = static_cast<std::uint8_t>((age_ + 1U) & 0x3FU);

    MoveList legal;
    position.generateLegalMoves(legal);
    if (legal.size == 0) {
        result.elapsedMs = static_cast<std::uint32_t>(nowMs() - start);
        return result;
    }

    Move bookMove;
    if (limits.useOpeningBook && position.fullmoveNumber_ <= 8 &&
        findBookMove(position, limits.randomSeed, bookMove)) {
        result.bestMove = bookMove;
        result.hasMove = true;
        result.fromBook = true;
        result.principalVariation[0] = bookMove;
        result.principalVariationLength = 1;
        result.elapsedMs = static_cast<std::uint32_t>(nowMs() - start);
        return result;
    }

    int previousScore = 0;
    const int depthLimit = std::max<int>(1, std::min<int>(limits.maxDepth,
                                                          kMaxSearchPly - 2));
    for (int depth = 1; depth <= depthLimit; ++depth) {
        int alpha = -kInfinity;
        int beta = kInfinity;
        if (depth >= 4) {
            alpha = std::max(-kInfinity, previousScore - 40);
            beta = std::min(kInfinity, previousScore + 40);
        }

        int score = searchRoot(position, depth, alpha, beta);
        if (!aborted_ && (score <= alpha || score >= beta)) {
            score = searchRoot(position, depth, -kInfinity, kInfinity);
        }
        if (aborted_) break;
        previousScore = score;
        completedRootMoveCount_ = rootMoveCount_;
        std::copy_n(rootMoves_.begin(), rootMoveCount_, completedRootMoves_.begin());

        result.hasMove = true;
        result.bestMove = rootMoves_[0].move;
        result.scoreCp = static_cast<std::int16_t>(clampScore(score));
        result.completedDepth = static_cast<std::uint8_t>(depth);
        result.principalVariationLength = pvLength_[0];
        std::copy_n(pvTable_[0].begin(), result.principalVariationLength,
                    result.principalVariation.begin());
        if (std::abs(score) > kMateThreshold) break;
        if (timed_ && nowMs() + 5 >= deadlineMs_) break;
    }

    if (!result.hasMove) {
        result.hasMove = true;
        result.bestMove = legal[0];
        completedRootMoveCount_ = 1;
        completedRootMoves_[0] = RootMove{legal[0], 0};
    }
    const Move skilled = chooseSkillMove(limits, limits.randomSeed ^ position.key_);
    if (skilled.from != result.bestMove.from || skilled.to != result.bestMove.to ||
        skilled.promotion != result.bestMove.promotion) {
        result.bestMove = skilled;
        result.principalVariation[0] = skilled;
        result.principalVariationLength = 1;
        for (std::uint16_t index = 0; index < completedRootMoveCount_; ++index) {
            if (completedRootMoves_[index].move == skilled) {
                result.scoreCp = completedRootMoves_[index].score;
                break;
            }
        }
    }
    result.stopped = aborted_;
    result.nodes = nodes_;
    result.elapsedMs = static_cast<std::uint32_t>(nowMs() - start);
    return result;
}

}  // namespace cardputer_chess
