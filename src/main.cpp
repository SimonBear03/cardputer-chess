#ifdef ARDUINO

#include <Arduino.h>
#include <M5Cardputer.h>
#include <Preferences.h>
#include <esp_system.h>

#include "cardputer_chess/chess.hpp"
#include "cardputer_chess/coach.hpp"
#include "cardputer_chess/engine.hpp"
#include "cardputer_chess/piece_glyphs.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>

using namespace cardputer_chess;

namespace {

constexpr int kBoardX = 8;
constexpr int kBoardY = 1;
constexpr int kSquarePixels = 15;
constexpr int kPanelX = 133;
constexpr int kPanelWidth = 107;
constexpr std::uint32_t kAnimationFrameMs = 42;

constexpr std::uint16_t rgb565(std::uint8_t red, std::uint8_t green,
                               std::uint8_t blue) {
    return static_cast<std::uint16_t>(((red & 0xF8U) << 8U) |
                                      ((green & 0xFCU) << 3U) | (blue >> 3U));
}

enum class Screen : std::uint8_t { Setup, Playing, Promotion, Coach, Pause, GameOver };
enum class Action : std::uint8_t {
    None,
    Up,
    Down,
    Left,
    Right,
    Confirm,
    Cancel,
    Menu,
    Undo,
    Coach,
};
enum class SideChoice : std::uint8_t { White, Black, Random };
enum class ThemeMode : std::uint8_t { Classic, Neon, Royal };
enum class SearchPurpose : std::uint8_t { Opponent, Coach };
enum class AnimationKind : std::uint8_t {
    None,
    GameIntro,
    MoveFeedback,
    Win,
    Loss,
    Draw,
};

struct ThemePalette {
    const char* name;
    std::uint16_t background;
    std::uint16_t surface;
    std::uint16_t surfaceStrong;
    std::uint16_t boardLight;
    std::uint16_t boardDark;
    std::uint16_t selected;
    std::uint16_t legal;
    std::uint16_t lastMove;
    std::uint16_t check;
    std::uint16_t accent;
    std::uint16_t secondary;
    std::uint16_t text;
    std::uint16_t muted;
    std::uint16_t whitePiece;
    std::uint16_t whiteEdge;
    std::uint16_t blackPiece;
    std::uint16_t blackEdge;
};

constexpr std::array<ThemePalette, 3> kThemes = {{
    {"Classic", rgb565(4, 20, 38), rgb565(9, 36, 58), rgb565(17, 55, 76),
     rgb565(238, 226, 190), rgb565(65, 130, 126), rgb565(212, 162, 42),
     rgb565(74, 207, 211), rgb565(113, 158, 142), rgb565(196, 55, 62),
     rgb565(231, 177, 62), rgb565(66, 205, 221), rgb565(246, 239, 216),
     rgb565(139, 165, 174), rgb565(250, 239, 205), rgb565(45, 46, 44),
     rgb565(26, 31, 35), rgb565(232, 205, 135)},
    {"Neon", rgb565(3, 5, 28), rgb565(15, 13, 55), rgb565(25, 27, 79),
     rgb565(28, 137, 156), rgb565(37, 18, 93), rgb565(236, 61, 151),
     rgb565(111, 238, 205), rgb565(49, 177, 178), rgb565(255, 77, 118),
     rgb565(126, 238, 212), rgb565(171, 82, 245), rgb565(255, 244, 206),
     rgb565(113, 190, 194), rgb565(255, 244, 206), rgb565(56, 24, 91),
     rgb565(21, 10, 49), rgb565(174, 104, 242)},
    {"Royal", rgb565(20, 10, 26), rgb565(45, 20, 41), rgb565(77, 32, 48),
     rgb565(242, 224, 188), rgb565(123, 39, 60), rgb565(211, 161, 73),
     rgb565(85, 196, 153), rgb565(174, 91, 89), rgb565(220, 58, 65),
     rgb565(226, 177, 88), rgb565(188, 118, 164), rgb565(250, 237, 210),
     rgb565(164, 135, 151), rgb565(252, 239, 208), rgb565(54, 26, 42),
     rgb565(42, 18, 37), rgb565(226, 177, 88)},
}};

struct UiAnimation {
    AnimationKind kind = AnimationKind::None;
    std::uint32_t startMs = 0;
    std::uint32_t lastFrameMs = 0;
    std::uint32_t durationMs = 0;
    int square = -1;
    MoveQuality quality = MoveQuality::Unavailable;
    bool check = false;
};

struct GameRecord {
    Move move{};
    Undo undo{};
    std::array<char, 12> san{};
};

Preferences preferences;
Position game = Position::startPosition();
Engine engine(64);
std::array<GameRecord, kMaxGamePly> records{};
std::uint16_t recordCount = 0;

Screen screen = Screen::Setup;
SideChoice sideChoice = SideChoice::White;
Color humanColor = Color::White;
int levelIndex = 3;
CoachMode coachMode = CoachMode::OnDemand;
ThemeMode themeMode = ThemeMode::Classic;
int setupRow = 0;
int pauseRow = 0;
int cursorSquare = 12;
int selectedSquare = -1;
MoveList selectedMoves{};
int promotionIndex = 0;
GameState outcome = GameState::Ongoing;
Move lastMove{};
bool hasLastMove = false;
SearchResult lastSearch{};
SearchResult coachAnalysis{};
CoachFeedback coachFeedback{};
std::uint64_t coachAnalysisKey = 0;
std::uint8_t coachLineIndex = 0;

Position searchPosition = Position::startPosition();
Position notationPosition = Position::startPosition();
SearchResult taskResult{};
SearchLimits taskLimits{};
SearchPurpose searchPurpose = SearchPurpose::Opponent;
SearchPurpose taskResultPurpose = SearchPurpose::Opponent;
TaskHandle_t searchTaskHandle = nullptr;
portMUX_TYPE resultMutex = portMUX_INITIALIZER_UNLOCKED;
volatile bool searchRunning = false;
volatile bool searchDone = false;
bool discardSearch = false;
bool pendingUndo = false;
bool pendingOpponentSearch = false;
bool openCoachWhenReady = false;
bool engineLaunchFailed = false;
std::uint64_t gameSeed = 0;
std::uint64_t searchRootKey = 0;
Screen drawnScreen = Screen::Setup;
bool hasDrawnScreen = false;
UiAnimation animation{};

const ThemePalette& theme() {
    return kThemes[static_cast<std::size_t>(themeMode)];
}

const char* colorName(Color color) { return color == Color::White ? "White" : "Black"; }

const char* sideChoiceName(SideChoice choice) {
    switch (choice) {
        case SideChoice::White: return "White";
        case SideChoice::Black: return "Black";
        case SideChoice::Random: return "Random";
    }
    return "White";
}

bool opponentSearchRunning() {
    return searchRunning && searchPurpose == SearchPurpose::Opponent;
}

bool coachSearchRunning() {
    return searchRunning && searchPurpose == SearchPurpose::Coach;
}

bool coachAnalysisCurrent() {
    return coachAnalysis.hasMove && coachAnalysis.lineCount > 0 &&
           coachAnalysisKey == game.key() && game.sideToMove() == humanColor;
}

void cycleCoachMode(int direction) {
    int value = static_cast<int>(coachMode);
    value = (value + (direction < 0 ? 2 : 1)) % 3;
    coachMode = static_cast<CoachMode>(value);
}

void startAnimation(AnimationKind kind, std::uint32_t durationMs, int square = -1,
                    MoveQuality quality = MoveQuality::Unavailable,
                    bool check = false) {
    const std::uint32_t now = millis();
    animation = UiAnimation{kind, now, now, durationMs, square, quality, check};
}

void cycleTheme(int direction) {
    int value = static_cast<int>(themeMode);
    value = (value + (direction < 0 ? 2 : 1)) % 3;
    themeMode = static_cast<ThemeMode>(value);
    hasDrawnScreen = false;
    animation = UiAnimation{};
}

void formatScore(std::int16_t scoreCp, char* output, std::size_t outputSize) {
    const int score = scoreCp;
    if (score > 29000) {
        std::snprintf(output, outputSize, "+Mate");
        return;
    }
    if (score < -29000) {
        std::snprintf(output, outputSize, "-Mate");
        return;
    }
    const int absolute = score < 0 ? -score : score;
    std::snprintf(output, outputSize, "%c%d.%02d", score >= 0 ? '+' : '-',
                  absolute / 100, absolute % 100);
}

std::string principalVariationText(const AnalysisLine& line) {
    notationPosition = game;
    std::string text;
    const std::uint8_t length = std::min<std::uint8_t>(line.principalVariationLength, 4);
    for (std::uint8_t index = 0; index < length; ++index) {
        const Move move = line.principalVariation[index];
        const std::string san = notationPosition.moveToSan(move);
        if (!text.empty()) text.push_back(' ');
        if (text.size() + san.size() > 27) break;
        text += san;
        Undo undo;
        if (!notationPosition.makeMove(move, undo)) break;
    }
    return text;
}

const char* outcomeName(GameState state) {
    switch (state) {
        case GameState::WhiteWinsCheckmate: return "White wins";
        case GameState::BlackWinsCheckmate: return "Black wins";
        case GameState::Stalemate: return "Stalemate";
        case GameState::DrawFiftyMove: return "50-move draw";
        case GameState::DrawThreefold: return "Repetition";
        case GameState::DrawInsufficientMaterial: return "No material";
        case GameState::Ongoing: return "Playing";
    }
    return "Game over";
}

void drawText(int x, int y, const char* value, std::uint16_t color = TFT_WHITE,
              int size = 1) {
    M5Cardputer.Display.setTextSize(size);
    M5Cardputer.Display.setTextColor(color);
    M5Cardputer.Display.setCursor(x, y);
    M5Cardputer.Display.print(value);
}

int squareToColumn(int square) {
    return humanColor == Color::White ? (square & 7) : 7 - (square & 7);
}

int squareToRow(int square) {
    const int rank = square >> 3;
    return humanColor == Color::White ? 7 - rank : rank;
}

int screenToSquare(int column, int row) {
    const int file = humanColor == Color::White ? column : 7 - column;
    const int rank = humanColor == Color::White ? 7 - row : row;
    return rank * 8 + file;
}

bool isLegalDestination(int square) {
    if (selectedSquare < 0) return false;
    for (std::uint16_t index = 0; index < selectedMoves.size; ++index) {
        if (selectedMoves[index].to == square) return true;
    }
    return false;
}

bool isCheckedKing(int square) {
    const Piece piece = game.pieceAt(square);
    return pieceType(piece) == PieceType::King && pieceColor(piece) == game.sideToMove() &&
           game.inCheck(game.sideToMove());
}

void drawPiece(int x, int y, Piece piece) {
    if (piece == 0) return;
    const bool white = pieceColor(piece) == Color::White;
    const ThemePalette& colors = theme();
    const std::uint16_t body = white ? colors.whitePiece : colors.blackPiece;
    const std::uint16_t edge = white ? colors.whiteEdge : colors.blackEdge;
    const std::size_t glyphIndex = static_cast<std::size_t>(pieceType(piece)) - 1U;
    const ui::PieceGlyph& glyph = ui::kPieceGlyphs[glyphIndex];
    for (int row = 0; row < ui::kPieceGlyphPixels; ++row) {
        int runStart = -1;
        std::uint16_t runColor = 0;
        for (int column = 0; column <= ui::kPieceGlyphPixels; ++column) {
            bool occupied = false;
            std::uint16_t pixelColor = 0;
            if (column < ui::kPieceGlyphPixels) {
                const std::uint16_t bit = static_cast<std::uint16_t>(
                    1U << (ui::kPieceGlyphPixels - 1 - column));
                if ((glyph.edge[static_cast<std::size_t>(row)] & bit) != 0U) {
                    occupied = true;
                    pixelColor = edge;
                }
                if ((glyph.fill[static_cast<std::size_t>(row)] & bit) != 0U) {
                    occupied = true;
                    pixelColor = body;
                }
            }
            if (runStart >= 0 && (!occupied || pixelColor != runColor)) {
                M5Cardputer.Display.drawFastHLine(x + runStart + 1, y + row + 1,
                                                  column - runStart, runColor);
                runStart = -1;
            }
            if (occupied && runStart < 0) {
                runStart = column;
                runColor = pixelColor;
            }
        }
    }
}

void drawBoard() {
    const ThemePalette& colors = theme();
    for (int row = 0; row < 8; ++row) {
        for (int column = 0; column < 8; ++column) {
            const int square = screenToSquare(column, row);
            const int x = kBoardX + column * kSquarePixels;
            const int y = kBoardY + row * kSquarePixels;
            std::uint16_t color =
                ((column + row) & 1) == 0 ? colors.boardLight : colors.boardDark;
            if (hasLastMove && (square == lastMove.from || square == lastMove.to)) {
                color = colors.lastMove;
            }
            if (square == selectedSquare) color = colors.selected;
            if (isCheckedKing(square)) color = colors.check;
            M5Cardputer.Display.fillRect(x, y, kSquarePixels, kSquarePixels, color);
            if (isLegalDestination(square) && game.pieceAt(square) == 0) {
                M5Cardputer.Display.fillCircle(x + 7, y + 7, 2, colors.legal);
            } else if (isLegalDestination(square)) {
                M5Cardputer.Display.drawRect(x + 1, y + 1, kSquarePixels - 2,
                                             kSquarePixels - 2, colors.legal);
            }
            drawPiece(x, y, game.pieceAt(square));
            if (square == cursorSquare) {
                M5Cardputer.Display.drawRect(x, y, kSquarePixels, kSquarePixels,
                                             colors.secondary);
                M5Cardputer.Display.drawRect(x + 1, y + 1, kSquarePixels - 2,
                                             kSquarePixels - 2, colors.secondary);
            }
        }
    }
    const int framePixels = 8 * kSquarePixels + 2;
    M5Cardputer.Display.drawRect(kBoardX - 1, kBoardY - 1, framePixels, framePixels,
                                 colors.accent);
    for (int column = 0; column < 8; ++column) {
        char file[2] = {static_cast<char>(humanColor == Color::White ? 'a' + column
                                                                     : 'h' - column),
                        '\0'};
        drawText(kBoardX + column * kSquarePixels + 5, 124, file, colors.muted, 1);
    }
    for (int row = 0; row < 8; ++row) {
        char rank[2] = {static_cast<char>(humanColor == Color::White ? '8' - row
                                                                     : '1' + row),
                        '\0'};
        drawText(1, kBoardY + row * kSquarePixels + 4, rank, colors.muted, 1);
    }
}

void drawPanel() {
    const ThemePalette& colors = theme();
    M5Cardputer.Display.fillRect(kPanelX, 0, kPanelWidth, 135, colors.background);
    drawText(kPanelX + 4, 3, "CARD CHESS", colors.accent, 1);
    M5Cardputer.Display.drawFastHLine(kPanelX + 4, 13, kPanelWidth - 8, colors.surfaceStrong);

    char line[32];
    M5Cardputer.Display.fillRoundRect(kPanelX + 3, 17, kPanelWidth - 6, 16, 3,
                                      colors.surface);
    std::snprintf(line, sizeof(line), "%s TO MOVE", colorName(game.sideToMove()));
    drawText(kPanelX + 8, 21, line, colors.text, 1);
    std::snprintf(line, sizeof(line), "LV%d  %s", levelIndex + 1,
                  levelConfig(levelIndex).name);
    drawText(kPanelX + 4, 38, line, colors.accent, 1);

    if (opponentSearchRunning()) {
        drawText(kPanelX + 4, 50, "ENGINE THINKING", colors.secondary, 1);
    } else if (coachSearchRunning()) {
        drawText(kPanelX + 4, 50, "COACH THINKING", colors.secondary, 1);
    } else if (engineLaunchFailed) {
        drawText(kPanelX + 4, 50, "ENGINE ERROR", colors.check, 1);
    } else if (outcome != GameState::Ongoing) {
        drawText(kPanelX + 4, 50, outcomeName(outcome), colors.check, 1);
    } else if (game.inCheck(game.sideToMove())) {
        drawText(kPanelX + 4, 50, "CHECK", colors.check, 1);
    } else {
        drawText(kPanelX + 4, 50, "READY", colors.legal, 1);
    }

    if (hasLastMove) {
        std::snprintf(line, sizeof(line), "LAST  %s", records[recordCount - 1U].san.data());
        drawText(kPanelX + 4, 63, line, colors.text, 1);
    }
    if (coachFeedback.quality != MoveQuality::Unavailable) {
        std::snprintf(line, sizeof(line), "COACH %s",
                      moveQualityName(coachFeedback.quality));
        const bool warning = coachFeedback.quality == MoveQuality::Inaccuracy ||
                             coachFeedback.quality == MoveQuality::OutsideTopThree;
        drawText(kPanelX + 4, 74, line, warning ? colors.check : colors.legal, 1);
    } else if (lastSearch.hasMove && !lastSearch.fromBook) {
        std::snprintf(line, sizeof(line), "D%u  %lluK NODES", lastSearch.completedDepth,
                      static_cast<unsigned long long>(lastSearch.nodes / 1000U));
        drawText(kPanelX + 4, 74, line, colors.muted, 1);
    } else if (lastSearch.fromBook) {
        drawText(kPanelX + 4, 74, "OPENING BOOK", colors.muted, 1);
    } else {
        std::snprintf(line, sizeof(line), "COACH %s", coachModeName(coachMode));
        drawText(kPanelX + 4, 74, line, colors.muted, 1);
    }

    M5Cardputer.Display.drawFastHLine(kPanelX + 4, 85, kPanelWidth - 8, colors.surfaceStrong);
    int historyY = 89;
    const int first = recordCount > 3 ? static_cast<int>(recordCount) - 3 : 0;
    for (int index = first; index < static_cast<int>(recordCount); ++index) {
        const auto& record = records[static_cast<std::size_t>(index)];
        std::snprintf(line, sizeof(line), "%d. %s", index + 1, record.san.data());
        drawText(kPanelX + 4, historyY, line, colors.muted, 1);
        historyY += 9;
    }
    M5Cardputer.Display.drawFastHLine(kPanelX + 4, 116, kPanelWidth - 8, colors.surfaceStrong);
    drawText(kPanelX + 4, 120, "H COACH", colors.secondary, 1);
    drawText(kPanelX + 58, 120, "TAB MENU", colors.muted, 1);
    drawText(kPanelX + 4, 127, "U UNDO", colors.muted, 1);
}

void drawGame() {
    drawBoard();
    M5Cardputer.Display.fillRect(129, 0, 4, 135, theme().background);
    drawPanel();
}

void drawThemeIndicator(int x, int y) {
    const ThemePalette& colors = theme();
    for (int index = 0; index < 3; ++index) {
        const ThemePalette& option = kThemes[static_cast<std::size_t>(index)];
        const int swatchX = x + index * 11;
        M5Cardputer.Display.fillRect(swatchX + 1, y + 1, 7, 6, option.boardDark);
        M5Cardputer.Display.fillRect(swatchX + 4, y + 1, 4, 6, option.accent);
        const std::uint16_t outline = static_cast<int>(themeMode) == index
                                          ? colors.text
                                          : colors.surfaceStrong;
        M5Cardputer.Display.drawRect(swatchX, y, 9, 8, outline);
    }
}

void drawSetup() {
    const ThemePalette& colors = theme();
    M5Cardputer.Display.fillRect(0, 0, 240, 33, colors.surface);
    M5Cardputer.Display.fillRect(0, 0, 4, 33, colors.accent);
    drawText(12, 4, "CARDPUTER", colors.accent, 1);
    drawText(12, 14, "CHESS", colors.text, 2);
    drawText(96, 18, "OFFLINE POCKET GAME", colors.muted, 1);
    drawPiece(214, 7, makePiece(Color::White, PieceType::Knight));
    M5Cardputer.Display.drawFastHLine(4, 32, 236, colors.accent);

    const std::array<const char*, 4> labels = {"PLAY AS", "LEVEL", "COACH", "THEME"};
    for (int row = 0; row < 4; ++row) {
        const int y = 43 + row * 20;
        M5Cardputer.Display.fillRect(8, y - 4, 224, 17, colors.background);
        if (setupRow == row) {
            M5Cardputer.Display.fillRoundRect(8, y - 4, 224, 17, 3, colors.surface);
            drawText(12, y, ">", colors.secondary, 1);
        }
        drawText(24, y, labels[static_cast<std::size_t>(row)], colors.text, 1);
        if (row == 0) {
            drawText(150, y, sideChoiceName(sideChoice), colors.accent, 1);
        } else if (row == 1) {
            char value[32];
            std::snprintf(value, sizeof(value), "%d %s", levelIndex + 1,
                          levelConfig(levelIndex).name);
            drawText(150, y, value, colors.accent, 1);
        } else if (row == 2) {
            drawText(150, y, coachModeName(coachMode), colors.accent, 1);
        } else if (row == 3) {
            drawText(150, y, colors.name, colors.accent, 1);
            drawThemeIndicator(198, y - 1);
        }
    }
    drawText(12, 126, "ARROWS CHANGE", colors.muted, 1);
    drawText(162, 126, "ENTER START", colors.secondary, 1);
}

void drawPromotion() {
    const ThemePalette& colors = theme();
    drawGame();
    M5Cardputer.Display.fillRoundRect(28, 38, 184, 58, 5, colors.surface);
    M5Cardputer.Display.drawRoundRect(28, 38, 184, 58, 5, colors.accent);
    drawText(42, 45, "CHOOSE PROMOTION", colors.text, 1);
    constexpr std::array<PieceType, 4> choices = {
        PieceType::Queen, PieceType::Rook, PieceType::Bishop, PieceType::Knight};
    for (int index = 0; index < 4; ++index) {
        const int x = 47 + index * 37;
        const std::uint16_t color = index == promotionIndex ? colors.selected
                                                            : colors.surfaceStrong;
        M5Cardputer.Display.fillRoundRect(x, 63, 28, 25, 3, color);
        drawPiece(x + 6, 68,
                  makePiece(humanColor, choices[static_cast<std::size_t>(index)]));
    }
}

void drawCoach() {
    const ThemePalette& colors = theme();
    drawGame();
    M5Cardputer.Display.fillRoundRect(14, 10, 212, 116, 6, colors.surface);
    M5Cardputer.Display.drawRoundRect(14, 10, 212, 116, 6, colors.secondary);
    drawText(24, 18, "COACH", colors.accent, 2);
    drawText(145, 21, coachModeName(coachMode), colors.muted, 1);
    M5Cardputer.Display.drawFastHLine(24, 38, 192, colors.surfaceStrong);

    if (coachSearchRunning()) {
        drawText(26, 51, "ANALYZING TOP THREE...", colors.text, 1);
        drawText(26, 68, "You can close and keep playing", colors.muted, 1);
        drawText(26, 108, "H / ESC  CLOSE", colors.secondary, 1);
        return;
    }
    if (!coachAnalysisCurrent()) {
        drawText(26, 52,
                 coachMode == CoachMode::Off ? "Coach is disabled" :
                                               "No analysis for this turn",
                 colors.text, 1);
        drawText(26, 70,
                 coachMode == CoachMode::Off ? "Enable it in TAB menu" :
                                               "Press H to analyze",
                 colors.muted, 1);
        drawText(26, 108, "H / ESC  CLOSE", colors.secondary, 1);
        return;
    }

    coachLineIndex = std::min<std::uint8_t>(
        coachLineIndex, static_cast<std::uint8_t>(coachAnalysis.lineCount - 1U));
    const AnalysisLine& line = coachAnalysis.lines[coachLineIndex];
    const std::string san = game.moveToSan(line.bestMove);
    char score[12];
    formatScore(line.scoreCp, score, sizeof(score));
    char summary[36];
    std::snprintf(summary, sizeof(summary), "%u/%u  %s   %s", coachLineIndex + 1U,
                  coachAnalysis.lineCount, san.c_str(), score);
    drawText(26, 47, summary, colors.text, 1);

    const int loss = std::max<int>(0, coachAnalysis.lines[0].scoreCp - line.scoreCp);
    char comparison[36];
    if (coachLineIndex == 0) {
        std::snprintf(comparison, sizeof(comparison), "Best line  depth %u",
                      coachAnalysis.completedDepth);
    } else {
        std::snprintf(comparison, sizeof(comparison), "-%d.%02d vs best  depth %u",
                      loss / 100, loss % 100, coachAnalysis.completedDepth);
    }
    drawText(26, 62, comparison,
             coachLineIndex == 0 ? colors.legal : colors.accent, 1);

    const std::string pv = principalVariationText(line);
    char pvLine[36];
    std::snprintf(pvLine, sizeof(pvLine), "PV %s", pv.c_str());
    drawText(26, 77, pvLine, colors.muted, 1);
    M5Cardputer.Display.drawFastHLine(24, 91, 192, colors.surfaceStrong);
    drawText(26, 97, "LEFT/RIGHT  LINES", colors.muted, 1);
    drawText(26, 109, "ENTER SHOW   H CLOSE", colors.secondary, 1);
}

void drawPause() {
    const ThemePalette& colors = theme();
    M5Cardputer.Display.fillRect(0, 0, 240, 24, colors.surface);
    M5Cardputer.Display.fillRect(0, 0, 4, 24, colors.accent);
    drawText(12, 5, "GAME MENU", colors.accent, 2);
    drawPiece(214, 4, makePiece(Color::White, PieceType::King));
    M5Cardputer.Display.drawFastHLine(4, 23, 236, colors.accent);
    const std::array<const char*, 6> labels = {"RESUME", "LEVEL", "COACH", "THEME",
                                               "UNDO TURN", "NEW GAME"};
    for (int row = 0; row < 6; ++row) {
        const int y = 28 + row * 16;
        M5Cardputer.Display.fillRect(8, y - 2, 224, 14, colors.background);
        if (pauseRow == row) {
            M5Cardputer.Display.fillRoundRect(8, y - 2, 224, 14, 3, colors.surface);
            drawText(12, y, ">", colors.secondary, 1);
        }
        drawText(26, y, labels[static_cast<std::size_t>(row)], colors.text, 1);
        if (row == 1) {
            char value[28];
            std::snprintf(value, sizeof(value), "%d %s", levelIndex + 1,
                          levelConfig(levelIndex).name);
            drawText(150, y, value, colors.accent, 1);
        } else if (row == 2) {
            drawText(150, y, coachModeName(coachMode), colors.accent, 1);
        } else if (row == 3) {
            drawText(150, y, colors.name, colors.accent, 1);
            drawThemeIndicator(198, y - 1);
        }
    }
    M5Cardputer.Display.fillRect(8, 124, 224, 11, colors.background);
    if (searchRunning) drawText(12, 126, "STOPPING ENGINE...", colors.check, 1);
}

void drawGameOver() {
    const ThemePalette& colors = theme();
    drawGame();
    M5Cardputer.Display.fillRoundRect(23, 40, 194, 56, 5, colors.surface);
    M5Cardputer.Display.drawRoundRect(23, 40, 194, 56, 5, colors.accent);
    drawText(38, 49, outcomeName(outcome), colors.check, 2);
    drawText(38, 79, "ENTER NEW GAME   U UNDO", colors.text, 1);
}

std::uint16_t moveFeedbackColor(MoveQuality quality) {
    const ThemePalette& colors = theme();
    switch (quality) {
        case MoveQuality::Best:
        case MoveQuality::Excellent:
        case MoveQuality::Good: return colors.legal;
        case MoveQuality::Inaccuracy:
        case MoveQuality::OutsideTopThree: return colors.check;
        case MoveQuality::Unavailable: return colors.secondary;
    }
    return colors.secondary;
}

AnimationKind outcomeAnimation() {
    if (outcome != GameState::WhiteWinsCheckmate &&
        outcome != GameState::BlackWinsCheckmate) {
        return AnimationKind::Draw;
    }
    const Color winner = outcome == GameState::WhiteWinsCheckmate ? Color::White
                                                                  : Color::Black;
    return winner == humanColor ? AnimationKind::Win : AnimationKind::Loss;
}

void drawAnimation() {
    if (animation.kind == AnimationKind::None || animation.durationMs == 0) return;
    const ThemePalette& colors = theme();
    const std::uint32_t elapsed = std::min<std::uint32_t>(
        millis() - animation.startMs, animation.durationMs);
    if (animation.kind == AnimationKind::GameIntro) {
        const int scanY = kBoardY + static_cast<int>(elapsed * 119U / animation.durationMs);
        M5Cardputer.Display.drawFastHLine(kBoardX, scanY, 8 * kSquarePixels,
                                          colors.accent);
        if (scanY > kBoardY) {
            M5Cardputer.Display.drawFastHLine(kBoardX, scanY - 1, 8 * kSquarePixels,
                                              colors.secondary);
        }
        return;
    }
    if (animation.kind == AnimationKind::MoveFeedback && animation.square >= 0) {
        const int column = squareToColumn(animation.square);
        const int row = squareToRow(animation.square);
        const int x = kBoardX + column * kSquarePixels;
        const int y = kBoardY + row * kSquarePixels;
        const std::uint16_t glow = moveFeedbackColor(animation.quality);
        M5Cardputer.Display.drawRect(x, y, kSquarePixels, kSquarePixels, glow);
        if (((elapsed / 84U) & 1U) == 0U) {
            M5Cardputer.Display.drawRect(x + 1, y + 1, kSquarePixels - 2,
                                         kSquarePixels - 2, glow);
        }
        if (animation.check) {
            for (int square = 0; square < 64; ++square) {
                if (!isCheckedKing(square)) continue;
                const int kingX = kBoardX + squareToColumn(square) * kSquarePixels;
                const int kingY = kBoardY + squareToRow(square) * kSquarePixels;
                M5Cardputer.Display.drawRect(kingX, kingY, kSquarePixels,
                                             kSquarePixels, colors.check);
                break;
            }
        }
        return;
    }

    const std::uint16_t celebration = animation.kind == AnimationKind::Win
                                          ? colors.accent
                                          : animation.kind == AnimationKind::Loss
                                                ? colors.check
                                                : colors.secondary;
    constexpr std::array<int, 12> confettiX = {8, 28, 53, 79, 107, 129,
                                               151, 174, 198, 224, 46, 188};
    constexpr std::array<int, 12> confettiY = {10, 119, 18, 111, 7, 126,
                                               15, 116, 6, 123, 31, 35};
    const int count = std::min<int>(12, static_cast<int>(elapsed * 13U /
                                                         animation.durationMs));
    for (int index = 0; index < count; ++index) {
        const int drop = static_cast<int>((elapsed / 55U + index * 3U) % 12U);
        M5Cardputer.Display.fillRect(confettiX[static_cast<std::size_t>(index)],
                                     confettiY[static_cast<std::size_t>(index)] + drop,
                                     2, 2, celebration);
    }
    if (((elapsed / 150U) & 1U) == 0U) {
        M5Cardputer.Display.drawRoundRect(21, 38, 198, 60, 6, celebration);
    }
}

void redraw() {
    M5Cardputer.Display.startWrite();
    if (!hasDrawnScreen || drawnScreen != screen) {
        M5Cardputer.Display.fillScreen(theme().background);
    }
    switch (screen) {
        case Screen::Setup: drawSetup(); break;
        case Screen::Playing: drawGame(); break;
        case Screen::Promotion: drawPromotion(); break;
        case Screen::Coach: drawCoach(); break;
        case Screen::Pause: drawPause(); break;
        case Screen::GameOver: drawGameOver(); break;
    }
    drawAnimation();
    M5Cardputer.Display.endWrite();
    drawnScreen = screen;
    hasDrawnScreen = true;
}

Action readAction() {
    if (!M5Cardputer.Keyboard.isChange() || !M5Cardputer.Keyboard.isPressed()) {
        return Action::None;
    }
    const auto& keys = M5Cardputer.Keyboard.keysState();
    if (keys.enter || keys.space) return Action::Confirm;
    if (keys.backspace || keys.del || keys.esc) return Action::Cancel;
    if (keys.tab) return Action::Menu;
    if (keys.up) return Action::Up;
    if (keys.down) return Action::Down;
    if (keys.left) return Action::Left;
    if (keys.right) return Action::Right;
    for (char raw : keys.word) {
        const char key = static_cast<char>(std::tolower(static_cast<unsigned char>(raw)));
        if (key == ';' || key == 'w') return Action::Up;
        if (key == '.' || key == 's') return Action::Down;
        if (key == ',' || key == 'a') return Action::Left;
        if (key == '/' || key == 'd') return Action::Right;
        if (key == 'u') return Action::Undo;
        if (key == 'h') return Action::Coach;
    }
    return Action::None;
}

void savePreferences() {
    preferences.putUChar("level", static_cast<std::uint8_t>(levelIndex));
    preferences.putUChar("side", static_cast<std::uint8_t>(sideChoice));
    preferences.putUChar("coach", static_cast<std::uint8_t>(coachMode));
    preferences.putUChar("theme", static_cast<std::uint8_t>(themeMode));
}

void loadPreferences() {
    preferences.begin("cpchess", false);
    levelIndex = std::min<int>(kLevelCount - 1, preferences.getUChar("level", 3));
    sideChoice = static_cast<SideChoice>(
        std::min<int>(2, preferences.getUChar("side", 0)));
    coachMode = static_cast<CoachMode>(
        std::min<int>(2, preferences.getUChar("coach", 1)));
    themeMode = static_cast<ThemeMode>(
        std::min<int>(2, preferences.getUChar("theme", 0)));
}

void engineTask(void*) {
    const SearchResult result = engine.search(searchPosition, taskLimits);
    portENTER_CRITICAL(&resultMutex);
    taskResult = result;
    taskResultPurpose = searchPurpose;
    searchDone = true;
    searchRunning = false;
    searchTaskHandle = nullptr;
    portEXIT_CRITICAL(&resultMutex);
    vTaskDelete(nullptr);
}

SearchLimits opponentLimits() {
    const LevelConfig& level = levelConfig(levelIndex);
    SearchLimits limits;
    limits.moveTimeMs = level.moveTimeMs;
    limits.maxDepth = level.maxDepth;
    limits.errorWindowCp = level.errorWindowCp;
    limits.candidateCount = level.candidateCount;
    limits.randomSeed = gameSeed ^ game.key();
    return limits;
}

SearchLimits coachLimits() {
    SearchLimits limits;
    limits.moveTimeMs = 1800;
    limits.maxDepth = 18;
    limits.errorWindowCp = 0;
    limits.candidateCount = 1;
    limits.multiPv = 3;
    limits.randomSeed = gameSeed ^ game.key() ^ UINT64_C(0x434F414348);
    limits.useOpeningBook = false;
    return limits;
}

void startSearch(SearchPurpose purpose) {
    if (searchRunning || outcome != GameState::Ongoing) return;
    if (purpose == SearchPurpose::Opponent && game.sideToMove() == humanColor) return;
    if (purpose == SearchPurpose::Coach && game.sideToMove() != humanColor) return;
    searchPosition = game;
    searchRootKey = game.key();
    searchPurpose = purpose;
    taskLimits = purpose == SearchPurpose::Opponent ? opponentLimits() : coachLimits();
    searchDone = false;
    discardSearch = false;
    engineLaunchFailed = false;
    searchRunning = true;
    const BaseType_t created = xTaskCreatePinnedToCore(engineTask, "chess-search", 16384,
                                                       nullptr, 1, &searchTaskHandle, 0);
    if (created != pdPASS) {
        searchRunning = false;
        engineLaunchFailed = true;
    }
}

void startEngineSearch() { startSearch(SearchPurpose::Opponent); }

void startCoachSearch(bool openWhenReady) {
    if (coachMode == CoachMode::Off || outcome != GameState::Ongoing ||
        game.sideToMove() != humanColor) {
        openCoachWhenReady = false;
        return;
    }
    openCoachWhenReady = openWhenReady;
    if (coachAnalysisCurrent()) {
        openCoachWhenReady = false;
        return;
    }
    startSearch(SearchPurpose::Coach);
}

void startTurnSearch() {
    if (outcome != GameState::Ongoing) return;
    if (game.sideToMove() != humanColor) {
        startEngineSearch();
    } else if (coachMode == CoachMode::Always) {
        startCoachSearch(false);
    }
}

void refreshLastMove() {
    hasLastMove = recordCount > 0;
    if (hasLastMove) lastMove = records[recordCount - 1].move;
}

void performUndo() {
    if (searchRunning) {
        engine.requestStop();
        discardSearch = true;
        pendingUndo = true;
        return;
    }
    if (recordCount == 0) return;
    do {
        --recordCount;
        game.unmakeMove(records[recordCount].move, records[recordCount].undo);
    } while (recordCount > 0 && game.sideToMove() != humanColor);
    outcome = GameState::Ongoing;
    selectedSquare = -1;
    selectedMoves.clear();
    lastSearch = SearchResult{};
    coachAnalysis = SearchResult{};
    coachAnalysisKey = 0;
    coachFeedback = CoachFeedback{};
    animation = UiAnimation{};
    refreshLastMove();
    screen = Screen::Playing;
    cursorSquare = humanColor == Color::White ? 12 : 52;
    startTurnSearch();
}

void applyMove(const Move& move) {
    if (recordCount >= records.size()) return;
    const bool humanMove = game.sideToMove() == humanColor;
    const std::string san = game.moveToSan(move);
    if (humanMove) {
        coachFeedback = coachAnalysisCurrent()
                            ? classifyCoachMove(coachAnalysis, move)
                            : CoachFeedback{};
        if (coachSearchRunning()) {
            engine.requestStop();
            discardSearch = true;
            pendingOpponentSearch = true;
        }
    }
    const MoveQuality playedQuality = humanMove ? coachFeedback.quality
                                                : MoveQuality::Unavailable;
    Undo undo;
    if (!game.makeMove(move, undo)) return;
    GameRecord& record = records[recordCount++];
    record.move = move;
    record.undo = undo;
    record.san.fill('\0');
    std::snprintf(record.san.data(), record.san.size(), "%s", san.c_str());
    lastMove = move;
    hasLastMove = true;
    selectedSquare = -1;
    selectedMoves.clear();
    coachAnalysis = SearchResult{};
    coachAnalysisKey = 0;
    outcome = game.gameState();
    if (outcome != GameState::Ongoing) {
        screen = Screen::GameOver;
        startAnimation(outcomeAnimation(), 1450);
    } else {
        screen = Screen::Playing;
        startAnimation(AnimationKind::MoveFeedback, 360, move.to, playedQuality,
                       game.inCheck(game.sideToMove()));
        startTurnSearch();
    }
}

void newGame() {
    game.resetToStartPosition();
    recordCount = 0;
    selectedSquare = -1;
    selectedMoves.clear();
    promotionIndex = 0;
    outcome = GameState::Ongoing;
    hasLastMove = false;
    lastSearch = SearchResult{};
    coachAnalysis = SearchResult{};
    coachAnalysisKey = 0;
    coachFeedback = CoachFeedback{};
    pendingUndo = false;
    pendingOpponentSearch = false;
    openCoachWhenReady = false;
    engineLaunchFailed = false;
    gameSeed = (static_cast<std::uint64_t>(esp_random()) << 32U) | esp_random();
    if (sideChoice == SideChoice::White) {
        humanColor = Color::White;
    } else if (sideChoice == SideChoice::Black) {
        humanColor = Color::Black;
    } else {
        humanColor = (esp_random() & 1U) == 0U ? Color::White : Color::Black;
    }
    cursorSquare = humanColor == Color::White ? 12 : 52;
    screen = Screen::Playing;
    startAnimation(AnimationKind::GameIntro, 360);
    savePreferences();
    startTurnSearch();
}

void moveCursor(Action action) {
    int column = squareToColumn(cursorSquare);
    int row = squareToRow(cursorSquare);
    if (action == Action::Up) row = std::max(0, row - 1);
    if (action == Action::Down) row = std::min(7, row + 1);
    if (action == Action::Left) column = std::max(0, column - 1);
    if (action == Action::Right) column = std::min(7, column + 1);
    cursorSquare = screenToSquare(column, row);
}

void selectAtCursor() {
    if (game.sideToMove() != humanColor || opponentSearchRunning()) return;
    if (selectedSquare < 0) {
        const Piece piece = game.pieceAt(cursorSquare);
        if (piece == 0 || pieceColor(piece) != humanColor) return;
        MoveList all;
        game.generateLegalMoves(all);
        selectedMoves.clear();
        for (std::uint16_t index = 0; index < all.size; ++index) {
            if (all[index].from == cursorSquare) selectedMoves.push(all[index]);
        }
        if (selectedMoves.size > 0) selectedSquare = cursorSquare;
        return;
    }

    std::array<Move, 4> matching{};
    std::uint8_t count = 0;
    for (std::uint16_t index = 0; index < selectedMoves.size; ++index) {
        if (selectedMoves[index].to == cursorSquare && count < matching.size()) {
            matching[count++] = selectedMoves[index];
        }
    }
    if (count == 1) {
        applyMove(matching[0]);
        return;
    }
    if (count > 1) {
        promotionIndex = 0;
        screen = Screen::Promotion;
        return;
    }

    const Piece piece = game.pieceAt(cursorSquare);
    selectedSquare = -1;
    selectedMoves.clear();
    if (piece != 0 && pieceColor(piece) == humanColor) selectAtCursor();
}

void confirmPromotion() {
    constexpr std::array<PieceType, 4> choices = {
        PieceType::Queen, PieceType::Rook, PieceType::Bishop, PieceType::Knight};
    const PieceType chosen = choices[static_cast<std::size_t>(promotionIndex)];
    for (std::uint16_t index = 0; index < selectedMoves.size; ++index) {
        const Move move = selectedMoves[index];
        if (move.to == cursorSquare && move.promotion == chosen) {
            applyMove(move);
            return;
        }
    }
}

void handleSetup(Action action) {
    if (action == Action::Up) setupRow = std::max(0, setupRow - 1);
    if (action == Action::Down) setupRow = std::min(3, setupRow + 1);
    if (action == Action::Confirm) {
        newGame();
        return;
    }
    if ((action == Action::Left || action == Action::Right) && setupRow == 0) {
        int value = static_cast<int>(sideChoice);
        value += action == Action::Left ? 2 : 1;
        sideChoice = static_cast<SideChoice>(value % 3);
        savePreferences();
    } else if ((action == Action::Left || action == Action::Right) && setupRow == 1) {
        levelIndex += action == Action::Left ? kLevelCount - 1 : 1;
        levelIndex %= kLevelCount;
        savePreferences();
    } else if ((action == Action::Left || action == Action::Right) && setupRow == 2) {
        cycleCoachMode(action == Action::Left ? -1 : 1);
        savePreferences();
    } else if ((action == Action::Left || action == Action::Right) && setupRow == 3) {
        cycleTheme(action == Action::Left ? -1 : 1);
        savePreferences();
    }
}

void handlePlaying(Action action) {
    if (action == Action::Menu) {
        if (searchRunning) {
            engine.requestStop();
            discardSearch = true;
        }
        screen = Screen::Pause;
        pauseRow = 0;
        return;
    }
    if (action == Action::Undo) {
        performUndo();
        return;
    }
    if (action == Action::Coach && game.sideToMove() == humanColor &&
        !opponentSearchRunning()) {
        screen = Screen::Coach;
        coachLineIndex = 0;
        startCoachSearch(true);
        return;
    }
    if (opponentSearchRunning() || game.sideToMove() != humanColor) return;
    if (action == Action::Up || action == Action::Down || action == Action::Left ||
        action == Action::Right) {
        moveCursor(action);
    } else if (action == Action::Confirm) {
        selectAtCursor();
    } else if (action == Action::Cancel) {
        selectedSquare = -1;
        selectedMoves.clear();
    }
}

void handleCoach(Action action) {
    if (action == Action::Coach || action == Action::Cancel) {
        openCoachWhenReady = false;
        screen = Screen::Playing;
        return;
    }
    if (action == Action::Menu) {
        screen = Screen::Playing;
        handlePlaying(Action::Menu);
        return;
    }
    if (action == Action::Undo) {
        screen = Screen::Playing;
        performUndo();
        return;
    }
    if (!coachAnalysisCurrent()) return;
    if (action == Action::Left || action == Action::Up) {
        coachLineIndex = static_cast<std::uint8_t>(
            (coachLineIndex + coachAnalysis.lineCount - 1U) % coachAnalysis.lineCount);
    } else if (action == Action::Right || action == Action::Down) {
        coachLineIndex = static_cast<std::uint8_t>(
            (coachLineIndex + 1U) % coachAnalysis.lineCount);
    } else if (action == Action::Confirm) {
        const Move candidate = coachAnalysis.lines[coachLineIndex].bestMove;
        screen = Screen::Playing;
        cursorSquare = candidate.from;
        selectedSquare = -1;
        selectedMoves.clear();
        selectAtCursor();
    }
}

void handlePromotion(Action action) {
    if (action == Action::Left || action == Action::Up) {
        promotionIndex = (promotionIndex + 3) % 4;
    } else if (action == Action::Right || action == Action::Down) {
        promotionIndex = (promotionIndex + 1) % 4;
    } else if (action == Action::Confirm) {
        confirmPromotion();
    } else if (action == Action::Cancel) {
        screen = Screen::Playing;
    }
}

void handlePause(Action action) {
    if (action == Action::Up) pauseRow = std::max(0, pauseRow - 1);
    if (action == Action::Down) pauseRow = std::min(5, pauseRow + 1);
    if (pauseRow == 1 && (action == Action::Left || action == Action::Right)) {
        levelIndex += action == Action::Left ? kLevelCount - 1 : 1;
        levelIndex %= kLevelCount;
        savePreferences();
    }
    if (pauseRow == 2 &&
        (action == Action::Left || action == Action::Right ||
         action == Action::Confirm)) {
        cycleCoachMode(action == Action::Left ? -1 : 1);
        savePreferences();
    }
    if (pauseRow == 3 &&
        (action == Action::Left || action == Action::Right ||
         action == Action::Confirm)) {
        cycleTheme(action == Action::Left ? -1 : 1);
        savePreferences();
    }
    if ((action == Action::Menu || action == Action::Cancel ||
         (action == Action::Confirm && pauseRow == 0)) &&
        !searchRunning) {
        screen = outcome == GameState::Ongoing ? Screen::Playing : Screen::GameOver;
        startTurnSearch();
    } else if (action == Action::Confirm && pauseRow == 4 && !searchRunning) {
        performUndo();
    } else if (action == Action::Confirm && pauseRow == 5 && !searchRunning) {
        screen = Screen::Setup;
    }
}

void handleGameOver(Action action) {
    if (action == Action::Confirm) {
        screen = Screen::Setup;
    } else if (action == Action::Undo) {
        performUndo();
    }
}

void consumeSearchResult() {
    if (!searchDone) return;
    SearchResult result;
    SearchPurpose completedPurpose;
    portENTER_CRITICAL(&resultMutex);
    result = taskResult;
    completedPurpose = taskResultPurpose;
    searchDone = false;
    portEXIT_CRITICAL(&resultMutex);

    if (discardSearch) {
        discardSearch = false;
        if (pendingUndo) {
            pendingUndo = false;
            pendingOpponentSearch = false;
            performUndo();
        } else if (pendingOpponentSearch) {
            pendingOpponentSearch = false;
            startEngineSearch();
        }
        redraw();
        return;
    }
    if (result.hasMove && game.key() == searchRootKey) {
        if (completedPurpose == SearchPurpose::Opponent &&
            game.sideToMove() != humanColor) {
            lastSearch = result;
            applyMove(result.bestMove);
            cursorSquare = result.bestMove.to;
        } else if (completedPurpose == SearchPurpose::Coach &&
                   game.sideToMove() == humanColor) {
            coachAnalysis = result;
            coachAnalysisKey = searchRootKey;
            coachLineIndex = 0;
            if (openCoachWhenReady) screen = Screen::Coach;
        }
    }
    openCoachWhenReady = false;
    redraw();
}

}  // namespace

void setup() {
    auto config = M5.config();
    M5Cardputer.begin(config, true);
    M5Cardputer.Display.setRotation(1);
    M5Cardputer.Display.setTextWrap(false);
    M5Cardputer.Display.setBrightness(110);
    loadPreferences();
    redraw();
}

void loop() {
    M5Cardputer.update();
    consumeSearchResult();

    const Action action = readAction();
    if (action != Action::None) {
        switch (screen) {
            case Screen::Setup: handleSetup(action); break;
            case Screen::Playing: handlePlaying(action); break;
            case Screen::Promotion: handlePromotion(action); break;
            case Screen::Coach: handleCoach(action); break;
            case Screen::Pause: handlePause(action); break;
            case Screen::GameOver: handleGameOver(action); break;
        }
        redraw();
    }

    if (animation.kind != AnimationKind::None) {
        const std::uint32_t now = millis();
        if (now - animation.lastFrameMs >= kAnimationFrameMs) {
            animation.lastFrameMs = now;
            if (now - animation.startMs >= animation.durationMs) {
                animation = UiAnimation{};
            }
            redraw();
        }
    }

    delay(8);
}

#endif  // ARDUINO
