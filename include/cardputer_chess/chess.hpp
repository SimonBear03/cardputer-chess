#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace cardputer_chess {

constexpr int kBoardSquares = 64;
constexpr int kMaxMoves = 256;
constexpr int kMaxGamePly = 512;

enum class Color : std::uint8_t { White = 0, Black = 1 };
enum class PieceType : std::uint8_t {
    None = 0,
    Pawn = 1,
    Knight = 2,
    Bishop = 3,
    Rook = 4,
    Queen = 5,
    King = 6,
};

using Piece = std::int8_t;

constexpr Color opposite(Color color) {
    return color == Color::White ? Color::Black : Color::White;
}

constexpr Piece makePiece(Color color, PieceType type) {
    const auto value = static_cast<Piece>(type);
    return color == Color::White ? value : static_cast<Piece>(-value);
}

constexpr PieceType pieceType(Piece piece) {
    const int value = piece < 0 ? -static_cast<int>(piece) : static_cast<int>(piece);
    return static_cast<PieceType>(value);
}

constexpr Color pieceColor(Piece piece) {
    return piece > 0 ? Color::White : Color::Black;
}

enum MoveFlag : std::uint8_t {
    MoveQuiet = 0,
    MoveCapture = 1U << 0U,
    MoveDoublePawn = 1U << 1U,
    MoveKingCastle = 1U << 2U,
    MoveQueenCastle = 1U << 3U,
    MoveEnPassant = 1U << 4U,
    MovePromotion = 1U << 5U,
};

struct Move {
    std::uint8_t from = 0;
    std::uint8_t to = 0;
    PieceType promotion = PieceType::None;
    std::uint8_t flags = MoveQuiet;

    constexpr bool operator==(const Move& other) const {
        return from == other.from && to == other.to && promotion == other.promotion &&
               flags == other.flags;
    }
    constexpr bool operator!=(const Move& other) const { return !(*this == other); }
    constexpr bool isCapture() const { return (flags & MoveCapture) != 0U; }
    constexpr bool isPromotion() const { return (flags & MovePromotion) != 0U; }
    constexpr bool isCastle() const {
        return (flags & (MoveKingCastle | MoveQueenCastle)) != 0U;
    }
};

struct MoveList {
    std::array<Move, kMaxMoves> moves{};
    std::uint16_t size = 0;

    void clear() { size = 0; }
    bool push(Move move) {
        if (size >= moves.size()) return false;
        moves[size++] = move;
        return true;
    }
    Move& operator[](std::size_t index) { return moves[index]; }
    const Move& operator[](std::size_t index) const { return moves[index]; }
};

struct Undo {
    Piece captured = 0;
    std::uint8_t castlingRights = 0;
    std::int8_t enPassantSquare = -1;
    std::uint16_t halfmoveClock = 0;
    std::uint16_t fullmoveNumber = 1;
    std::array<std::uint8_t, 2> kingSquares{};
    std::uint64_t key = 0;
};

enum CastlingRight : std::uint8_t {
    WhiteKingSide = 1U << 0U,
    WhiteQueenSide = 1U << 1U,
    BlackKingSide = 1U << 2U,
    BlackQueenSide = 1U << 3U,
};

enum class GameState : std::uint8_t {
    Ongoing,
    WhiteWinsCheckmate,
    BlackWinsCheckmate,
    Stalemate,
    DrawFiftyMove,
    DrawThreefold,
    DrawInsufficientMaterial,
};

class Position {
  public:
    Position();

    static Position startPosition();
    void resetToStartPosition();
    static std::optional<Position> fromFen(std::string_view fen,
                                           std::string* error = nullptr);

    std::string toFen() const;
    Piece pieceAt(int square) const;
    Color sideToMove() const { return sideToMove_; }
    std::uint8_t castlingRights() const { return castlingRights_; }
    int enPassantSquare() const { return enPassantSquare_; }
    std::uint16_t halfmoveClock() const { return halfmoveClock_; }
    std::uint16_t fullmoveNumber() const { return fullmoveNumber_; }
    std::uint64_t key() const { return key_; }

    void generateLegalMoves(MoveList& list);
    bool makeMove(const Move& move, Undo& undo);
    void unmakeMove(const Move& move, const Undo& undo);
    void makeNullMove(Undo& undo);
    void unmakeNullMove(const Undo& undo);

    bool inCheck(Color color) const;
    bool isSquareAttacked(int square, Color byColor) const;
    bool isRepetition(int requiredOccurrences = 3) const;
    bool isFiftyMoveDraw() const { return halfmoveClock_ >= 100; }
    bool isInsufficientMaterial() const;
    bool hasNonPawnMaterial(Color color) const;
    GameState gameState();

    static std::string squareName(int square);
    static int parseSquare(std::string_view name);
    static std::string moveToUci(const Move& move);
    std::string moveToSan(const Move& move);
    bool parseUciMove(std::string_view text, Move& result);

  private:
    std::array<Piece, kBoardSquares> board_{};
    Color sideToMove_ = Color::White;
    std::uint8_t castlingRights_ = 0;
    std::int8_t enPassantSquare_ = -1;
    std::uint16_t halfmoveClock_ = 0;
    std::uint16_t fullmoveNumber_ = 1;
    std::array<std::uint8_t, 2> kingSquares_{{4, 60}};
    std::uint64_t key_ = 0;
    std::array<std::uint64_t, kMaxGamePly> repetitionKeys_{};
    std::uint16_t repetitionCount_ = 0;

    void clear();
    void generatePseudoMoves(MoveList& list) const;
    void addPawnMove(MoveList& list, int from, int to, std::uint8_t flags) const;
    void computeKey();
    int legalEnPassantFile();
    void pushRepetitionKey();
    void popRepetitionKey();

    friend class Engine;
};

std::uint64_t perft(Position& position, int depth);

}  // namespace cardputer_chess
