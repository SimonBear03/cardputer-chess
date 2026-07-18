#ifdef ARDUINO

#include <Arduino.h>
#include <M5Cardputer.h>
#include <Preferences.h>
#include <esp_system.h>

#include "cardputer_chess/chess.hpp"
#include "cardputer_chess/coach.hpp"
#include "cardputer_chess/engine.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>

using namespace cardputer_chess;

namespace {

constexpr int kBoardX = 2;
constexpr int kBoardY = 3;
constexpr int kSquarePixels = 16;
constexpr int kPanelX = 133;
constexpr int kPanelWidth = 107;
constexpr std::uint16_t kLightSquare = 0xCE79;
constexpr std::uint16_t kDarkSquare = 0x4A49;
constexpr std::uint16_t kSelectedSquare = 0xFD20;
constexpr std::uint16_t kLegalSquare = 0x35E6;
constexpr std::uint16_t kLastMoveSquare = 0x449F;
constexpr std::uint16_t kCheckSquare = 0xF986;
constexpr std::uint16_t kPanelColor = 0x10A2;
constexpr std::uint16_t kAccent = 0x2E7F;

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
enum class SearchPurpose : std::uint8_t { Opponent, Coach };

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
std::uint32_t lastThinkingPaint = 0;

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
    Position position = game;
    std::string text;
    const std::uint8_t length = std::min<std::uint8_t>(line.principalVariationLength, 4);
    for (std::uint8_t index = 0; index < length; ++index) {
        const Move move = line.principalVariation[index];
        const std::string san = position.moveToSan(move);
        if (!text.empty()) text.push_back(' ');
        if (text.size() + san.size() > 27) break;
        text += san;
        Undo undo;
        if (!position.makeMove(move, undo)) break;
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

char pieceLetter(Piece piece) {
    switch (pieceType(piece)) {
        case PieceType::Pawn: return 'P';
        case PieceType::Knight: return 'N';
        case PieceType::Bishop: return 'B';
        case PieceType::Rook: return 'R';
        case PieceType::Queen: return 'Q';
        case PieceType::King: return 'K';
        case PieceType::None: return ' ';
    }
    return ' ';
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
    const std::uint16_t body = white ? TFT_WHITE : 0x18C3;
    const std::uint16_t edge = white ? 0x3186 : TFT_BLACK;
    const std::uint16_t ink = white ? TFT_BLACK : TFT_WHITE;
    M5Cardputer.Display.fillCircle(x + 8, y + 8, 6, body);
    M5Cardputer.Display.drawCircle(x + 8, y + 8, 6, edge);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(ink, body);
    M5Cardputer.Display.setCursor(x + 5, y + 4);
    M5Cardputer.Display.print(pieceLetter(piece));
}

void drawBoard() {
    for (int row = 0; row < 8; ++row) {
        for (int column = 0; column < 8; ++column) {
            const int square = screenToSquare(column, row);
            const int x = kBoardX + column * kSquarePixels;
            const int y = kBoardY + row * kSquarePixels;
            std::uint16_t color = ((column + row) & 1) == 0 ? kLightSquare : kDarkSquare;
            if (hasLastMove && (square == lastMove.from || square == lastMove.to)) {
                color = kLastMoveSquare;
            }
            if (isLegalDestination(square)) color = kLegalSquare;
            if (square == selectedSquare) color = kSelectedSquare;
            if (isCheckedKing(square)) color = kCheckSquare;
            M5Cardputer.Display.fillRect(x, y, kSquarePixels, kSquarePixels, color);
            if (isLegalDestination(square) && game.pieceAt(square) == 0) {
                M5Cardputer.Display.fillCircle(x + 8, y + 8, 2, 0x03E0);
            }
            drawPiece(x, y, game.pieceAt(square));
            if (square == cursorSquare) {
                M5Cardputer.Display.drawRect(x, y, kSquarePixels, kSquarePixels, TFT_CYAN);
                M5Cardputer.Display.drawRect(x + 1, y + 1, kSquarePixels - 2,
                                             kSquarePixels - 2, TFT_CYAN);
            }
        }
    }
    M5Cardputer.Display.drawRect(kBoardX - 1, kBoardY - 1, 130, 130, TFT_WHITE);
}

void drawPanel() {
    M5Cardputer.Display.fillRect(kPanelX, 0, kPanelWidth, 135, kPanelColor);
    drawText(kPanelX + 4, 3, "CARD CHESS", kAccent, 1);

    char line[32];
    std::snprintf(line, sizeof(line), "%s to move", colorName(game.sideToMove()));
    drawText(kPanelX + 4, 18, line, TFT_WHITE, 1);
    std::snprintf(line, sizeof(line), "Lv%d %s", levelIndex + 1,
                  levelConfig(levelIndex).name);
    drawText(kPanelX + 4, 30, line, 0xFFE0, 1);

    if (opponentSearchRunning()) {
        drawText(kPanelX + 4, 44, "Thinking...", 0x07FF, 1);
    } else if (coachSearchRunning()) {
        drawText(kPanelX + 4, 44, "Coach thinking", 0x07FF, 1);
    } else if (engineLaunchFailed) {
        drawText(kPanelX + 4, 44, "ENGINE ERROR", TFT_RED, 1);
    } else if (outcome != GameState::Ongoing) {
        drawText(kPanelX + 4, 44, outcomeName(outcome), 0xFD20, 1);
    } else if (game.inCheck(game.sideToMove())) {
        drawText(kPanelX + 4, 44, "CHECK", TFT_RED, 1);
    } else {
        drawText(kPanelX + 4, 44, "Your move", 0x07E0, 1);
    }

    if (hasLastMove) {
        std::snprintf(line, sizeof(line), "Last %s", records[recordCount - 1U].san.data());
        drawText(kPanelX + 4, 57, line, TFT_WHITE, 1);
    }
    if (coachFeedback.quality != MoveQuality::Unavailable) {
        std::snprintf(line, sizeof(line), "Coach %s",
                      moveQualityName(coachFeedback.quality));
        const bool warning = coachFeedback.quality == MoveQuality::Inaccuracy ||
                             coachFeedback.quality == MoveQuality::OutsideTopThree;
        drawText(kPanelX + 4, 69, line, warning ? 0xFD20 : 0x07E0, 1);
    } else if (lastSearch.hasMove && !lastSearch.fromBook) {
        std::snprintf(line, sizeof(line), "d%u  %llu kn", lastSearch.completedDepth,
                      static_cast<unsigned long long>(lastSearch.nodes / 1000U));
        drawText(kPanelX + 4, 69, line, 0xBDF7, 1);
    } else if (lastSearch.fromBook) {
        drawText(kPanelX + 4, 69, "Opening book", 0xBDF7, 1);
    }

    int historyY = 82;
    const int first = recordCount > 3 ? static_cast<int>(recordCount) - 3 : 0;
    for (int index = first; index < static_cast<int>(recordCount); ++index) {
        const auto& record = records[static_cast<std::size_t>(index)];
        std::snprintf(line, sizeof(line), "%d. %s", index + 1, record.san.data());
        drawText(kPanelX + 4, historyY, line, 0xC618, 1);
        historyY += 10;
    }
    drawText(kPanelX + 4, 116, "H coach", 0x8410, 1);
    drawText(kPanelX + 4, 126, "TAB menu U undo", 0x8410, 1);
}

void drawGame() {
    M5Cardputer.Display.fillScreen(TFT_BLACK);
    drawBoard();
    drawPanel();
}

void drawSetup() {
    M5Cardputer.Display.fillScreen(kPanelColor);
    drawText(12, 8, "CARDPUTER CHESS", kAccent, 2);
    drawText(12, 30, "Offline ESP32 chess", 0xBDF7, 1);

    const std::array<const char*, 4> labels = {"Play as", "Level", "Coach",
                                               "Start game"};
    for (int row = 0; row < 4; ++row) {
        const int y = 46 + row * 21;
        if (setupRow == row) {
            M5Cardputer.Display.fillRoundRect(8, y - 4, 224, 20, 4, 0x2945);
            drawText(12, y, ">", TFT_CYAN, 1);
        }
        drawText(24, y, labels[static_cast<std::size_t>(row)], TFT_WHITE, 1);
        if (row == 0) {
            drawText(126, y, sideChoiceName(sideChoice), 0xFFE0, 1);
        } else if (row == 1) {
            char value[32];
            std::snprintf(value, sizeof(value), "%d %s", levelIndex + 1,
                          levelConfig(levelIndex).name);
            drawText(126, y, value, 0xFFE0, 1);
        } else if (row == 2) {
            drawText(126, y, coachModeName(coachMode), 0xFFE0, 1);
        } else {
            drawText(126, y, "ENTER", 0x07E0, 1);
        }
    }
    drawText(12, 128, "Arrows move  Enter select", 0x8410, 1);
}

void drawPromotion() {
    drawGame();
    M5Cardputer.Display.fillRoundRect(30, 42, 180, 50, 5, 0x2104);
    M5Cardputer.Display.drawRoundRect(30, 42, 180, 50, 5, TFT_WHITE);
    drawText(50, 49, "Choose promotion", TFT_WHITE, 1);
    constexpr std::array<char, 4> names = {'Q', 'R', 'B', 'N'};
    for (int index = 0; index < 4; ++index) {
        const int x = 53 + index * 34;
        const std::uint16_t color = index == promotionIndex ? kSelectedSquare : 0x4208;
        M5Cardputer.Display.fillRoundRect(x, 65, 25, 20, 3, color);
        char value[2] = {names[static_cast<std::size_t>(index)], '\0'};
        drawText(x + 9, 70, value, TFT_WHITE, 1);
    }
}

void drawCoach() {
    drawGame();
    M5Cardputer.Display.fillRoundRect(16, 14, 208, 108, 6, 0x2104);
    M5Cardputer.Display.drawRoundRect(16, 14, 208, 108, 6, TFT_CYAN);
    drawText(26, 21, "COACH", kAccent, 2);
    drawText(119, 23, coachModeName(coachMode), 0xBDF7, 1);

    if (coachSearchRunning()) {
        drawText(28, 51, "Analyzing top three...", TFT_WHITE, 1);
        drawText(28, 68, "Close and keep playing", 0xBDF7, 1);
        drawText(28, 102, "H / ESC close", 0x8410, 1);
        return;
    }
    if (!coachAnalysisCurrent()) {
        drawText(28, 52,
                 coachMode == CoachMode::Off ? "Coach is disabled" :
                                               "No analysis for this turn",
                 TFT_WHITE, 1);
        drawText(28, 70,
                 coachMode == CoachMode::Off ? "Enable it in TAB menu" :
                                               "Press H to analyze",
                 0xBDF7, 1);
        drawText(28, 102, "H / ESC close", 0x8410, 1);
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
    drawText(28, 47, summary, TFT_WHITE, 1);

    const int loss = std::max<int>(0, coachAnalysis.lines[0].scoreCp - line.scoreCp);
    char comparison[36];
    if (coachLineIndex == 0) {
        std::snprintf(comparison, sizeof(comparison), "Best line  depth %u",
                      coachAnalysis.completedDepth);
    } else {
        std::snprintf(comparison, sizeof(comparison), "-%d.%02d vs best  depth %u",
                      loss / 100, loss % 100, coachAnalysis.completedDepth);
    }
    drawText(28, 62, comparison, coachLineIndex == 0 ? 0x07E0 : 0xFFE0, 1);

    const std::string pv = principalVariationText(line);
    char pvLine[36];
    std::snprintf(pvLine, sizeof(pvLine), "PV %s", pv.c_str());
    drawText(28, 77, pvLine, 0xBDF7, 1);
    drawText(28, 94, "LEFT/RIGHT lines", 0x8410, 1);
    drawText(28, 106, "ENTER show move  H close", 0x8410, 1);
}

void drawPause() {
    M5Cardputer.Display.fillScreen(kPanelColor);
    drawText(12, 8, "GAME MENU", kAccent, 2);
    const std::array<const char*, 5> labels = {"Resume", "Level", "Coach", "Undo turn",
                                               "New game"};
    for (int row = 0; row < 5; ++row) {
        const int y = 31 + row * 20;
        if (pauseRow == row) {
            M5Cardputer.Display.fillRoundRect(8, y - 4, 224, 19, 4, 0x2945);
            drawText(12, y, ">", TFT_CYAN, 1);
        }
        drawText(26, y, labels[static_cast<std::size_t>(row)], TFT_WHITE, 1);
        if (row == 1) {
            char value[28];
            std::snprintf(value, sizeof(value), "%d %s", levelIndex + 1,
                          levelConfig(levelIndex).name);
            drawText(126, y, value, 0xFFE0, 1);
        } else if (row == 2) {
            drawText(126, y, coachModeName(coachMode), 0xFFE0, 1);
        }
    }
    if (searchRunning) drawText(12, 126, "Stopping engine...", 0xFD20, 1);
}

void drawGameOver() {
    drawGame();
    M5Cardputer.Display.fillRoundRect(25, 44, 190, 48, 5, 0x2104);
    M5Cardputer.Display.drawRoundRect(25, 44, 190, 48, 5, TFT_WHITE);
    drawText(44, 52, outcomeName(outcome), 0xFD20, 2);
    drawText(43, 77, "ENTER new game  U undo", TFT_WHITE, 1);
}

void redraw() {
    switch (screen) {
        case Screen::Setup: drawSetup(); break;
        case Screen::Playing: drawGame(); break;
        case Screen::Promotion: drawPromotion(); break;
        case Screen::Coach: drawCoach(); break;
        case Screen::Pause: drawPause(); break;
        case Screen::GameOver: drawGameOver(); break;
    }
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
}

void loadPreferences() {
    preferences.begin("cpchess", false);
    levelIndex = std::min<int>(kLevelCount - 1, preferences.getUChar("level", 3));
    sideChoice = static_cast<SideChoice>(
        std::min<int>(2, preferences.getUChar("side", 0)));
    coachMode = static_cast<CoachMode>(
        std::min<int>(2, preferences.getUChar("coach", 1)));
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
    } else {
        screen = Screen::Playing;
        startTurnSearch();
    }
}

void newGame() {
    game = Position::startPosition();
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
    if ((action == Action::Left || action == Action::Right || action == Action::Confirm) &&
        setupRow == 0) {
        int value = static_cast<int>(sideChoice);
        value += action == Action::Left ? 2 : 1;
        sideChoice = static_cast<SideChoice>(value % 3);
        savePreferences();
    } else if ((action == Action::Left || action == Action::Right ||
                action == Action::Confirm) &&
               setupRow == 1) {
        levelIndex += action == Action::Left ? kLevelCount - 1 : 1;
        levelIndex %= kLevelCount;
        savePreferences();
    } else if ((action == Action::Left || action == Action::Right ||
                action == Action::Confirm) &&
               setupRow == 2) {
        cycleCoachMode(action == Action::Left ? -1 : 1);
        savePreferences();
    } else if (action == Action::Confirm && setupRow == 3) {
        newGame();
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
    if (action == Action::Down) pauseRow = std::min(4, pauseRow + 1);
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
    if ((action == Action::Menu || action == Action::Cancel ||
         (action == Action::Confirm && pauseRow == 0)) &&
        !searchRunning) {
        screen = outcome == GameState::Ongoing ? Screen::Playing : Screen::GameOver;
        startTurnSearch();
    } else if (action == Action::Confirm && pauseRow == 3 && !searchRunning) {
        performUndo();
    } else if (action == Action::Confirm && pauseRow == 4 && !searchRunning) {
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

    if (searchRunning && screen == Screen::Playing && millis() - lastThinkingPaint > 350U) {
        lastThinkingPaint = millis();
        drawPanel();
    }
    delay(8);
}

#endif  // ARDUINO
