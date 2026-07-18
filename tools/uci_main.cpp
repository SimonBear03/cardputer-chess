#include "cardputer_chess/chess.hpp"
#include "cardputer_chess/engine.hpp"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

using namespace cardputer_chess;

namespace {

std::vector<std::string> words(const std::string& line) {
    std::istringstream input(line);
    std::vector<std::string> result;
    for (std::string word; input >> word;) result.push_back(word);
    return result;
}

bool applyMoves(Position& position, const std::vector<std::string>& tokens,
                std::size_t begin) {
    for (std::size_t index = begin; index < tokens.size(); ++index) {
        Move move;
        if (!position.parseUciMove(tokens[index], move)) return false;
        Undo undo;
        if (!position.makeMove(move, undo)) return false;
    }
    return true;
}

bool setPosition(Position& position, const std::vector<std::string>& tokens) {
    if (tokens.size() < 2) return false;
    std::size_t movesIndex = tokens.size();
    for (std::size_t index = 2; index < tokens.size(); ++index) {
        if (tokens[index] == "moves") {
            movesIndex = index;
            break;
        }
    }

    if (tokens[1] == "startpos") {
        position = Position::startPosition();
    } else if (tokens[1] == "fen" && movesIndex >= 8) {
        std::string fen;
        for (std::size_t index = 2; index < movesIndex; ++index) {
            if (!fen.empty()) fen.push_back(' ');
            fen += tokens[index];
        }
        const auto parsed = Position::fromFen(fen);
        if (!parsed.has_value()) return false;
        position = *parsed;
    } else {
        return false;
    }
    return movesIndex == tokens.size() || applyMoves(position, tokens, movesIndex + 1);
}

std::uint64_t unsignedValue(const std::vector<std::string>& tokens,
                            const std::string& name, std::uint64_t fallback) {
    for (std::size_t index = 0; index + 1 < tokens.size(); ++index) {
        if (tokens[index] != name) continue;
        try {
            return std::stoull(tokens[index + 1]);
        } catch (...) {
            return fallback;
        }
    }
    return fallback;
}

}  // namespace

int main() {
    std::ios::sync_with_stdio(false);
    Position position = Position::startPosition();
    std::size_t hashKilobytes = 64;
    auto engine = std::make_unique<Engine>(hashKilobytes);

    for (std::string line; std::getline(std::cin, line);) {
        const std::vector<std::string> tokens = words(line);
        if (tokens.empty()) continue;
        if (tokens[0] == "uci") {
            std::cout << "id name Cardputer Chess\n"
                      << "id author SimonBear03\n"
                      << "option name Hash type spin default 64 min 1 max 256\n"
                      << "uciok\n" << std::flush;
        } else if (tokens[0] == "isready") {
            std::cout << "readyok\n" << std::flush;
        } else if (tokens[0] == "ucinewgame") {
            engine->clearHash();
            position = Position::startPosition();
        } else if (tokens[0] == "setoption") {
            for (std::size_t index = 0; index + 1 < tokens.size(); ++index) {
                if (tokens[index] != "value") continue;
                try {
                    hashKilobytes = static_cast<std::size_t>(std::clamp<unsigned long long>(
                        std::stoull(tokens[index + 1]), 1, 256));
                    engine = std::make_unique<Engine>(hashKilobytes);
                } catch (...) {
                }
                break;
            }
        } else if (tokens[0] == "position") {
            setPosition(position, tokens);
        } else if (tokens[0] == "go") {
            SearchLimits limits;
            limits.maxDepth = static_cast<std::uint8_t>(std::min<std::uint64_t>(
                46, unsignedValue(tokens, "depth", 24)));
            limits.maxNodes = unsignedValue(tokens, "nodes", 0);
            limits.moveTimeMs = static_cast<std::uint32_t>(std::min<std::uint64_t>(
                UINT32_MAX, unsignedValue(tokens, "movetime", 0)));
            if (limits.moveTimeMs == 0 && limits.maxNodes == 0 &&
                unsignedValue(tokens, "depth", 0) == 0) {
                const std::string clockName =
                    position.sideToMove() == Color::White ? "wtime" : "btime";
                const std::uint64_t remaining = unsignedValue(tokens, clockName, 1000);
                const std::uint64_t movesToGo = unsignedValue(tokens, "movestogo", 30);
                limits.moveTimeMs = static_cast<std::uint32_t>(
                    std::max<std::uint64_t>(10, remaining / std::max<std::uint64_t>(1,
                                                                                   movesToGo)));
            }
            limits.candidateCount = 1;
            limits.errorWindowCp = 0;
            limits.useOpeningBook = false;
            const SearchResult result = engine->search(position, limits);
            std::cout << "info depth " << static_cast<unsigned>(result.completedDepth)
                      << " score cp " << result.scoreCp << " nodes " << result.nodes
                      << " time " << result.elapsedMs << " pv";
            for (std::uint8_t index = 0; index < result.principalVariationLength; ++index) {
                std::cout << ' ' << Position::moveToUci(result.principalVariation[index]);
            }
            std::cout << "\nbestmove "
                      << (result.hasMove ? Position::moveToUci(result.bestMove) : "0000")
                      << '\n' << std::flush;
        } else if (tokens[0] == "quit") {
            break;
        }
    }
    return 0;
}
