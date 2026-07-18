#pragma once

#include "cardputer_chess/chess.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>

namespace cardputer_chess {

constexpr int kLevelCount = 8;
constexpr int kMaxSearchPly = 48;
constexpr int kMaxAnalysisLines = 3;

struct LevelConfig {
    const char* name;
    std::uint32_t moveTimeMs;
    std::uint8_t maxDepth;
    std::uint16_t errorWindowCp;
    std::uint8_t candidateCount;
};

const LevelConfig& levelConfig(int levelIndex);

struct SearchLimits {
    std::uint32_t moveTimeMs = 1000;
    std::uint8_t maxDepth = 12;
    std::uint64_t maxNodes = 0;
    std::uint16_t errorWindowCp = 0;
    std::uint8_t candidateCount = 1;
    std::uint8_t multiPv = 1;
    std::uint64_t randomSeed = 0;
    bool useOpeningBook = true;
};

struct AnalysisLine {
    Move bestMove{};
    std::int16_t scoreCp = 0;
    std::array<Move, kMaxSearchPly> principalVariation{};
    std::uint8_t principalVariationLength = 0;
};

struct SearchResult {
    Move bestMove{};
    bool hasMove = false;
    bool fromBook = false;
    bool stopped = false;
    std::int16_t scoreCp = 0;
    std::uint8_t completedDepth = 0;
    std::uint64_t nodes = 0;
    std::uint32_t elapsedMs = 0;
    std::array<Move, kMaxSearchPly> principalVariation{};
    std::uint8_t principalVariationLength = 0;
    std::array<AnalysisLine, kMaxAnalysisLines> lines{};
    std::uint8_t lineCount = 0;
};

class Engine {
  public:
    explicit Engine(std::size_t hashKilobytes = 64);
    ~Engine();

    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    SearchResult search(const Position& position, const SearchLimits& limits);
    SearchResult search(const Position& position, int levelIndex,
                        std::uint64_t randomSeed = 0);
    void requestStop();
    void clearHash();

    static int evaluate(const Position& position);

  private:
    struct TTEntry;
    struct RootMove {
        Move move{};
        std::int16_t score = 0;
    };
    struct BookEntry {
        std::uint64_t key = 0;
        Move move{};
    };

    std::unique_ptr<TTEntry[]> table_;
    std::size_t tableSize_ = 0;
    std::size_t tableMask_ = 0;
    std::uint8_t age_ = 0;

    std::array<MoveList, kMaxSearchPly> moveLists_{};
    std::array<std::array<std::int16_t, kMaxMoves>, kMaxSearchPly> moveScores_{};
    std::array<std::array<Move, 2>, kMaxSearchPly> killerMoves_{};
    std::array<std::array<std::array<std::int16_t, 64>, 64>, 2> history_{};
    std::array<std::array<Move, kMaxSearchPly>, kMaxSearchPly> pvTable_{};
    std::array<std::uint8_t, kMaxSearchPly> pvLength_{};
    std::array<RootMove, kMaxMoves> rootMoves_{};
    std::uint16_t rootMoveCount_ = 0;
    std::array<RootMove, kMaxMoves> completedRootMoves_{};
    std::uint16_t completedRootMoveCount_ = 0;
    std::array<BookEntry, 192> book_{};
    std::uint16_t bookSize_ = 0;
    bool bookInitialized_ = false;

    std::atomic<bool> stopRequested_{false};
    std::uint64_t nodes_ = 0;
    std::uint64_t maxNodes_ = 0;
    std::uint64_t deadlineMs_ = 0;
    bool timed_ = false;
    bool aborted_ = false;

    int alphaBeta(Position& position, int depth, int ply, int alpha, int beta,
                  bool allowNull);
    int quiescence(Position& position, int ply, int alpha, int beta);
    int searchRoot(Position& position, int depth, int alpha, int beta,
                   const Move* excludedMoves = nullptr,
                   std::uint8_t excludedCount = 0);
    void scoreAndSortMoves(const Position& position, MoveList& list, int ply,
                           Move hashMove);
    int moveOrderingScore(const Position& position, const Move& move, int ply,
                          Move hashMove) const;
    bool shouldStop();
    void storeHash(std::uint64_t key, int depth, int ply, int score,
                   std::uint8_t bound, Move move);
    bool probeHash(std::uint64_t key, int depth, int ply, int alpha, int beta,
                   int& score, Move& move) const;
    static int scoreToTable(int score, int ply);
    static int scoreFromTable(int score, int ply);
    static std::uint64_t nowMs();

    void initializeBook();
    bool findBookMove(Position& position, std::uint64_t seed, Move& move);
    Move chooseSkillMove(const SearchLimits& limits, std::uint64_t seed) const;
};

}  // namespace cardputer_chess
