#include "cardputer_chess/chess.hpp"
#include "cardputer_chess/engine.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

using namespace cardputer_chess;

namespace {

struct BenchmarkPosition {
    std::string id;
    std::string fen;
    std::vector<std::string> oracleMoves;
};

std::vector<std::string> splitMoves(std::string_view text) {
    std::vector<std::string> moves;
    std::size_t begin = 0;
    while (begin < text.size()) {
        const std::size_t end = text.find(',', begin);
        moves.emplace_back(text.substr(begin, end == std::string_view::npos
                                                  ? text.size() - begin
                                                  : end - begin));
        if (end == std::string_view::npos) break;
        begin = end + 1;
    }
    return moves;
}

std::vector<BenchmarkPosition> loadSuite(const std::string& path) {
    std::ifstream input(path);
    std::vector<BenchmarkPosition> positions;
    for (std::string line; std::getline(input, line);) {
        if (line.empty() || line.front() == '#') continue;
        const std::size_t first = line.find('|');
        const std::size_t second = line.find('|', first + 1);
        if (first == std::string::npos || second == std::string::npos) continue;
        positions.push_back(BenchmarkPosition{
            line.substr(0, first), line.substr(first + 1, second - first - 1),
            splitMoves(std::string_view(line).substr(second + 1))});
    }
    return positions;
}

std::uint64_t parseUnsigned(const char* text, std::uint64_t fallback) {
    char* end = nullptr;
    const unsigned long long value = std::strtoull(text, &end, 10);
    return end != text && *end == '\0' ? static_cast<std::uint64_t>(value) : fallback;
}

}  // namespace

int main(int argc, char** argv) {
    std::uint64_t maxNodes = 100000;
    std::uint8_t maxDepth = 24;
    std::size_t hashKilobytes = 64;
    std::string suitePath = "bench/positions.tsv";
    for (int index = 1; index + 1 < argc; index += 2) {
        const std::string_view option = argv[index];
        if (option == "--nodes") maxNodes = parseUnsigned(argv[index + 1], maxNodes);
        if (option == "--depth") {
            maxDepth = static_cast<std::uint8_t>(
                std::min<std::uint64_t>(46, parseUnsigned(argv[index + 1], maxDepth)));
        }
        if (option == "--hash-kb") {
            hashKilobytes = static_cast<std::size_t>(
                std::max<std::uint64_t>(1, parseUnsigned(argv[index + 1], hashKilobytes)));
        }
        if (option == "--suite") suitePath = argv[index + 1];
    }

    const std::vector<BenchmarkPosition> positions = loadSuite(suitePath);
    if (positions.empty()) {
        std::cerr << "benchmark suite is empty or unreadable: " << suitePath << '\n';
        return 2;
    }
    Engine engine(hashKilobytes);
    std::uint64_t totalNodes = 0;
    std::uint64_t totalMs = 0;
    std::uint64_t fingerprint = UINT64_C(1469598103934665603);
    unsigned oraclePoints = 0;
    unsigned oracleMaximum = 0;
    std::cout << "id\tbest\toracle\tscore\tdepth\tnodes\tms\tknps\n";
    for (const BenchmarkPosition& benchmark : positions) {
        const auto parsed = Position::fromFen(benchmark.fen);
        if (!parsed.has_value()) {
            std::cerr << "invalid benchmark FEN: " << benchmark.id << '\n';
            return 2;
        }
        SearchLimits limits;
        limits.moveTimeMs = 0;
        limits.maxDepth = maxDepth;
        limits.maxNodes = maxNodes;
        limits.useOpeningBook = false;
        const SearchResult result = engine.search(*parsed, limits);
        const std::string move = result.hasMove ? Position::moveToUci(result.bestMove) : "0000";
        const std::uint64_t knps = result.elapsedMs == 0
                                       ? 0
                                       : result.nodes / result.elapsedMs;
        unsigned movePoints = 0;
        const auto oracle = std::find(benchmark.oracleMoves.begin(),
                                      benchmark.oracleMoves.end(), move);
        if (!benchmark.oracleMoves.empty()) {
            oracleMaximum += 3;
            if (oracle != benchmark.oracleMoves.end()) {
                const auto rank = static_cast<unsigned>(
                    std::distance(benchmark.oracleMoves.begin(), oracle));
                movePoints = rank < 3 ? 3U - rank : 0U;
            }
        }
        oraclePoints += movePoints;
        std::cout << benchmark.id << '\t' << move << '\t' << movePoints << '\t'
                  << result.scoreCp << '\t'
                  << static_cast<unsigned>(result.completedDepth) << '\t' << result.nodes
                  << '\t' << result.elapsedMs << '\t' << knps << '\n';
        totalNodes += result.nodes;
        totalMs += result.elapsedMs;
        for (char character : move) {
            fingerprint ^= static_cast<unsigned char>(character);
            fingerprint *= UINT64_C(1099511628211);
        }
        fingerprint ^= result.completedDepth;
        fingerprint *= UINT64_C(1099511628211);
    }
    const double nps = totalMs == 0
                           ? 0.0
                           : 1000.0 * static_cast<double>(totalNodes) /
                                 static_cast<double>(totalMs);
    std::cout << "summary\tpositions=" << positions.size() << " nodes=" << totalNodes
              << " ms=" << totalMs << " nps=" << std::fixed << std::setprecision(0)
              << nps << " oracle=" << oraclePoints << '/' << oracleMaximum
              << " fingerprint=0x" << std::hex << fingerprint << std::dec << '\n';
    return 0;
}
