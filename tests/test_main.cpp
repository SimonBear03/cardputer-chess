#include "cardputer_chess/chess.hpp"
#include "cardputer_chess/coach.hpp"
#include "cardputer_chess/engine.hpp"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>

using namespace cardputer_chess;

namespace {

int failures = 0;
int assertions = 0;

void expect(bool condition, std::string_view message) {
    ++assertions;
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

template <typename Actual, typename Expected>
void expectEqual(const Actual& actual, const Expected& expected, std::string_view message) {
    ++assertions;
    if (!(actual == expected)) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

void expectEqual(std::uint64_t actual, std::uint64_t expected, std::string_view message) {
    ++assertions;
    if (actual != expected) {
        ++failures;
        std::cerr << "FAIL: " << message << " (actual=" << actual
                  << ", expected=" << expected << ")\n";
    }
}

Position fen(std::string_view text) {
    std::string error;
    const auto parsed = Position::fromFen(text, &error);
    if (!parsed.has_value()) {
        std::cerr << "FATAL: invalid test FEN: " << text << " (" << error << ")\n";
        std::exit(2);
    }
    return *parsed;
}

Move requireMove(Position& position, std::string_view uci) {
    Move move;
    if (!position.parseUciMove(uci, move)) {
        std::cerr << "FATAL: expected legal move " << uci << " in " << position.toFen()
                  << '\n';
        std::exit(2);
    }
    return move;
}

bool hasMove(Position& position, std::string_view uci) {
    Move move;
    return position.parseUciMove(uci, move);
}

void play(Position& position, std::string_view uci) {
    const Move move = requireMove(position, uci);
    Undo undo;
    expect(position.makeMove(move, undo), "legal parsed move can be made");
}

void testFenAndCoordinates() {
    const Position start = Position::startPosition();
    expectEqual(start.toFen(),
                std::string("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"),
                "start position FEN round-trips");
    expectEqual(Position::parseSquare("a1"), 0, "a1 square index");
    expectEqual(Position::parseSquare("h8"), 63, "h8 square index");
    expectEqual(Position::squareName(28), std::string("e4"), "square name e4");
    expectEqual(Position::parseSquare("z9"), -1, "invalid square rejected");

    std::string error;
    expect(!Position::fromFen("8/8/8/8/8/8/8/8 w - - 0 1", &error).has_value(),
           "FEN without kings rejected");
    expect(!error.empty(), "invalid FEN reports reason");
}

void testPerft() {
    Position start = Position::startPosition();
    expectEqual(perft(start, 1), UINT64_C(20), "start perft depth 1");
    expectEqual(perft(start, 2), UINT64_C(400), "start perft depth 2");
    expectEqual(perft(start, 3), UINT64_C(8902), "start perft depth 3");
    expectEqual(perft(start, 4), UINT64_C(197281), "start perft depth 4");

    Position kiwipete = fen(
        "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1");
    expectEqual(perft(kiwipete, 1), UINT64_C(48), "kiwipete perft depth 1");
    expectEqual(perft(kiwipete, 2), UINT64_C(2039), "kiwipete perft depth 2");
    expectEqual(perft(kiwipete, 3), UINT64_C(97862), "kiwipete perft depth 3");

    Position endgame =
        fen("8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1");
    expectEqual(perft(endgame, 1), UINT64_C(14), "endgame perft depth 1");
    expectEqual(perft(endgame, 2), UINT64_C(191), "endgame perft depth 2");
    expectEqual(perft(endgame, 3), UINT64_C(2812), "endgame perft depth 3");
    expectEqual(perft(endgame, 4), UINT64_C(43238), "endgame perft depth 4");

    Position castlingTactics = fen(
        "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1");
    expectEqual(perft(castlingTactics, 1), UINT64_C(6),
                "castling tactics perft depth 1");
    expectEqual(perft(castlingTactics, 2), UINT64_C(264),
                "castling tactics perft depth 2");
    expectEqual(perft(castlingTactics, 3), UINT64_C(9467),
                "castling tactics perft depth 3");

    Position promotionTactics =
        fen("rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8");
    expectEqual(perft(promotionTactics, 1), UINT64_C(44),
                "promotion tactics perft depth 1");
    expectEqual(perft(promotionTactics, 2), UINT64_C(1486),
                "promotion tactics perft depth 2");
    expectEqual(perft(promotionTactics, 3), UINT64_C(62379),
                "promotion tactics perft depth 3");

    Position middlegame = fen(
        "r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10");
    expectEqual(perft(middlegame, 1), UINT64_C(46), "middlegame perft depth 1");
    expectEqual(perft(middlegame, 2), UINT64_C(2079), "middlegame perft depth 2");
    expectEqual(perft(middlegame, 3), UINT64_C(89890), "middlegame perft depth 3");
}

void testMakeUnmakeIdentity() {
    Position position = Position::startPosition();
    const std::string originalFen = position.toFen();
    const std::uint64_t originalKey = position.key();
    MoveList moves;
    position.generateLegalMoves(moves);
    for (std::uint16_t index = 0; index < moves.size; ++index) {
        Undo undo;
        expect(position.makeMove(moves[index], undo), "generated move is legal");
        position.unmakeMove(moves[index], undo);
        expectEqual(position.toFen(), originalFen, "make/unmake restores FEN");
        expectEqual(position.key(), originalKey, "make/unmake restores hash");
    }
}

void testCastling() {
    Position position = fen("r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1");
    expect(hasMove(position, "e1g1"), "white kingside castling legal");
    expect(hasMove(position, "e1c1"), "white queenside castling legal");

    const Move castle = requireMove(position, "e1g1");
    const std::string before = position.toFen();
    Undo undo;
    expect(position.makeMove(castle, undo), "castle can be made");
    expectEqual(pieceType(position.pieceAt(Position::parseSquare("g1"))), PieceType::King,
                "king lands on g1");
    expectEqual(pieceType(position.pieceAt(Position::parseSquare("f1"))), PieceType::Rook,
                "rook lands on f1");
    position.unmakeMove(castle, undo);
    expectEqual(position.toFen(), before, "castle unmake restores position");

    Position throughCheck = fen("r3kr2/8/8/8/8/8/8/R3K2R w KQ - 0 1");
    expect(!hasMove(throughCheck, "e1g1"), "cannot castle through attacked f1");
    expect(hasMove(throughCheck, "e1c1"), "unattacked queenside castle remains legal");
}

void testEnPassant() {
    Position position = fen("4k3/8/8/3pP3/8/8/8/4K3 w - d6 0 1");
    const Move move = requireMove(position, "e5d6");
    expect((move.flags & MoveEnPassant) != 0U, "en-passant move flag present");
    Undo undo;
    expect(position.makeMove(move, undo), "en-passant move can be made");
    expectEqual(position.pieceAt(Position::parseSquare("d5")), Piece{0},
                "captured pawn removed from d5");
    expectEqual(position.pieceAt(Position::parseSquare("d6")),
                makePiece(Color::White, PieceType::Pawn), "white pawn lands on d6");
    position.unmakeMove(move, undo);
    expectEqual(position.toFen(),
                std::string("4k3/8/8/3pP3/8/8/8/4K3 w - d6 0 1"),
                "en-passant unmake restores FEN");

    Position pinned = fen("k3r3/8/8/3pP3/8/8/8/4K3 w - d6 0 1");
    expect(!hasMove(pinned, "e5d6"), "pinned en-passant exposing king is illegal");
}

void testPromotion() {
    Position position = fen("4k3/P7/8/8/8/8/8/4K3 w - - 0 1");
    for (std::string_view uci : {"a7a8q", "a7a8r", "a7a8b", "a7a8n"}) {
        expect(hasMove(position, uci), "all four promotion choices legal");
    }
    expect(!hasMove(position, "a7a8"), "promotion piece is required");
    const Move promote = requireMove(position, "a7a8n");
    Undo undo;
    expect(position.makeMove(promote, undo), "knight promotion can be made");
    expectEqual(pieceType(position.pieceAt(Position::parseSquare("a8"))), PieceType::Knight,
                "promotion creates selected piece");
    position.unmakeMove(promote, undo);
    expectEqual(position.toFen(), std::string("4k3/P7/8/8/8/8/8/4K3 w - - 0 1"),
                "promotion unmake restores pawn");
}

void testOutcomes() {
    Position mate =
        fen("rnb1kbnr/pppp1ppp/8/4p3/6Pq/5P2/PPPPP2P/RNBQKBNR w KQkq - 1 3");
    expectEqual(mate.gameState(), GameState::BlackWinsCheckmate,
                "fool's mate is black checkmate win");

    Position stalemate = fen("7k/5Q2/6K1/8/8/8/8/8 b - - 0 1");
    expectEqual(stalemate.gameState(), GameState::Stalemate, "stalemate recognized");

    Position fifty = fen("8/8/8/8/8/6k1/8/6K1 w - - 100 51");
    expectEqual(fifty.gameState(), GameState::DrawFiftyMove, "fifty-move draw recognized");

    expect(fen("8/8/8/8/8/6k1/8/6K1 w - - 0 1").isInsufficientMaterial(),
           "king versus king insufficient");
    expect(fen("8/8/8/8/8/6k1/8/5BK1 w - - 0 1").isInsufficientMaterial(),
           "king and bishop versus king insufficient");
    expect(fen("8/8/8/8/8/6k1/8/5NK1 w - - 0 1").isInsufficientMaterial(),
           "king and knight versus king insufficient");
    expect(!fen("8/8/8/8/8/6k1/8/4BNK1 w - - 0 1").isInsufficientMaterial(),
           "bishop and knight can mate");
}

void testRepetition() {
    Position position = Position::startPosition();
    constexpr std::array<std::string_view, 8> moves = {
        "g1f3", "g8f6", "f3g1", "f6g8", "g1f3", "g8f6", "f3g1", "f6g8"};
    for (const auto move : moves) play(position, move);
    expect(position.isRepetition(3), "threefold repetition counter recognized");
    expectEqual(position.gameState(), GameState::DrawThreefold,
                "threefold repetition game state");

    Position uncapturableEnPassant = Position::startPosition();
    constexpr std::array<std::string_view, 10> enPassantCycle = {
        "g1f3", "h7h5", "f3g1", "g8f6", "g1f3",
        "f6g8", "f3g1", "g8f6", "g1f3", "f6g8"};
    for (const auto move : enPassantCycle) play(uncapturableEnPassant, move);
    expect(uncapturableEnPassant.isRepetition(3),
           "uncapturable en-passant target does not prevent repetition");
    expectEqual(uncapturableEnPassant.gameState(), GameState::DrawThreefold,
                "uncapturable en-passant sequence is a threefold draw");
}

void testSanNotation() {
    Position start = Position::startPosition();
    const std::string startFen = start.toFen();
    const std::uint64_t startKey = start.key();
    expectEqual(start.moveToSan(requireMove(start, "e2e4")), std::string("e4"),
                "SAN formats a quiet pawn move");
    expectEqual(start.toFen(), startFen, "SAN formatting preserves FEN");
    expectEqual(start.key(), startKey, "SAN formatting preserves hash key");

    play(start, "e2e4");
    play(start, "e7e5");
    expectEqual(start.moveToSan(requireMove(start, "g1f3")), std::string("Nf3"),
                "SAN formats a quiet piece move");

    Position capture = fen("4k3/8/8/3p4/4P3/8/8/4K3 w - - 0 1");
    expectEqual(capture.moveToSan(requireMove(capture, "e4d5")), std::string("exd5"),
                "SAN formats a pawn capture");

    Position castle = fen("4k3/8/8/8/8/8/8/4K2R w K - 0 1");
    expectEqual(castle.moveToSan(requireMove(castle, "e1g1")), std::string("O-O"),
                "SAN formats kingside castling");

    Position ambiguous = fen("4k3/8/8/8/8/8/8/1N2KN2 w - - 0 1");
    expectEqual(ambiguous.moveToSan(requireMove(ambiguous, "b1d2")),
                std::string("Nbd2"), "SAN disambiguates pieces by file");
    expectEqual(ambiguous.moveToSan(requireMove(ambiguous, "f1d2")),
                std::string("Nfd2"), "SAN disambiguates the alternate piece");

    Position promotion = fen("4k3/P7/8/8/8/8/8/4K3 w - - 0 1");
    expectEqual(promotion.moveToSan(requireMove(promotion, "a7a8q")),
                std::string("a8=Q+"), "SAN formats promotion with check");

    Position mate = fen("7k/8/5KQ1/8/8/8/8/8 w - - 0 1");
    expectEqual(mate.moveToSan(requireMove(mate, "g6g7")), std::string("Qg7#"),
                "SAN marks checkmate");
}

void testLevelsAndEngine() {
    expectEqual(std::string(levelConfig(0).name), std::string("Beginner"),
                "first level name");
    expectEqual(std::string(levelConfig(7).name), std::string("Maximum"),
                "last level name");
    for (int level = 1; level < kLevelCount; ++level) {
        expect(levelConfig(level).moveTimeMs >= levelConfig(level - 1).moveTimeMs,
               "level time budgets are monotonic");
        expect(levelConfig(level).errorWindowCp <= levelConfig(level - 1).errorWindowCp,
               "level error windows tighten monotonically");
    }

    Engine engine(64);
    Position opening = Position::startPosition();
    const std::string openingFen = opening.toFen();
    SearchLimits bookLimits;
    bookLimits.moveTimeMs = 0;
    bookLimits.maxDepth = 2;
    bookLimits.useOpeningBook = true;
    bookLimits.randomSeed = 42;
    const SearchResult book = engine.search(opening, bookLimits);
    expect(book.hasMove, "opening search returns a move");
    expect(book.fromBook, "start position uses opening book");
    expectEqual(book.lineCount, std::uint8_t{1}, "book result exposes one PV line");
    expect(book.lines[0].bestMove == book.bestMove,
           "book PV line starts with returned move");
    expectEqual(opening.toFen(), openingFen, "search does not mutate caller position");
    Move parsedBook;
    expect(opening.parseUciMove(Position::moveToUci(book.bestMove), parsedBook),
           "book move is legal");

    Position berlin = Position::startPosition();
    for (std::string_view move : {"e2e4", "e7e5", "g1f3", "b8c6", "f1b5",
                                  "g8f6"}) {
        play(berlin, move);
    }
    const SearchResult berlinBook = engine.search(berlin, bookLimits);
    expect(berlinBook.fromBook, "expanded opening book reaches the Berlin position");
    expectEqual(Position::moveToUci(berlinBook.bestMove), std::string("e1g1"),
                "expanded opening book follows the vetted Berlin mainline");
    expectEqual(berlinBook.lineCount, std::uint8_t{1},
                "expanded book result exposes one PV line");

    Position mateInOne = fen("7k/8/5KQ1/8/8/8/8/8 w - - 0 1");
    SearchLimits tactical;
    tactical.moveTimeMs = 0;
    tactical.maxDepth = 4;
    tactical.maxNodes = 200000;
    tactical.useOpeningBook = false;
    const SearchResult result = engine.search(mateInOne, tactical);
    expect(result.hasMove, "tactical search returns move");
    Undo undo;
    expect(mateInOne.makeMove(result.bestMove, undo), "engine move is legal");
    if (mateInOne.gameState() != GameState::WhiteWinsCheckmate) {
        std::cerr << "INFO: mate search chose " << Position::moveToUci(result.bestMove)
                  << " score=" << result.scoreCp
                  << " depth=" << static_cast<int>(result.completedDepth)
                  << " nodes=" << result.nodes
                  << " black_check=" << mateInOne.inCheck(Color::Black)
                  << " state=" << static_cast<int>(mateInOne.gameState()) << '\n';
    }
    expectEqual(mateInOne.gameState(), GameState::WhiteWinsCheckmate,
                "engine finds mate in one");

    Position bounded = fen("r1bqkbnr/pppp1ppp/2n5/4p3/4P3/5N2/PPPP1PPP/RNBQKB1R w KQkq - 2 3");
    SearchLimits nodeLimits;
    nodeLimits.moveTimeMs = 0;
    nodeLimits.maxDepth = 20;
    nodeLimits.maxNodes = 4096;
    nodeLimits.useOpeningBook = false;
    const SearchResult limited = engine.search(bounded, nodeLimits);
    expect(limited.hasMove, "node-limited search still returns a legal fallback");
    expect(limited.stopped, "node-limited search reports stop");
    Move parsed;
    expect(bounded.parseUciMove(Position::moveToUci(limited.bestMove), parsed),
           "node-limited result is legal");

    Position analysis = fen(
        "r1bqkbnr/pppp1ppp/2n5/4p3/4P3/5N2/PPPP1PPP/RNBQKB1R w KQkq - 2 3");
    const std::string analysisFen = analysis.toFen();
    const std::uint64_t analysisKey = analysis.key();
    SearchLimits analysisLimits;
    analysisLimits.moveTimeMs = 0;
    analysisLimits.maxDepth = 3;
    analysisLimits.multiPv = 3;
    analysisLimits.useOpeningBook = true;
    const SearchResult multiPv = engine.search(analysis, analysisLimits);
    expect(multiPv.hasMove, "MultiPV search returns a move");
    expect(!multiPv.fromBook, "MultiPV bypasses book to evaluate every line");
    expectEqual(multiPv.lineCount, std::uint8_t{3}, "MultiPV returns three lines");
    expect(multiPv.lines[0].bestMove == multiPv.bestMove,
           "first MultiPV line is the selected best move");
    for (std::uint8_t line = 0; line < multiPv.lineCount; ++line) {
        Move legalLineMove;
        expect(analysis.parseUciMove(Position::moveToUci(multiPv.lines[line].bestMove),
                                     legalLineMove),
               "each MultiPV candidate is legal");
        expect(multiPv.lines[line].principalVariationLength > 0,
               "each MultiPV candidate has a principal variation");
        expect(multiPv.lines[line].principalVariation[0] == multiPv.lines[line].bestMove,
               "each MultiPV line begins with its candidate move");
        for (std::uint8_t earlier = 0; earlier < line; ++earlier) {
            expect(!(multiPv.lines[line].bestMove == multiPv.lines[earlier].bestMove),
                   "MultiPV candidates are unique");
        }
        if (line > 0) {
            expect(multiPv.lines[line - 1].scoreCp >= multiPv.lines[line].scoreCp,
                   "MultiPV candidates are ordered by score");
        }
    }
    expectEqual(analysis.toFen(), analysisFen, "MultiPV preserves caller FEN");
    expectEqual(analysis.key(), analysisKey, "MultiPV preserves caller hash key");

    expectEqual(std::string(coachModeName(CoachMode::OnDemand)),
                std::string("On demand"), "coach mode has a display name");
    const CoachFeedback bestFeedback =
        classifyCoachMove(multiPv, multiPv.lines[0].bestMove);
    expectEqual(bestFeedback.quality, MoveQuality::Best,
                "top analysis move is classified as best");
    expectEqual(bestFeedback.rank, std::uint8_t{1}, "best move has rank one");
    const CoachFeedback thirdFeedback =
        classifyCoachMove(multiPv, multiPv.lines[2].bestMove);
    expectEqual(thirdFeedback.rank, std::uint8_t{3}, "third candidate has rank three");
    expect(thirdFeedback.lossCp >= 0, "candidate loss is never negative");

    Move outsideMove{};
    MoveList analysisMoves;
    analysis.generateLegalMoves(analysisMoves);
    for (std::uint16_t index = 0; index < analysisMoves.size; ++index) {
        bool listed = false;
        for (std::uint8_t line = 0; line < multiPv.lineCount; ++line) {
            listed = listed || analysisMoves[index] == multiPv.lines[line].bestMove;
        }
        if (!listed) {
            outsideMove = analysisMoves[index];
            break;
        }
    }
    const CoachFeedback outsideFeedback = classifyCoachMove(multiPv, outsideMove);
    expectEqual(outsideFeedback.quality, MoveQuality::OutsideTopThree,
                "unlisted legal move is classified honestly");
}

}  // namespace

int main() {
    testFenAndCoordinates();
    testPerft();
    testMakeUnmakeIdentity();
    testCastling();
    testEnPassant();
    testPromotion();
    testOutcomes();
    testRepetition();
    testSanNotation();
    testLevelsAndEngine();

    if (failures != 0) {
        std::cerr << failures << " failure(s) across " << assertions << " assertions\n";
        return 1;
    }
    std::cout << "PASS: " << assertions << " assertions\n";
    return 0;
}
