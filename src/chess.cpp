#include "cardputer_chess/chess.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cctype>
#include <sstream>
#include <vector>

namespace cardputer_chess {
namespace {

constexpr int colorIndex(Color color) { return color == Color::White ? 0 : 1; }
constexpr bool onBoard(int square) { return square >= 0 && square < 64; }
constexpr int fileOf(int square) { return square & 7; }
constexpr int rankOf(int square) { return square >> 3; }

int pieceIndex(Piece piece) {
    if (piece == 0) return -1;
    const int offset = piece < 0 ? 6 : 0;
    return offset + static_cast<int>(pieceType(piece)) - 1;
}

std::uint64_t splitmix64(std::uint64_t& state) {
    std::uint64_t value = (state += 0x9E3779B97F4A7C15ULL);
    value = (value ^ (value >> 30U)) * 0xBF58476D1CE4E5B9ULL;
    value = (value ^ (value >> 27U)) * 0x94D049BB133111EBULL;
    return value ^ (value >> 31U);
}

struct Zobrist {
    std::array<std::array<std::uint64_t, 64>, 12> pieces{};
    std::array<std::uint64_t, 16> castling{};
    std::array<std::uint64_t, 8> enPassantFile{};
    std::uint64_t side = 0;

    Zobrist() {
        std::uint64_t seed = 0x4341524450555445ULL;
        for (auto& piece : pieces) {
            for (auto& square : piece) square = splitmix64(seed);
        }
        for (auto& value : castling) value = splitmix64(seed);
        for (auto& value : enPassantFile) value = splitmix64(seed);
        side = splitmix64(seed);
    }
};

const Zobrist& zobrist() {
    static const Zobrist values;
    return values;
}

char pieceToFen(Piece piece) {
    char result = ' ';
    switch (pieceType(piece)) {
        case PieceType::Pawn: result = 'p'; break;
        case PieceType::Knight: result = 'n'; break;
        case PieceType::Bishop: result = 'b'; break;
        case PieceType::Rook: result = 'r'; break;
        case PieceType::Queen: result = 'q'; break;
        case PieceType::King: result = 'k'; break;
        case PieceType::None: return ' ';
    }
    return piece > 0 ? static_cast<char>(std::toupper(result)) : result;
}

Piece fenToPiece(char value) {
    const Color color = std::isupper(static_cast<unsigned char>(value)) != 0
                            ? Color::White
                            : Color::Black;
    switch (static_cast<char>(std::tolower(static_cast<unsigned char>(value)))) {
        case 'p': return makePiece(color, PieceType::Pawn);
        case 'n': return makePiece(color, PieceType::Knight);
        case 'b': return makePiece(color, PieceType::Bishop);
        case 'r': return makePiece(color, PieceType::Rook);
        case 'q': return makePiece(color, PieceType::Queen);
        case 'k': return makePiece(color, PieceType::King);
        default: return 0;
    }
}

bool parseUnsigned(std::string_view text, std::uint16_t& value) {
    unsigned parsed = 0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), parsed);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size() ||
        parsed > 65535U) {
        return false;
    }
    value = static_cast<std::uint16_t>(parsed);
    return true;
}

}  // namespace

Position::Position() { clear(); }

void Position::clear() {
    board_.fill(0);
    sideToMove_ = Color::White;
    castlingRights_ = 0;
    enPassantSquare_ = -1;
    halfmoveClock_ = 0;
    fullmoveNumber_ = 1;
    kingSquares_ = {{4, 60}};
    key_ = 0;
    repetitionKeys_.fill(0);
    repetitionCount_ = 0;
}

Position Position::startPosition() {
    Position position;
    position.resetToStartPosition();
    return position;
}

void Position::resetToStartPosition() {
    clear();
    constexpr std::array<PieceType, 8> backRank = {
        PieceType::Rook, PieceType::Knight, PieceType::Bishop, PieceType::Queen,
        PieceType::King, PieceType::Bishop, PieceType::Knight, PieceType::Rook,
    };
    for (std::size_t file = 0; file < backRank.size(); ++file) {
        board_[file] = makePiece(Color::White, backRank[file]);
        board_[8U + file] = makePiece(Color::White, PieceType::Pawn);
        board_[48U + file] = makePiece(Color::Black, PieceType::Pawn);
        board_[56U + file] = makePiece(Color::Black, backRank[file]);
    }
    castlingRights_ = static_cast<std::uint8_t>(WhiteKingSide | WhiteQueenSide |
                                                BlackKingSide | BlackQueenSide);
    computeKey();
    pushRepetitionKey();
}

std::optional<Position> Position::fromFen(std::string_view fen, std::string* error) {
    Position result;
    if (!result.loadFen(fen, error)) return std::nullopt;
    return result;
}

bool Position::loadFen(std::string_view fen, std::string* error) {
    auto fail = [&](const char* message) {
        if (error != nullptr) *error = message;
        resetToStartPosition();
        return false;
    };

    std::array<std::string_view, 6> fields{};
    std::size_t fieldCount = 0;
    std::size_t begin = 0;
    while (begin < fen.size() && fieldCount < fields.size()) {
        while (begin < fen.size() && fen[begin] == ' ') ++begin;
        if (begin == fen.size()) break;
        const std::size_t end = fen.find(' ', begin);
        fields[fieldCount++] = fen.substr(begin, end == std::string_view::npos
                                                    ? fen.size() - begin
                                                    : end - begin);
        if (end == std::string_view::npos) break;
        begin = end + 1;
    }
    if (fieldCount != 6) return fail("FEN must contain six fields");

    clear();
    int rank = 7;
    int file = 0;
    int whiteKings = 0;
    int blackKings = 0;
    for (char value : fields[0]) {
        if (value == '/') {
            if (file != 8 || rank == 0) return fail("invalid FEN board rows");
            --rank;
            file = 0;
            continue;
        }
        if (value >= '1' && value <= '8') {
            file += value - '0';
            if (file > 8) return fail("FEN row exceeds eight squares");
            continue;
        }
        const Piece piece = fenToPiece(value);
        if (piece == 0 || file >= 8 || rank < 0) return fail("invalid FEN piece field");
        const int square = rank * 8 + file;
        board_[static_cast<std::size_t>(square)] = piece;
        if (pieceType(piece) == PieceType::King) {
            kingSquares_[static_cast<std::size_t>(colorIndex(pieceColor(piece)))] =
                static_cast<std::uint8_t>(square);
            if (pieceColor(piece) == Color::White) {
                ++whiteKings;
            } else {
                ++blackKings;
            }
        }
        ++file;
    }
    if (rank != 0 || file != 8) return fail("FEN board does not have eight rows");
    if (whiteKings != 1 || blackKings != 1) return fail("FEN must contain one king per side");

    if (fields[1] == "w") {
        sideToMove_ = Color::White;
    } else if (fields[1] == "b") {
        sideToMove_ = Color::Black;
    } else {
        return fail("invalid FEN active color");
    }

    castlingRights_ = 0;
    if (fields[2] != "-") {
        for (char value : fields[2]) {
            switch (value) {
                case 'K': castlingRights_ |= WhiteKingSide; break;
                case 'Q': castlingRights_ |= WhiteQueenSide; break;
                case 'k': castlingRights_ |= BlackKingSide; break;
                case 'q': castlingRights_ |= BlackQueenSide; break;
                default: return fail("invalid FEN castling rights");
            }
        }
    }

    if (fields[3] == "-") {
        enPassantSquare_ = -1;
    } else {
        const int square = parseSquare(fields[3]);
        if (square < 0 || (rankOf(square) != 2 && rankOf(square) != 5)) {
            return fail("invalid FEN en-passant square");
        }
        enPassantSquare_ = static_cast<std::int8_t>(square);
    }

    if (!parseUnsigned(fields[4], halfmoveClock_)) {
        return fail("invalid FEN halfmove clock");
    }
    if (!parseUnsigned(fields[5], fullmoveNumber_) || fullmoveNumber_ == 0) {
        return fail("invalid FEN fullmove number");
    }

    computeKey();
    pushRepetitionKey();
    return true;
}

std::string Position::toFen() const {
    std::ostringstream output;
    for (int rank = 7; rank >= 0; --rank) {
        int empty = 0;
        for (int file = 0; file < 8; ++file) {
            const Piece piece = board_[static_cast<std::size_t>(rank * 8 + file)];
            if (piece == 0) {
                ++empty;
            } else {
                if (empty > 0) {
                    output << empty;
                    empty = 0;
                }
                output << pieceToFen(piece);
            }
        }
        if (empty > 0) output << empty;
        if (rank != 0) output << '/';
    }
    output << (sideToMove_ == Color::White ? " w " : " b ");
    if (castlingRights_ == 0) {
        output << '-';
    } else {
        if ((castlingRights_ & WhiteKingSide) != 0U) output << 'K';
        if ((castlingRights_ & WhiteQueenSide) != 0U) output << 'Q';
        if ((castlingRights_ & BlackKingSide) != 0U) output << 'k';
        if ((castlingRights_ & BlackQueenSide) != 0U) output << 'q';
    }
    output << ' ';
    if (enPassantSquare_ >= 0) {
        output << squareName(enPassantSquare_);
    } else {
        output << '-';
    }
    output << ' ' << halfmoveClock_ << ' ' << fullmoveNumber_;
    return output.str();
}

Piece Position::pieceAt(int square) const {
    return onBoard(square) ? board_[static_cast<std::size_t>(square)] : 0;
}

void Position::addPawnMove(MoveList& list, int from, int to, std::uint8_t flags) const {
    const int promotionRank = sideToMove_ == Color::White ? 7 : 0;
    if (rankOf(to) == promotionRank) {
        constexpr std::array<PieceType, 4> promotions = {
            PieceType::Queen, PieceType::Rook, PieceType::Bishop, PieceType::Knight};
        for (PieceType promotion : promotions) {
            list.push(Move{static_cast<std::uint8_t>(from), static_cast<std::uint8_t>(to),
                           promotion, static_cast<std::uint8_t>(flags | MovePromotion)});
        }
    } else {
        list.push(Move{static_cast<std::uint8_t>(from), static_cast<std::uint8_t>(to),
                       PieceType::None, flags});
    }
}

void Position::generatePseudoMoves(MoveList& list) const {
    list.clear();
    const Color us = sideToMove_;
    const Color them = opposite(us);

    constexpr std::array<std::array<int, 2>, 8> knightSteps = {{{1, 2}, {2, 1},
                                                                 {2, -1}, {1, -2},
                                                                 {-1, -2}, {-2, -1},
                                                                 {-2, 1}, {-1, 2}}};
    constexpr std::array<std::array<int, 2>, 8> kingSteps = {{{1, 0}, {1, 1},
                                                               {0, 1}, {-1, 1},
                                                               {-1, 0}, {-1, -1},
                                                               {0, -1}, {1, -1}}};
    constexpr std::array<std::array<int, 2>, 4> bishopDirs =
        {{{1, 1}, {-1, 1}, {-1, -1}, {1, -1}}};
    constexpr std::array<std::array<int, 2>, 4> rookDirs =
        {{{1, 0}, {0, 1}, {-1, 0}, {0, -1}}};

    for (int from = 0; from < 64; ++from) {
        const Piece piece = board_[static_cast<std::size_t>(from)];
        if (piece == 0 || pieceColor(piece) != us) continue;
        const int file = fileOf(from);
        const int rank = rankOf(from);

        if (pieceType(piece) == PieceType::Pawn) {
            const int direction = us == Color::White ? 1 : -1;
            const int startRank = us == Color::White ? 1 : 6;
            const int oneRank = rank + direction;
            if (oneRank >= 0 && oneRank < 8) {
                const int one = oneRank * 8 + file;
                if (board_[static_cast<std::size_t>(one)] == 0) {
                    addPawnMove(list, from, one, MoveQuiet);
                    const int twoRank = rank + direction * 2;
                    const int two = twoRank * 8 + file;
                    if (rank == startRank && board_[static_cast<std::size_t>(two)] == 0) {
                        list.push(Move{static_cast<std::uint8_t>(from),
                                       static_cast<std::uint8_t>(two), PieceType::None,
                                       MoveDoublePawn});
                    }
                }
                for (int deltaFile : {-1, 1}) {
                    const int targetFile = file + deltaFile;
                    if (targetFile < 0 || targetFile >= 8) continue;
                    const int to = oneRank * 8 + targetFile;
                    const Piece target = board_[static_cast<std::size_t>(to)];
                    if (target != 0 && pieceColor(target) == them) {
                        addPawnMove(list, from, to, MoveCapture);
                    } else if (to == enPassantSquare_) {
                        const int capturedSquare =
                            us == Color::White ? to - 8 : to + 8;
                        if (board_[static_cast<std::size_t>(capturedSquare)] !=
                            makePiece(them, PieceType::Pawn)) {
                            continue;
                        }
                        list.push(Move{static_cast<std::uint8_t>(from),
                                       static_cast<std::uint8_t>(to), PieceType::None,
                                       static_cast<std::uint8_t>(MoveCapture | MoveEnPassant)});
                    }
                }
            }
            continue;
        }

        if (pieceType(piece) == PieceType::Knight) {
            for (const auto& step : knightSteps) {
                const int targetFile = file + step[0];
                const int targetRank = rank + step[1];
                if (targetFile < 0 || targetFile >= 8 || targetRank < 0 || targetRank >= 8)
                    continue;
                const int to = targetRank * 8 + targetFile;
                const Piece target = board_[static_cast<std::size_t>(to)];
                if (target == 0 || pieceColor(target) != us) {
                    list.push(Move{static_cast<std::uint8_t>(from),
                                   static_cast<std::uint8_t>(to), PieceType::None,
                                   target == 0 ? MoveQuiet : MoveCapture});
                }
            }
            continue;
        }

        if (pieceType(piece) == PieceType::King) {
            for (const auto& step : kingSteps) {
                const int targetFile = file + step[0];
                const int targetRank = rank + step[1];
                if (targetFile < 0 || targetFile >= 8 || targetRank < 0 || targetRank >= 8)
                    continue;
                const int to = targetRank * 8 + targetFile;
                const Piece target = board_[static_cast<std::size_t>(to)];
                if (target == 0 || pieceColor(target) != us) {
                    list.push(Move{static_cast<std::uint8_t>(from),
                                   static_cast<std::uint8_t>(to), PieceType::None,
                                   target == 0 ? MoveQuiet : MoveCapture});
                }
            }

            if (us == Color::White && from == 4) {
                if ((castlingRights_ & WhiteKingSide) != 0U && board_[5] == 0 &&
                    board_[6] == 0 && board_[7] == makePiece(Color::White, PieceType::Rook) &&
                    !isSquareAttacked(4, them) && !isSquareAttacked(5, them) &&
                    !isSquareAttacked(6, them)) {
                    list.push(Move{4, 6, PieceType::None, MoveKingCastle});
                }
                if ((castlingRights_ & WhiteQueenSide) != 0U && board_[1] == 0 &&
                    board_[2] == 0 && board_[3] == 0 &&
                    board_[0] == makePiece(Color::White, PieceType::Rook) &&
                    !isSquareAttacked(4, them) && !isSquareAttacked(3, them) &&
                    !isSquareAttacked(2, them)) {
                    list.push(Move{4, 2, PieceType::None, MoveQueenCastle});
                }
            } else if (us == Color::Black && from == 60) {
                if ((castlingRights_ & BlackKingSide) != 0U && board_[61] == 0 &&
                    board_[62] == 0 &&
                    board_[63] == makePiece(Color::Black, PieceType::Rook) &&
                    !isSquareAttacked(60, them) && !isSquareAttacked(61, them) &&
                    !isSquareAttacked(62, them)) {
                    list.push(Move{60, 62, PieceType::None, MoveKingCastle});
                }
                if ((castlingRights_ & BlackQueenSide) != 0U && board_[57] == 0 &&
                    board_[58] == 0 && board_[59] == 0 &&
                    board_[56] == makePiece(Color::Black, PieceType::Rook) &&
                    !isSquareAttacked(60, them) && !isSquareAttacked(59, them) &&
                    !isSquareAttacked(58, them)) {
                    list.push(Move{60, 58, PieceType::None, MoveQueenCastle});
                }
            }
            continue;
        }

        auto addSliding = [&](const auto& directions) {
            for (const auto& direction : directions) {
                int targetFile = file + direction[0];
                int targetRank = rank + direction[1];
                while (targetFile >= 0 && targetFile < 8 && targetRank >= 0 &&
                       targetRank < 8) {
                    const int to = targetRank * 8 + targetFile;
                    const Piece target = board_[static_cast<std::size_t>(to)];
                    if (target == 0) {
                        list.push(Move{static_cast<std::uint8_t>(from),
                                       static_cast<std::uint8_t>(to), PieceType::None,
                                       MoveQuiet});
                    } else {
                        if (pieceColor(target) != us) {
                            list.push(Move{static_cast<std::uint8_t>(from),
                                           static_cast<std::uint8_t>(to), PieceType::None,
                                           MoveCapture});
                        }
                        break;
                    }
                    targetFile += direction[0];
                    targetRank += direction[1];
                }
            }
        };

        if (pieceType(piece) == PieceType::Bishop) {
            addSliding(bishopDirs);
        } else if (pieceType(piece) == PieceType::Rook) {
            addSliding(rookDirs);
        } else if (pieceType(piece) == PieceType::Queen) {
            addSliding(bishopDirs);
            addSliding(rookDirs);
        }
    }
}

void Position::generateLegalMoves(MoveList& list) {
    MoveList pseudo;
    generatePseudoMoves(pseudo);
    list.clear();
    for (std::uint16_t index = 0; index < pseudo.size; ++index) {
        Undo undo;
        if (makeMove(pseudo[index], undo)) {
            list.push(pseudo[index]);
            unmakeMove(pseudo[index], undo);
        }
    }
}

bool Position::makeMove(const Move& move, Undo& undo) {
    if (!onBoard(move.from) || !onBoard(move.to)) return false;
    const Piece moving = board_[move.from];
    if (moving == 0 || pieceColor(moving) != sideToMove_) return false;

    undo.castlingRights = castlingRights_;
    undo.enPassantSquare = enPassantSquare_;
    undo.halfmoveClock = halfmoveClock_;
    undo.fullmoveNumber = fullmoveNumber_;
    undo.kingSquares = kingSquares_;
    undo.key = key_;

    const auto& hash = zobrist();
    const int oldEnPassantFile = legalEnPassantFile();
    key_ ^= hash.castling[castlingRights_];
    if (oldEnPassantFile >= 0) {
        key_ ^= hash.enPassantFile[static_cast<std::size_t>(oldEnPassantFile)];
    }

    int capturedSquare = move.to;
    if ((move.flags & MoveEnPassant) != 0U) {
        capturedSquare = sideToMove_ == Color::White ? move.to - 8 : move.to + 8;
    }
    undo.captured = board_[static_cast<std::size_t>(capturedSquare)];

    key_ ^= hash.pieces[static_cast<std::size_t>(pieceIndex(moving))][move.from];
    board_[move.from] = 0;
    if (undo.captured != 0) {
        key_ ^= hash.pieces[static_cast<std::size_t>(pieceIndex(undo.captured))]
                           [static_cast<std::size_t>(capturedSquare)];
        board_[static_cast<std::size_t>(capturedSquare)] = 0;
    }

    Piece placed = moving;
    if ((move.flags & MovePromotion) != 0U) {
        if (pieceType(moving) != PieceType::Pawn || move.promotion == PieceType::None ||
            move.promotion == PieceType::Pawn || move.promotion == PieceType::King) {
            board_[move.from] = moving;
            board_[static_cast<std::size_t>(capturedSquare)] = undo.captured;
            key_ = undo.key;
            return false;
        }
        placed = makePiece(sideToMove_, move.promotion);
    }
    board_[move.to] = placed;
    key_ ^= hash.pieces[static_cast<std::size_t>(pieceIndex(placed))][move.to];

    if ((move.flags & MoveKingCastle) != 0U) {
        const int rookFrom = sideToMove_ == Color::White ? 7 : 63;
        const int rookTo = sideToMove_ == Color::White ? 5 : 61;
        const Piece rook = board_[static_cast<std::size_t>(rookFrom)];
        board_[static_cast<std::size_t>(rookFrom)] = 0;
        board_[static_cast<std::size_t>(rookTo)] = rook;
        key_ ^= hash.pieces[static_cast<std::size_t>(pieceIndex(rook))]
                           [static_cast<std::size_t>(rookFrom)];
        key_ ^= hash.pieces[static_cast<std::size_t>(pieceIndex(rook))]
                           [static_cast<std::size_t>(rookTo)];
    } else if ((move.flags & MoveQueenCastle) != 0U) {
        const int rookFrom = sideToMove_ == Color::White ? 0 : 56;
        const int rookTo = sideToMove_ == Color::White ? 3 : 59;
        const Piece rook = board_[static_cast<std::size_t>(rookFrom)];
        board_[static_cast<std::size_t>(rookFrom)] = 0;
        board_[static_cast<std::size_t>(rookTo)] = rook;
        key_ ^= hash.pieces[static_cast<std::size_t>(pieceIndex(rook))]
                           [static_cast<std::size_t>(rookFrom)];
        key_ ^= hash.pieces[static_cast<std::size_t>(pieceIndex(rook))]
                           [static_cast<std::size_t>(rookTo)];
    }

    if (pieceType(moving) == PieceType::King) {
        kingSquares_[static_cast<std::size_t>(colorIndex(sideToMove_))] = move.to;
        if (sideToMove_ == Color::White) {
            castlingRights_ &= static_cast<std::uint8_t>(~(WhiteKingSide | WhiteQueenSide));
        } else {
            castlingRights_ &= static_cast<std::uint8_t>(~(BlackKingSide | BlackQueenSide));
        }
    }
    if (pieceType(moving) == PieceType::Rook) {
        if (move.from == 0) castlingRights_ &= static_cast<std::uint8_t>(~WhiteQueenSide);
        if (move.from == 7) castlingRights_ &= static_cast<std::uint8_t>(~WhiteKingSide);
        if (move.from == 56) castlingRights_ &= static_cast<std::uint8_t>(~BlackQueenSide);
        if (move.from == 63) castlingRights_ &= static_cast<std::uint8_t>(~BlackKingSide);
    }
    if (undo.captured != 0 && pieceType(undo.captured) == PieceType::Rook) {
        if (capturedSquare == 0)
            castlingRights_ &= static_cast<std::uint8_t>(~WhiteQueenSide);
        if (capturedSquare == 7)
            castlingRights_ &= static_cast<std::uint8_t>(~WhiteKingSide);
        if (capturedSquare == 56)
            castlingRights_ &= static_cast<std::uint8_t>(~BlackQueenSide);
        if (capturedSquare == 63)
            castlingRights_ &= static_cast<std::uint8_t>(~BlackKingSide);
    }

    enPassantSquare_ = -1;
    if ((move.flags & MoveDoublePawn) != 0U) {
        enPassantSquare_ = static_cast<std::int8_t>((move.from + move.to) / 2);
    }

    if (pieceType(moving) == PieceType::Pawn || undo.captured != 0) {
        halfmoveClock_ = 0;
    } else {
        ++halfmoveClock_;
    }
    const Color movedColor = sideToMove_;
    if (sideToMove_ == Color::Black) ++fullmoveNumber_;
    sideToMove_ = opposite(sideToMove_);
    key_ ^= hash.side;
    key_ ^= hash.castling[castlingRights_];
    const int newEnPassantFile = legalEnPassantFile();
    if (newEnPassantFile >= 0) {
        key_ ^= hash.enPassantFile[static_cast<std::size_t>(newEnPassantFile)];
    }
    pushRepetitionKey();

    if (inCheck(movedColor)) {
        unmakeMove(move, undo);
        return false;
    }
    return true;
}

void Position::unmakeMove(const Move& move, const Undo& undo) {
    popRepetitionKey();
    sideToMove_ = opposite(sideToMove_);

    Piece moved = board_[move.to];
    if ((move.flags & MoveKingCastle) != 0U) {
        const int rookFrom = sideToMove_ == Color::White ? 7 : 63;
        const int rookTo = sideToMove_ == Color::White ? 5 : 61;
        board_[static_cast<std::size_t>(rookFrom)] = board_[static_cast<std::size_t>(rookTo)];
        board_[static_cast<std::size_t>(rookTo)] = 0;
    } else if ((move.flags & MoveQueenCastle) != 0U) {
        const int rookFrom = sideToMove_ == Color::White ? 0 : 56;
        const int rookTo = sideToMove_ == Color::White ? 3 : 59;
        board_[static_cast<std::size_t>(rookFrom)] = board_[static_cast<std::size_t>(rookTo)];
        board_[static_cast<std::size_t>(rookTo)] = 0;
    }

    if ((move.flags & MovePromotion) != 0U) {
        moved = makePiece(sideToMove_, PieceType::Pawn);
    }
    board_[move.from] = moved;
    if ((move.flags & MoveEnPassant) != 0U) {
        board_[move.to] = 0;
        const int capturedSquare = sideToMove_ == Color::White ? move.to - 8 : move.to + 8;
        board_[static_cast<std::size_t>(capturedSquare)] = undo.captured;
    } else {
        board_[move.to] = undo.captured;
    }

    castlingRights_ = undo.castlingRights;
    enPassantSquare_ = undo.enPassantSquare;
    halfmoveClock_ = undo.halfmoveClock;
    fullmoveNumber_ = undo.fullmoveNumber;
    kingSquares_ = undo.kingSquares;
    key_ = undo.key;
}

void Position::makeNullMove(Undo& undo) {
    undo.captured = 0;
    undo.castlingRights = castlingRights_;
    undo.enPassantSquare = enPassantSquare_;
    undo.halfmoveClock = halfmoveClock_;
    undo.fullmoveNumber = fullmoveNumber_;
    undo.kingSquares = kingSquares_;
    undo.key = key_;
    const auto& hash = zobrist();
    const int oldEnPassantFile = legalEnPassantFile();
    if (oldEnPassantFile >= 0) {
        key_ ^= hash.enPassantFile[static_cast<std::size_t>(oldEnPassantFile)];
    }
    enPassantSquare_ = -1;
    ++halfmoveClock_;
    if (sideToMove_ == Color::Black) ++fullmoveNumber_;
    sideToMove_ = opposite(sideToMove_);
    key_ ^= hash.side;
    pushRepetitionKey();
}

void Position::unmakeNullMove(const Undo& undo) {
    popRepetitionKey();
    sideToMove_ = opposite(sideToMove_);
    castlingRights_ = undo.castlingRights;
    enPassantSquare_ = undo.enPassantSquare;
    halfmoveClock_ = undo.halfmoveClock;
    fullmoveNumber_ = undo.fullmoveNumber;
    kingSquares_ = undo.kingSquares;
    key_ = undo.key;
}

bool Position::isSquareAttacked(int square, Color byColor) const {
    if (!onBoard(square)) return false;
    const int file = fileOf(square);
    const int rank = rankOf(square);

    const int pawnSourceRank = rank + (byColor == Color::White ? -1 : 1);
    if (pawnSourceRank >= 0 && pawnSourceRank < 8) {
        for (int deltaFile : {-1, 1}) {
            const int sourceFile = file + deltaFile;
            if (sourceFile >= 0 && sourceFile < 8 &&
                board_[static_cast<std::size_t>(pawnSourceRank * 8 + sourceFile)] ==
                    makePiece(byColor, PieceType::Pawn)) {
                return true;
            }
        }
    }

    constexpr std::array<std::array<int, 2>, 8> knightSteps = {{{1, 2}, {2, 1},
                                                                 {2, -1}, {1, -2},
                                                                 {-1, -2}, {-2, -1},
                                                                 {-2, 1}, {-1, 2}}};
    for (const auto& step : knightSteps) {
        const int sourceFile = file + step[0];
        const int sourceRank = rank + step[1];
        if (sourceFile >= 0 && sourceFile < 8 && sourceRank >= 0 && sourceRank < 8 &&
            board_[static_cast<std::size_t>(sourceRank * 8 + sourceFile)] ==
                makePiece(byColor, PieceType::Knight)) {
            return true;
        }
    }

    constexpr std::array<std::array<int, 2>, 8> directions = {{{1, 0}, {1, 1},
                                                                 {0, 1}, {-1, 1},
                                                                 {-1, 0}, {-1, -1},
                                                                 {0, -1}, {1, -1}}};
    for (std::size_t directionIndex = 0; directionIndex < directions.size();
         ++directionIndex) {
        const auto& direction = directions[directionIndex];
        int sourceFile = file + direction[0];
        int sourceRank = rank + direction[1];
        int distance = 1;
        while (sourceFile >= 0 && sourceFile < 8 && sourceRank >= 0 && sourceRank < 8) {
            const Piece piece = board_[static_cast<std::size_t>(sourceRank * 8 + sourceFile)];
            if (piece != 0) {
                if (pieceColor(piece) == byColor) {
                    const PieceType type = pieceType(piece);
                    const bool diagonal = (directionIndex & 1U) != 0U;
                    if (type == PieceType::Queen ||
                        (diagonal && type == PieceType::Bishop) ||
                        (!diagonal && type == PieceType::Rook) ||
                        (distance == 1 && type == PieceType::King)) {
                        return true;
                    }
                }
                break;
            }
            sourceFile += direction[0];
            sourceRank += direction[1];
            ++distance;
        }
    }
    return false;
}

bool Position::inCheck(Color color) const {
    return isSquareAttacked(kingSquares_[static_cast<std::size_t>(colorIndex(color))],
                            opposite(color));
}

bool Position::isRepetition(int requiredOccurrences) const {
    if (requiredOccurrences <= 1) return true;
    int count = 0;
    const int reversible = std::min<int>(halfmoveClock_, repetitionCount_ - 1);
    const int begin = static_cast<int>(repetitionCount_) - 1 - reversible;
    for (int index = static_cast<int>(repetitionCount_) - 1; index >= begin; index -= 2) {
        if (repetitionKeys_[static_cast<std::size_t>(index)] == key_ &&
            ++count >= requiredOccurrences) {
            return true;
        }
    }
    return false;
}

bool Position::isInsufficientMaterial() const {
    int knights = 0;
    int bishops = 0;
    int bishopColor = -1;
    bool bishopsSameColor = true;
    for (int square = 0; square < 64; ++square) {
        const Piece piece = board_[static_cast<std::size_t>(square)];
        switch (pieceType(piece)) {
            case PieceType::None:
            case PieceType::King: break;
            case PieceType::Knight: ++knights; break;
            case PieceType::Bishop: {
                ++bishops;
                const int color = (fileOf(square) + rankOf(square)) & 1;
                if (bishopColor < 0) bishopColor = color;
                if (bishopColor != color) bishopsSameColor = false;
                break;
            }
            default: return false;
        }
    }
    if (knights + bishops <= 1) return true;
    return knights == 0 && bishopsSameColor;
}

bool Position::hasNonPawnMaterial(Color color) const {
    for (Piece piece : board_) {
        if (piece != 0 && pieceColor(piece) == color) {
            const PieceType type = pieceType(piece);
            if (type == PieceType::Knight || type == PieceType::Bishop ||
                type == PieceType::Rook || type == PieceType::Queen) {
                return true;
            }
        }
    }
    return false;
}

GameState Position::gameState() {
    MoveList moves;
    generateLegalMoves(moves);
    if (moves.size == 0) {
        if (inCheck(sideToMove_)) {
            return sideToMove_ == Color::White ? GameState::BlackWinsCheckmate
                                                : GameState::WhiteWinsCheckmate;
        }
        return GameState::Stalemate;
    }
    if (isFiftyMoveDraw()) return GameState::DrawFiftyMove;
    if (isRepetition(3)) return GameState::DrawThreefold;
    if (isInsufficientMaterial()) return GameState::DrawInsufficientMaterial;
    return GameState::Ongoing;
}

std::string Position::squareName(int square) {
    if (!onBoard(square)) return "--";
    std::string result(2, ' ');
    result[0] = static_cast<char>('a' + fileOf(square));
    result[1] = static_cast<char>('1' + rankOf(square));
    return result;
}

int Position::parseSquare(std::string_view name) {
    if (name.size() != 2 || name[0] < 'a' || name[0] > 'h' || name[1] < '1' ||
        name[1] > '8') {
        return -1;
    }
    return (name[1] - '1') * 8 + (name[0] - 'a');
}

std::string Position::moveToUci(const Move& move) {
    std::string result = squareName(move.from) + squareName(move.to);
    if (move.isPromotion()) {
        char promotion = 'q';
        if (move.promotion == PieceType::Rook) promotion = 'r';
        if (move.promotion == PieceType::Bishop) promotion = 'b';
        if (move.promotion == PieceType::Knight) promotion = 'n';
        result.push_back(promotion);
    }
    return result;
}

std::string Position::moveToSan(const Move& requested) {
    MoveList legalMoves;
    generateLegalMoves(legalMoves);
    Move move{};
    bool found = false;
    for (std::uint16_t index = 0; index < legalMoves.size; ++index) {
        if (legalMoves[index] == requested) {
            move = legalMoves[index];
            found = true;
            break;
        }
    }
    if (!found) return moveToUci(requested);

    std::string result;
    if ((move.flags & MoveKingCastle) != 0U) {
        result = "O-O";
    } else if ((move.flags & MoveQueenCastle) != 0U) {
        result = "O-O-O";
    } else {
        const Piece moving = board_[move.from];
        const PieceType type = pieceType(moving);
        if (type != PieceType::Pawn) {
            char letter = 'N';
            if (type == PieceType::Bishop) letter = 'B';
            if (type == PieceType::Rook) letter = 'R';
            if (type == PieceType::Queen) letter = 'Q';
            if (type == PieceType::King) letter = 'K';
            result.push_back(letter);

            bool ambiguous = false;
            bool sharesFile = false;
            bool sharesRank = false;
            for (std::uint16_t index = 0; index < legalMoves.size; ++index) {
                const Move other = legalMoves[index];
                if (other.from == move.from || other.to != move.to ||
                    pieceType(board_[other.from]) != type) {
                    continue;
                }
                ambiguous = true;
                sharesFile = sharesFile || fileOf(other.from) == fileOf(move.from);
                sharesRank = sharesRank || rankOf(other.from) == rankOf(move.from);
            }
            if (ambiguous) {
                if (!sharesFile) {
                    result.push_back(static_cast<char>('a' + fileOf(move.from)));
                } else if (!sharesRank) {
                    result.push_back(static_cast<char>('1' + rankOf(move.from)));
                } else {
                    result += squareName(move.from);
                }
            }
        } else if (move.isCapture()) {
            result.push_back(static_cast<char>('a' + fileOf(move.from)));
        }

        if (move.isCapture()) result.push_back('x');
        result += squareName(move.to);
        if (move.isPromotion()) {
            result.push_back('=');
            char promotion = 'Q';
            if (move.promotion == PieceType::Rook) promotion = 'R';
            if (move.promotion == PieceType::Bishop) promotion = 'B';
            if (move.promotion == PieceType::Knight) promotion = 'N';
            result.push_back(promotion);
        }
    }

    Undo undo;
    if (!makeMove(move, undo)) return moveToUci(move);
    if (inCheck(sideToMove_)) {
        MoveList replies;
        generateLegalMoves(replies);
        result.push_back(replies.size == 0 ? '#' : '+');
    }
    unmakeMove(move, undo);
    return result;
}

bool Position::parseUciMove(std::string_view text, Move& result) {
    if (text.size() != 4 && text.size() != 5) return false;
    const int from = parseSquare(text.substr(0, 2));
    const int to = parseSquare(text.substr(2, 2));
    if (from < 0 || to < 0) return false;
    PieceType promotion = PieceType::None;
    if (text.size() == 5) {
        switch (static_cast<char>(std::tolower(static_cast<unsigned char>(text[4])))) {
            case 'q': promotion = PieceType::Queen; break;
            case 'r': promotion = PieceType::Rook; break;
            case 'b': promotion = PieceType::Bishop; break;
            case 'n': promotion = PieceType::Knight; break;
            default: return false;
        }
    }
    MoveList moves;
    generateLegalMoves(moves);
    for (std::uint16_t index = 0; index < moves.size; ++index) {
        const Move move = moves[index];
        if (move.from == from && move.to == to && move.promotion == promotion) {
            result = move;
            return true;
        }
    }
    return false;
}

void Position::computeKey() {
    const auto& hash = zobrist();
    key_ = 0;
    for (int square = 0; square < 64; ++square) {
        const Piece piece = board_[static_cast<std::size_t>(square)];
        if (piece != 0) {
            key_ ^= hash.pieces[static_cast<std::size_t>(pieceIndex(piece))]
                               [static_cast<std::size_t>(square)];
        }
    }
    key_ ^= hash.castling[castlingRights_];
    const int enPassantFile = legalEnPassantFile();
    if (enPassantFile >= 0) {
        key_ ^= hash.enPassantFile[static_cast<std::size_t>(enPassantFile)];
    }
    if (sideToMove_ == Color::Black) key_ ^= hash.side;
}

int Position::legalEnPassantFile() {
    if (enPassantSquare_ < 0) return -1;
    const int target = enPassantSquare_;
    const int targetFile = fileOf(target);
    const int direction = sideToMove_ == Color::White ? 1 : -1;
    const int sourceRank = rankOf(target) - direction;
    const int capturedSquare = target - direction * 8;
    if (!onBoard(capturedSquare) || board_[static_cast<std::size_t>(target)] != 0 ||
        board_[static_cast<std::size_t>(capturedSquare)] !=
            makePiece(opposite(sideToMove_), PieceType::Pawn)) {
        return -1;
    }

    for (const int fileDelta : {-1, 1}) {
        const int sourceFile = targetFile + fileDelta;
        if (sourceFile < 0 || sourceFile >= 8 || sourceRank < 0 || sourceRank >= 8) {
            continue;
        }
        const int source = sourceRank * 8 + sourceFile;
        const Piece pawn = board_[static_cast<std::size_t>(source)];
        if (pawn != makePiece(sideToMove_, PieceType::Pawn)) continue;

        const Piece captured = board_[static_cast<std::size_t>(capturedSquare)];
        board_[static_cast<std::size_t>(source)] = 0;
        board_[static_cast<std::size_t>(capturedSquare)] = 0;
        board_[static_cast<std::size_t>(target)] = pawn;
        const bool legal = !inCheck(sideToMove_);
        board_[static_cast<std::size_t>(target)] = 0;
        board_[static_cast<std::size_t>(capturedSquare)] = captured;
        board_[static_cast<std::size_t>(source)] = pawn;
        if (legal) return targetFile;
    }
    return -1;
}

void Position::pushRepetitionKey() {
    if (repetitionCount_ < repetitionKeys_.size()) {
        repetitionKeys_[repetitionCount_++] = key_;
    }
}

void Position::popRepetitionKey() {
    if (repetitionCount_ > 0) --repetitionCount_;
}

std::uint64_t perft(Position& position, int depth) {
    if (depth <= 0) return 1;
    MoveList moves;
    position.generateLegalMoves(moves);
    if (depth == 1) return moves.size;
    std::uint64_t nodes = 0;
    for (std::uint16_t index = 0; index < moves.size; ++index) {
        Undo undo;
        if (!position.makeMove(moves[index], undo)) continue;
        nodes += perft(position, depth - 1);
        position.unmakeMove(moves[index], undo);
    }
    return nodes;
}

}  // namespace cardputer_chess
