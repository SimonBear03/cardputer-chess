#ifdef ARDUINO

#include <Arduino.h>
#include <M5Cardputer.h>
#include <Preferences.h>
#include <esp_system.h>

#include "cardputer_chess/chess.hpp"
#include "cardputer_chess/coach.hpp"
#include "cardputer_chess/engine.hpp"
#include "cardputer_chess/piece_glyphs.hpp"
#include "cardputer_chess/saved_game.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstring>

using namespace cardputer_chess;

namespace {

constexpr int kBoardX = 8;
constexpr int kBoardY = 1;
constexpr int kSquarePixels = 15;
constexpr int kPanelX = 133;
constexpr int kPanelWidth = 107;
constexpr int kPanelTextChars = (kPanelWidth - 8) / 6;
constexpr std::uint32_t kThinkingFrameMs = 300;
constexpr std::uint32_t kEventFrameMs = 120;
constexpr std::uint8_t kEventFrameCount = 6;
constexpr std::uint32_t kSearchTaskStackBytes = 24576;
constexpr std::uint8_t kLevelPreferenceVersion = 2;

constexpr std::uint16_t rgb565(std::uint8_t red, std::uint8_t green,
                               std::uint8_t blue) {
    return static_cast<std::uint16_t>(((red & 0xF8U) << 8U) |
                                      ((green & 0xFCU) << 3U) | (blue >> 3U));
}

enum class Screen : std::uint8_t {
    Home,
    Setup,
    Playing,
    Promotion,
    Coach,
    Pause,
    GameOver,
};
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
enum class SaveSlot : std::uint8_t { None, A, B };
enum class UiAnimation : std::uint8_t { None, GameStart, PositiveMove, Result };

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
    std::uint16_t whiteDetail;
    std::uint16_t blackPiece;
    std::uint16_t blackDetail;
};

// Classic needs stronger luminance separation between its gold and teal squares;
// Neon and Royal retain their user-approved hue-driven checkerboards.
constexpr std::array<ThemePalette, 3> kThemes = {{
    {"Classic", rgb565(4, 20, 38), rgb565(9, 36, 58), rgb565(17, 55, 76),
     rgb565(190, 150, 83), rgb565(41, 100, 97), rgb565(212, 162, 42),
     rgb565(74, 207, 211), rgb565(113, 158, 142), rgb565(196, 55, 62),
     rgb565(231, 177, 62), rgb565(66, 205, 221), rgb565(246, 239, 216),
     rgb565(139, 165, 174), rgb565(255, 247, 220), rgb565(45, 46, 44),
     rgb565(22, 27, 31), rgb565(232, 205, 135)},
    {"Neon", rgb565(3, 5, 28), rgb565(15, 13, 55), rgb565(25, 27, 79),
     rgb565(28, 137, 156), rgb565(115, 74, 185), rgb565(236, 61, 151),
     rgb565(111, 238, 205), rgb565(49, 177, 178), rgb565(255, 77, 118),
     rgb565(126, 238, 212), rgb565(171, 82, 245), rgb565(255, 244, 206),
     rgb565(113, 190, 194), rgb565(255, 244, 206), rgb565(56, 24, 91),
     rgb565(10, 6, 28), rgb565(174, 104, 242)},
    {"Royal", rgb565(20, 10, 26), rgb565(45, 20, 41), rgb565(77, 32, 48),
     rgb565(160, 122, 74), rgb565(172, 68, 91), rgb565(211, 161, 73),
     rgb565(85, 196, 153), rgb565(174, 91, 89), rgb565(220, 58, 65),
     rgb565(226, 177, 88), rgb565(188, 118, 164), rgb565(250, 237, 210),
     rgb565(164, 135, 151), rgb565(255, 245, 220), rgb565(54, 26, 42),
     rgb565(34, 14, 30), rgb565(226, 177, 88)},
}};

struct GameRecord {
    Move move{};
    Undo undo{};
    std::array<char, 12> san{};
};

struct UiSnapshot {
    Screen screen;
    ThemeMode themeMode;
    SideChoice sideChoice;
    CoachMode coachMode;
    int levelIndex;
    int homeRow;
    int setupRow;
    int pauseRow;
    int cursorSquare;
    int selectedSquare;
    int promotionIndex;
    std::uint16_t recordCount;
    std::uint8_t coachLineIndex;
    std::uint64_t legalDestinationMask;
};

Preferences preferences;
Position game = Position::startPosition();
Engine engine(64);
std::array<GameRecord, kMaxGamePly> records{};
std::uint16_t recordCount = 0;

Screen screen = Screen::Home;
SideChoice sideChoice = SideChoice::White;
Color humanColor = Color::White;
int levelIndex = 3;
CoachMode coachMode = CoachMode::OnDemand;
ThemeMode themeMode = ThemeMode::Classic;
int homeRow = 0;
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
bool gameSaveFailed = false;
bool resumeAvailable = false;
std::uint64_t gameSeed = 0;
std::uint64_t searchRootKey = 0;
std::uint32_t lastThinkingFrameMs = 0;
std::uint8_t thinkingFrame = 0;
UiAnimation uiAnimation = UiAnimation::None;
std::uint32_t lastEventFrameMs = 0;
std::uint8_t eventFrame = 0;
std::uint8_t eventFrameCount = 0;
Screen drawnScreen = Screen::Home;
bool hasDrawnScreen = false;
SavedGame savedGameScratch{};
std::array<std::uint8_t, kSavedGameMaxBytes> savedGameBytes{};
SaveSlot activeSaveSlot = SaveSlot::None;
std::uint32_t activeSaveGeneration = 0;

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

void cycleTheme(int direction) {
    int value = static_cast<int>(themeMode);
    value = (value + (direction < 0 ? 2 : 1)) % 3;
    themeMode = static_cast<ThemeMode>(value);
    hasDrawnScreen = false;
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

int textWidth(const char* value, int size = 1) {
    return static_cast<int>(std::strlen(value)) * 6 * size;
}

void drawCenteredText(int y, const char* value, std::uint16_t color, int size = 1) {
    drawText(std::max(0, (240 - textWidth(value, size)) / 2), y, value, color, size);
}

void drawPanelText(int y, const char* value, std::uint16_t color) {
    char clipped[kPanelTextChars + 1]{};
    std::snprintf(clipped, sizeof(clipped), "%.*s", kPanelTextChars, value);
    drawText(kPanelX + 4, y, clipped, color, 1);
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

std::uint64_t legalDestinationMask() {
    std::uint64_t mask = 0;
    for (std::uint16_t index = 0; index < selectedMoves.size; ++index) {
        mask |= std::uint64_t{1} << selectedMoves[index].to;
    }
    return mask;
}

bool isCheckedKing(int square) {
    const Piece piece = game.pieceAt(square);
    return pieceType(piece) == PieceType::King && pieceColor(piece) == game.sideToMove() &&
           game.inCheck(game.sideToMove());
}

void drawPieceScaled(int x, int y, Piece piece, int scale) {
    if (piece == 0) return;
    const bool white = pieceColor(piece) == Color::White;
    const ThemePalette& colors = theme();
    const std::uint16_t body = white ? colors.whitePiece : colors.blackPiece;
    const std::uint16_t detail = white ? colors.whiteDetail : colors.blackDetail;
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
                if ((glyph.body[static_cast<std::size_t>(row)] & bit) != 0U) {
                    occupied = true;
                    pixelColor = body;
                }
                if ((glyph.detail[static_cast<std::size_t>(row)] & bit) != 0U) {
                    occupied = true;
                    pixelColor = detail;
                }
            }
            if (runStart >= 0 && (!occupied || pixelColor != runColor)) {
                M5Cardputer.Display.fillRect(x + (runStart + 1) * scale,
                                             y + (row + 1) * scale,
                                             (column - runStart) * scale, scale,
                                             runColor);
                runStart = -1;
            }
            if (occupied && runStart < 0) {
                runStart = column;
                runColor = pixelColor;
            }
        }
    }
}

void drawPiece(int x, int y, Piece piece) { drawPieceScaled(x, y, piece, 1); }

void drawBoardSquare(int square) {
    const ThemePalette& colors = theme();
    const int column = squareToColumn(square);
    const int row = squareToRow(square);
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

void drawBoardSquares() {
    for (int square = 0; square < 64; ++square) {
        drawBoardSquare(square);
    }
}

void drawBoard() {
    const ThemePalette& colors = theme();
    M5Cardputer.Display.fillRect(0, 0, kBoardX - 1, 135, colors.background);
    M5Cardputer.Display.fillRect(kBoardX - 1, 122, 122, 13, colors.background);
    drawBoardSquares();
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

void drawThinkingDots(int x, int y, std::uint16_t backgroundColor) {
    const ThemePalette& colors = theme();
    for (int index = 0; index < 3; ++index) {
        const std::uint16_t color = index == thinkingFrame
                                        ? colors.secondary
                                        : backgroundColor;
        M5Cardputer.Display.fillCircle(x + index * 7, y, 2, color);
    }
}

void startUiAnimation(UiAnimation animation) {
    uiAnimation = animation;
    eventFrame = 0;
    eventFrameCount = 0;
    lastEventFrameMs = millis();
}

void drawEventDots(int x, int y, std::uint16_t backgroundColor,
                   std::uint16_t activeColor, bool active) {
    for (int index = 0; index < 3; ++index) {
        const std::uint16_t color =
            active && index == eventFrame ? activeColor : backgroundColor;
        M5Cardputer.Display.fillCircle(x + index * 7, y, 2, color);
    }
}

void drawUiAnimationIndicator(UiAnimation animation, bool active) {
    const ThemePalette& colors = theme();
    if (animation == UiAnimation::Result) {
        if (screen == Screen::GameOver) {
            drawEventDots(106, 88, colors.surface, colors.check, active);
        }
        return;
    }
    if ((animation == UiAnimation::GameStart ||
         animation == UiAnimation::PositiveMove) &&
        screen == Screen::Playing) {
        const std::uint16_t color = animation == UiAnimation::PositiveMove
                                        ? colors.legal
                                        : colors.secondary;
        drawEventDots(kPanelX + 81, 8, colors.background, color, active);
    }
}

void drawPanelThinkingStatus() {
    const ThemePalette& colors = theme();
    const char* label = opponentSearchRunning() ? "ENGINE THINK" : "COACH THINK";
    drawPanelText(50, label, colors.secondary);
    drawThinkingDots(kPanelX + 82, 54, colors.surfaceStrong);
}

void drawPanel() {
    const ThemePalette& colors = theme();
    M5Cardputer.Display.fillRect(kPanelX, 0, kPanelWidth, 135, colors.background);
    drawText(kPanelX + 4, 3, "CARD CHESS", colors.accent, 1);
    if (uiAnimation == UiAnimation::GameStart ||
        uiAnimation == UiAnimation::PositiveMove) {
        drawUiAnimationIndicator(uiAnimation, true);
    }
    M5Cardputer.Display.drawFastHLine(kPanelX + 4, 13, kPanelWidth - 8, colors.surfaceStrong);

    char line[32];
    M5Cardputer.Display.fillRoundRect(kPanelX + 3, 17, kPanelWidth - 6, 16, 3,
                                      colors.surface);
    std::snprintf(line, sizeof(line), "%s TO MOVE", colorName(game.sideToMove()));
    drawText(kPanelX + 8, 21, line, colors.text, 1);
    std::snprintf(line, sizeof(line), "LV%d  %s", levelIndex + 1,
                  levelConfig(levelIndex).name);
    drawPanelText(38, line, colors.accent);

    if (gameSaveFailed) {
        drawPanelText(50, "SAVE ERROR", colors.check);
    } else if (opponentSearchRunning()) {
        drawPanelThinkingStatus();
    } else if (coachSearchRunning()) {
        drawPanelThinkingStatus();
    } else if (engineLaunchFailed) {
        drawPanelText(50, "ENGINE ERROR", colors.check);
    } else if (outcome != GameState::Ongoing) {
        drawPanelText(50, outcomeName(outcome), colors.check);
    } else if (game.inCheck(game.sideToMove())) {
        drawPanelText(50, "CHECK", colors.check);
    } else {
        drawPanelText(50, "READY", colors.legal);
    }

    if (hasLastMove) {
        std::snprintf(line, sizeof(line), "LAST %s", records[recordCount - 1U].san.data());
        drawPanelText(63, line, colors.text);
    }
    if (coachAnalysisCurrent()) {
        const std::string nextMove = game.moveToSan(coachAnalysis.lines[0].bestMove);
        std::snprintf(line, sizeof(line), "NEXT %.11s", nextMove.c_str());
        drawPanelText(74, line, colors.legal);
    } else if (coachFeedback.quality != MoveQuality::Unavailable) {
        const bool warning = coachFeedback.quality == MoveQuality::Inaccuracy ||
                             coachFeedback.quality == MoveQuality::OutsideTopThree;
        if (coachFeedback.quality == MoveQuality::OutsideTopThree) {
            std::snprintf(line, sizeof(line), "NOT IN TOP 3");
        } else {
            std::snprintf(line, sizeof(line), "COACH %s",
                          moveQualityName(coachFeedback.quality));
        }
        drawPanelText(74, line, warning ? colors.check : colors.legal);
    } else if (lastSearch.hasMove && !lastSearch.fromBook) {
        std::snprintf(line, sizeof(line), "D%u  %lluK", lastSearch.completedDepth,
                      static_cast<unsigned long long>(lastSearch.nodes / 1000U));
        drawPanelText(74, line, colors.muted);
    } else if (lastSearch.fromBook) {
        drawPanelText(74, "OPENING BOOK", colors.muted);
    } else {
        std::snprintf(line, sizeof(line), "COACH %s", coachModeName(coachMode));
        drawPanelText(74, line, colors.muted);
    }

    M5Cardputer.Display.drawFastHLine(kPanelX + 4, 85, kPanelWidth - 8, colors.surfaceStrong);
    int historyY = 89;
    const int first = recordCount > 3 ? static_cast<int>(recordCount) - 3 : 0;
    for (int index = first; index < static_cast<int>(recordCount); ++index) {
        const auto& record = records[static_cast<std::size_t>(index)];
        std::snprintf(line, sizeof(line), "%d. %s", index + 1, record.san.data());
        drawPanelText(historyY, line, colors.muted);
        historyY += 9;
    }
    M5Cardputer.Display.drawFastHLine(kPanelX + 4, 116, kPanelWidth - 8, colors.surfaceStrong);
    drawText(kPanelX + 4, 118, "H COACH", colors.secondary, 1);
    drawText(kPanelX + 58, 118, "TAB MENU", colors.muted, 1);
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

int homeActionCount() { return resumeAvailable ? 2 : 1; }

const char* homeActionLabel(int row) {
    if (!resumeAvailable) return "NEW GAME";
    return row == 0 ? "CONTINUE" : "NEW GAME";
}

void drawHomeAction(int row) {
    const ThemePalette& colors = theme();
    const int firstY = resumeAvailable ? 78 : 89;
    const int y = firstY + row * 18;
    const char* label = homeActionLabel(row);
    const int labelX = (240 - textWidth(label)) / 2;
    M5Cardputer.Display.fillRect(70, y, 100, 17, colors.background);
    if (homeRow == row) {
        drawText(labelX - 18, y + 2, ">", colors.accent, 1);
    }
    drawCenteredText(y + 2, label,
                     homeRow == row ? colors.secondary : colors.muted, 1);
}

void drawHome() {
    const ThemePalette& colors = theme();
    M5Cardputer.Display.fillScreen(colors.background);
    drawCenteredText(3, "CARDPUTER", colors.accent, 1);
    drawCenteredText(13, "CHESS", colors.text, 3);
    drawPieceScaled(104, 38, makePiece(Color::White, PieceType::Knight), 2);
    for (int row = 0; row < homeActionCount(); ++row) drawHomeAction(row);
    drawText(8, 126, "ARROWS SELECT", colors.secondary, 1);
    drawText(172, 126, "ENTER OPEN", colors.text, 1);
}

constexpr std::array<const char*, 4> kSetupLabels = {
    "PLAY AS", "LEVEL", "COACH", "THEME"};

int setupChoiceCount(int row) {
    return row == 1 ? kLevelCount : 3;
}

int setupChoiceIndex(int row) {
    if (row == 0) return static_cast<int>(sideChoice);
    if (row == 1) return levelIndex;
    if (row == 2) return static_cast<int>(coachMode);
    return static_cast<int>(themeMode);
}

void formatSetupChoice(int row, int choice, char* value, std::size_t valueSize) {
    const int count = setupChoiceCount(row);
    choice = (choice % count + count) % count;
    if (row == 0) {
        std::snprintf(value, valueSize, "%s",
                      sideChoiceName(static_cast<SideChoice>(choice)));
    } else if (row == 1) {
        std::snprintf(value, valueSize, "%d %s", choice + 1,
                      levelConfig(choice).name);
    } else if (row == 2) {
        std::snprintf(value, valueSize, "%s",
                      coachModeName(static_cast<CoachMode>(choice)));
    } else {
        std::snprintf(value, valueSize, "%s",
                      kThemes[static_cast<std::size_t>(choice)].name);
    }
    for (std::size_t index = 0; value[index] != '\0'; ++index) {
        value[index] = static_cast<char>(
            std::toupper(static_cast<unsigned char>(value[index])));
    }
}

void formatSetupValue(int row, char* value, std::size_t valueSize) {
    formatSetupChoice(row, setupChoiceIndex(row), value, valueSize);
}

void drawSetupRow(int row) {
    const ThemePalette& colors = theme();
    const int y = 32 + row * 15;
    const bool selected = row == setupRow;
    char value[24];
    formatSetupValue(row, value, sizeof(value));
    if (selected) {
        M5Cardputer.Display.fillRect(8, y - 1, 3, 10, colors.accent);
        drawText(14, y, ">", colors.accent, 1);
    }
    drawText(26, y, kSetupLabels[static_cast<std::size_t>(row)],
             selected ? colors.secondary : colors.muted, 1);
    drawText(232 - textWidth(value), y, value, colors.text, 1);
}

void drawSetupOptions() {
    const ThemePalette& colors = theme();
    const int count = setupChoiceCount(setupRow);
    const int currentIndex = setupChoiceIndex(setupRow);
    char previous[24];
    char current[24];
    char next[24];
    char selected[28];
    formatSetupChoice(setupRow, currentIndex - 1, previous, sizeof(previous));
    formatSetupChoice(setupRow, currentIndex, current, sizeof(current));
    formatSetupChoice(setupRow, (currentIndex + 1) % count, next, sizeof(next));
    std::snprintf(selected, sizeof(selected), "<%s>", current);

    char label[24];
    std::snprintf(label, sizeof(label), "%s OPTIONS",
                  kSetupLabels[static_cast<std::size_t>(setupRow)]);
    drawCenteredText(94, label, colors.secondary, 1);

    constexpr int gap = 10;
    const int totalWidth = textWidth(previous) + gap + textWidth(selected) + gap +
                           textWidth(next);
    int x = std::max(4, (240 - totalWidth) / 2);
    drawText(x, 106, previous, colors.muted, 1);
    x += textWidth(previous) + gap;
    drawText(x, 106, selected, colors.text, 1);
    x += textWidth(selected) + gap;
    drawText(x, 106, next, colors.muted, 1);
}

void drawSetupMenu() {
    const ThemePalette& colors = theme();
    M5Cardputer.Display.fillRect(0, 29, 240, 90, colors.background);
    for (int row = 0; row < static_cast<int>(kSetupLabels.size()); ++row) {
        drawSetupRow(row);
    }
    M5Cardputer.Display.drawFastHLine(8, 90, 224, colors.surfaceStrong);
    drawSetupOptions();
}

void drawSetup() {
    const ThemePalette& colors = theme();
    M5Cardputer.Display.fillScreen(colors.background);
    drawCenteredText(4, "NEW MATCH", colors.accent, 2);
    drawPiece(214, 3, makePiece(Color::White, PieceType::Knight));
    M5Cardputer.Display.drawFastHLine(8, 27, 224, colors.accent);
    drawSetupMenu();
    M5Cardputer.Display.drawFastHLine(8, 119, 224, colors.accent);
    drawText(8, 126, "ESC HOME", colors.secondary, 1);
    drawText(172, 126, "ENTER PLAY", colors.text, 1);
}

void drawPromotionChoice(int index) {
    const ThemePalette& colors = theme();
    constexpr std::array<PieceType, 4> choices = {
        PieceType::Queen, PieceType::Rook, PieceType::Bishop, PieceType::Knight};
    const int x = 47 + index * 37;
    const std::uint16_t color =
        index == promotionIndex ? colors.selected : colors.surfaceStrong;
    M5Cardputer.Display.fillRoundRect(x, 63, 28, 25, 3, color);
    drawPiece(x + 6, 68,
              makePiece(humanColor, choices[static_cast<std::size_t>(index)]));
}

void drawPromotion() {
    const ThemePalette& colors = theme();
    drawGame();
    M5Cardputer.Display.fillRoundRect(28, 38, 184, 58, 5, colors.surface);
    M5Cardputer.Display.drawRoundRect(28, 38, 184, 58, 5, colors.accent);
    drawText(42, 45, "CHOOSE PROMOTION", colors.text, 1);
    for (int index = 0; index < 4; ++index) {
        drawPromotionChoice(index);
    }
}

void drawCoachLineDetails() {
    const ThemePalette& colors = theme();
    M5Cardputer.Display.fillRect(22, 43, 196, 45, colors.surface);
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
        drawText(26, 51, "ANALYZING TOP THREE", colors.text, 1);
        drawThinkingDots(147, 55, colors.surfaceStrong);
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

    drawCoachLineDetails();
    M5Cardputer.Display.drawFastHLine(24, 91, 192, colors.surfaceStrong);
    drawText(26, 97, "LEFT/RIGHT  LINES", colors.muted, 1);
    drawText(26, 109, "ENTER SHOW   H CLOSE", colors.secondary, 1);
}

void drawPauseRow(int row) {
    const ThemePalette& colors = theme();
    constexpr std::array<const char*, 6> labels = {
        "RESUME", "LEVEL", "COACH", "THEME", "UNDO TURN", "NEW GAME"};
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

void drawPause() {
    const ThemePalette& colors = theme();
    M5Cardputer.Display.fillRect(0, 0, 240, 24, colors.surface);
    M5Cardputer.Display.fillRect(0, 0, 4, 24, colors.accent);
    drawText(12, 5, "GAME MENU", colors.accent, 2);
    drawPiece(214, 4, makePiece(Color::White, PieceType::King));
    M5Cardputer.Display.drawFastHLine(4, 23, 236, colors.accent);
    for (int row = 0; row < 6; ++row) {
        drawPauseRow(row);
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
    if (uiAnimation == UiAnimation::Result) {
        drawUiAnimationIndicator(uiAnimation, true);
    }
}

UiSnapshot captureUiSnapshot() {
    return UiSnapshot{screen,
                      themeMode,
                      sideChoice,
                      coachMode,
                      levelIndex,
                      homeRow,
                      setupRow,
                      pauseRow,
                      cursorSquare,
                      selectedSquare,
                      promotionIndex,
                      recordCount,
                      coachLineIndex,
                      legalDestinationMask()};
}

void redraw();

void finishPartialRedraw() {
    drawnScreen = screen;
    hasDrawnScreen = true;
}

void redrawPanelOnly() {
    M5Cardputer.Display.startWrite();
    drawPanel();
    M5Cardputer.Display.endWrite();
    finishPartialRedraw();
}

void redrawThinkingIndicator() {
    if (!searchRunning) return;
    const bool panelIndicator = screen == Screen::Playing;
    const bool coachIndicator = screen == Screen::Coach && coachSearchRunning();
    if (!panelIndicator && !coachIndicator) return;
    M5Cardputer.Display.startWrite();
    if (panelIndicator) {
        drawThinkingDots(kPanelX + 82, 54, theme().surfaceStrong);
    } else {
        drawThinkingDots(147, 55, theme().surfaceStrong);
    }
    M5Cardputer.Display.endWrite();
}

void redrawUiAnimationIndicator(UiAnimation animation, bool active) {
    M5Cardputer.Display.startWrite();
    drawUiAnimationIndicator(animation, active);
    M5Cardputer.Display.endWrite();
}

void redrawAfterAction(const UiSnapshot& before) {
    if (!hasDrawnScreen || before.screen != screen ||
        before.themeMode != themeMode) {
        redraw();
        return;
    }

    M5Cardputer.Display.startWrite();
    bool changed = false;
    switch (screen) {
        case Screen::Home:
            if (before.homeRow != homeRow) {
                drawHomeAction(before.homeRow);
                drawHomeAction(homeRow);
                changed = true;
            }
            break;
        case Screen::Setup: {
            const bool rowChanged = before.setupRow != setupRow;
            const bool valueChanged = before.sideChoice != sideChoice ||
                                      before.levelIndex != levelIndex ||
                                      before.coachMode != coachMode;
            if (rowChanged || valueChanged) {
                drawSetupMenu();
                changed = true;
            }
            break;
        }
        case Screen::Playing:
            if (before.recordCount != recordCount) {
                drawGame();
                changed = true;
            } else {
                std::uint64_t dirtySquares =
                    before.legalDestinationMask ^ legalDestinationMask();
                if (before.selectedSquare != selectedSquare) {
                    if (before.selectedSquare >= 0) {
                        dirtySquares |= std::uint64_t{1} << before.selectedSquare;
                    }
                    if (selectedSquare >= 0) {
                        dirtySquares |= std::uint64_t{1} << selectedSquare;
                    }
                }
                if (before.cursorSquare != cursorSquare) {
                    dirtySquares |= std::uint64_t{1} << before.cursorSquare;
                    dirtySquares |= std::uint64_t{1} << cursorSquare;
                }
                for (int square = 0; square < 64; ++square) {
                    if ((dirtySquares & (std::uint64_t{1} << square)) != 0) {
                        drawBoardSquare(square);
                        changed = true;
                    }
                }
            }
            break;
        case Screen::Promotion:
            if (before.promotionIndex != promotionIndex) {
                drawPromotionChoice(before.promotionIndex);
                drawPromotionChoice(promotionIndex);
                changed = true;
            }
            break;
        case Screen::Coach:
            if (before.coachLineIndex != coachLineIndex && coachAnalysisCurrent()) {
                drawCoachLineDetails();
                changed = true;
            }
            break;
        case Screen::Pause: {
            const bool rowChanged = before.pauseRow != pauseRow;
            const bool valueChanged = before.levelIndex != levelIndex ||
                                      before.coachMode != coachMode;
            if (rowChanged) {
                drawPauseRow(before.pauseRow);
                drawPauseRow(pauseRow);
                changed = true;
            } else if (valueChanged) {
                drawPauseRow(pauseRow);
                changed = true;
            }
            break;
        }
        case Screen::GameOver: break;
    }
    M5Cardputer.Display.endWrite();
    if (changed) finishPartialRedraw();
}

void redraw() {
    M5Cardputer.Display.startWrite();
    const bool gameBackedScreen =
        screen == Screen::Playing || screen == Screen::Promotion ||
        screen == Screen::Coach || screen == Screen::GameOver;
    if (!hasDrawnScreen || (drawnScreen != screen && !gameBackedScreen)) {
        M5Cardputer.Display.fillScreen(theme().background);
    }
    switch (screen) {
        case Screen::Home: drawHome(); break;
        case Screen::Setup: drawSetup(); break;
        case Screen::Playing: drawGame(); break;
        case Screen::Promotion: drawPromotion(); break;
        case Screen::Coach: drawCoach(); break;
        case Screen::Pause: drawPause(); break;
        case Screen::GameOver: drawGameOver(); break;
    }
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
    preferences.putUChar("lvl_ver", kLevelPreferenceVersion);
    preferences.putUChar("side", static_cast<std::uint8_t>(sideChoice));
    preferences.putUChar("coach", static_cast<std::uint8_t>(coachMode));
    preferences.putUChar("theme", static_cast<std::uint8_t>(themeMode));
}

void loadPreferences() {
    preferences.begin("cpchess", false);
    const std::uint8_t storedLevel = preferences.getUChar("level", 3);
    const std::uint8_t storedLevelVersion = preferences.getUChar("lvl_ver", 1);
    const bool migrateLevelPreference =
        storedLevelVersion < kLevelPreferenceVersion;
    if (migrateLevelPreference) {
        constexpr std::array<std::uint8_t, 8> legacyLevelMap = {
            0, 1, 2, 3, 5, 7, 8, 9};
        levelIndex = legacyLevelMap[std::min<std::size_t>(storedLevel,
                                                          legacyLevelMap.size() - 1U)];
    } else {
        levelIndex = std::min<int>(kLevelCount - 1, storedLevel);
    }
    sideChoice = static_cast<SideChoice>(
        std::min<int>(2, preferences.getUChar("side", 0)));
    coachMode = static_cast<CoachMode>(
        std::min<int>(2, preferences.getUChar("coach", 1)));
    themeMode = static_cast<ThemeMode>(
        std::min<int>(2, preferences.getUChar("theme", 0)));
    if (migrateLevelPreference) savePreferences();
}

const char* saveSlotKey(SaveSlot slot) {
    return slot == SaveSlot::A ? "game_a" : "game_b";
}

bool readSavedGameSlot(SaveSlot slot, SavedGame& savedGame) {
    const char* key = saveSlotKey(slot);
    const std::size_t size = preferences.getBytesLength(key);
    if (size == 0 || size > savedGameBytes.size()) return false;
    if (preferences.getBytes(key, savedGameBytes.data(), size) != size) return false;
    return deserializeSavedGame(savedGameBytes.data(), size, savedGame);
}

bool loadNewestSavedGame() {
    const bool validA = readSavedGameSlot(SaveSlot::A, savedGameScratch);
    const std::uint32_t generationA = validA ? savedGameScratch.generation : 0;
    const bool validB = readSavedGameSlot(SaveSlot::B, savedGameScratch);
    const std::uint32_t generationB = validB ? savedGameScratch.generation : 0;
    if (!validA && !validB) return false;

    const SaveSlot selected =
        validB && (!validA || savedGameGenerationNewer(generationB, generationA))
            ? SaveSlot::B
            : SaveSlot::A;
    if (!readSavedGameSlot(selected, savedGameScratch)) return false;
    activeSaveSlot = selected;
    activeSaveGeneration = savedGameScratch.generation;
    return true;
}

void clearSavedGame() {
    preferences.remove(saveSlotKey(SaveSlot::A));
    preferences.remove(saveSlotKey(SaveSlot::B));
    activeSaveSlot = SaveSlot::None;
    activeSaveGeneration = 0;
    gameSaveFailed = false;
    resumeAvailable = false;
}

bool saveCurrentGame() {
    savedGameScratch = SavedGame{};
    savedGameScratch.generation = activeSaveGeneration + 1U;
    savedGameScratch.gameSeed = gameSeed;
    savedGameScratch.humanColor = humanColor;
    savedGameScratch.moveCount = recordCount;
    for (std::uint16_t index = 0; index < recordCount; ++index) {
        savedGameScratch.moves[index] = encodeSavedMove(records[index].move);
    }
    const std::size_t size = serializeSavedGame(
        savedGameScratch, savedGameBytes.data(), savedGameBytes.size());
    const SaveSlot destination =
        activeSaveSlot == SaveSlot::A ? SaveSlot::B : SaveSlot::A;
    if (size == 0 ||
        preferences.putBytes(saveSlotKey(destination), savedGameBytes.data(), size) !=
            size) {
        gameSaveFailed = true;
        return false;
    }
    activeSaveSlot = destination;
    activeSaveGeneration = savedGameScratch.generation;
    gameSaveFailed = false;
    resumeAvailable = true;
    return true;
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
    thinkingFrame = 0;
    lastThinkingFrameMs = millis();
    searchRunning = true;
    const BaseType_t created = xTaskCreatePinnedToCore(
        engineTask, "chess-search", kSearchTaskStackBytes, nullptr, 1,
        &searchTaskHandle, 0);
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

bool restoreSavedGame() {
    if (!loadNewestSavedGame()) return false;

    game.resetToStartPosition();
    recordCount = 0;
    humanColor = savedGameScratch.humanColor;
    gameSeed = savedGameScratch.gameSeed;
    for (std::uint16_t index = 0; index < savedGameScratch.moveCount; ++index) {
        Move move;
        if (!resolveSavedMove(game, savedGameScratch.moves[index], move)) {
            clearSavedGame();
            game.resetToStartPosition();
            recordCount = 0;
            return false;
        }
        const std::string san = game.moveToSan(move);
        Undo undo;
        if (!game.makeMove(move, undo)) {
            clearSavedGame();
            game.resetToStartPosition();
            recordCount = 0;
            return false;
        }
        GameRecord& record = records[recordCount++];
        record.move = move;
        record.undo = undo;
        record.san.fill('\0');
        std::snprintf(record.san.data(), record.san.size(), "%s", san.c_str());
        if ((index & 31U) == 31U) delay(1);
    }

    selectedSquare = -1;
    selectedMoves.clear();
    promotionIndex = 0;
    lastSearch = SearchResult{};
    coachAnalysis = SearchResult{};
    coachAnalysisKey = 0;
    coachFeedback = CoachFeedback{};
    pendingUndo = false;
    pendingOpponentSearch = false;
    openCoachWhenReady = false;
    engineLaunchFailed = false;
    outcome = game.gameState();
    refreshLastMove();
    cursorSquare = hasLastMove ? lastMove.to
                               : (humanColor == Color::White ? 12 : 52);
    resumeAvailable = true;
    homeRow = 0;
    screen = Screen::Home;
    return true;
}

void resumeGame() {
    if (!resumeAvailable) return;
    screen = outcome == GameState::Ongoing ? Screen::Playing : Screen::GameOver;
    if (outcome != GameState::Ongoing) startUiAnimation(UiAnimation::Result);
    startTurnSearch();
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
    refreshLastMove();
    screen = Screen::Playing;
    cursorSquare = humanColor == Color::White ? 12 : 52;
    saveCurrentGame();
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
        if (coachFeedback.quality == MoveQuality::Best ||
            coachFeedback.quality == MoveQuality::Excellent ||
            coachFeedback.quality == MoveQuality::Good) {
            startUiAnimation(UiAnimation::PositiveMove);
        }
        if (coachSearchRunning()) {
            engine.requestStop();
            discardSearch = true;
            pendingOpponentSearch = true;
        }
    }
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
    saveCurrentGame();
    if (outcome != GameState::Ongoing) {
        screen = Screen::GameOver;
        startUiAnimation(UiAnimation::Result);
    } else {
        screen = Screen::Playing;
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
    startUiAnimation(UiAnimation::GameStart);
    savePreferences();
    saveCurrentGame();
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

void handleHome(Action action) {
    const int lastRow = homeActionCount() - 1;
    if (action == Action::Up) homeRow = std::max(0, homeRow - 1);
    if (action == Action::Down) homeRow = std::min(lastRow, homeRow + 1);
    if (action != Action::Confirm) return;
    if (resumeAvailable && homeRow == 0) {
        resumeGame();
        return;
    }
    setupRow = 0;
    screen = Screen::Setup;
}

void handleSetup(Action action) {
    if (action == Action::Up) setupRow = std::max(0, setupRow - 1);
    if (action == Action::Down) setupRow = std::min(3, setupRow + 1);
    if (action == Action::Cancel) {
        homeRow = 0;
        screen = Screen::Home;
        return;
    }
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
        setupRow = 0;
        screen = Screen::Setup;
    }
}

void handleGameOver(Action action) {
    if (action == Action::Confirm) {
        setupRow = 0;
        screen = Screen::Setup;
    } else if (action == Action::Undo) {
        performUndo();
    }
}

void consumeSearchResult() {
    if (!searchDone) return;
    const Screen screenBeforeResult = screen;
    const std::uint16_t recordCountBeforeResult = recordCount;
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
    if (screenBeforeResult == Screen::Playing && screen == Screen::Playing &&
        recordCountBeforeResult == recordCount) {
        redrawPanelOnly();
    } else {
        redraw();
    }
}

}  // namespace

void setup() {
    auto config = M5.config();
    M5Cardputer.begin(config, true);
    M5Cardputer.Display.setRotation(1);
    M5Cardputer.Display.setTextWrap(false);
    M5Cardputer.Display.setBrightness(110);
    loadPreferences();
    restoreSavedGame();
    redraw();
}

void loop() {
    M5Cardputer.update();
    consumeSearchResult();

    const Action action = readAction();
    if (action != Action::None) {
        const UiSnapshot before = captureUiSnapshot();
        switch (screen) {
            case Screen::Home: handleHome(action); break;
            case Screen::Setup: handleSetup(action); break;
            case Screen::Playing: handlePlaying(action); break;
            case Screen::Promotion: handlePromotion(action); break;
            case Screen::Coach: handleCoach(action); break;
            case Screen::Pause: handlePause(action); break;
            case Screen::GameOver: handleGameOver(action); break;
        }
        redrawAfterAction(before);
    }

    if (searchRunning) {
        const std::uint32_t now = millis();
        if (now - lastThinkingFrameMs >= kThinkingFrameMs) {
            lastThinkingFrameMs = now;
            thinkingFrame = static_cast<std::uint8_t>((thinkingFrame + 1U) % 3U);
            redrawThinkingIndicator();
        }
    }

    if (uiAnimation != UiAnimation::None) {
        const std::uint32_t now = millis();
        if (now - lastEventFrameMs >= kEventFrameMs) {
            lastEventFrameMs = now;
            const UiAnimation animation = uiAnimation;
            ++eventFrameCount;
            if (eventFrameCount >= kEventFrameCount) {
                uiAnimation = UiAnimation::None;
                redrawUiAnimationIndicator(animation, false);
            } else {
                eventFrame = static_cast<std::uint8_t>((eventFrame + 1U) % 3U);
                redrawUiAnimationIndicator(animation, true);
            }
        }
    }

    delay(8);
}

#endif  // ARDUINO
