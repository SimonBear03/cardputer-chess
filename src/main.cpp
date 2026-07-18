#ifdef ARDUINO

#include <Arduino.h>
#include <M5Cardputer.h>
#include <Preferences.h>
#include <esp_system.h>

#include "cardputer_chess/chess.hpp"
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

enum class Screen : std::uint8_t { Setup, Playing, Promotion, Pause, GameOver };
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
};
enum class SideChoice : std::uint8_t { White, Black, Random };

struct GameRecord {
    Move move{};
    Undo undo{};
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

Position searchPosition = Position::startPosition();
SearchResult taskResult{};
TaskHandle_t searchTaskHandle = nullptr;
portMUX_TYPE resultMutex = portMUX_INITIALIZER_UNLOCKED;
volatile bool searchRunning = false;
volatile bool searchDone = false;
bool discardSearch = false;
bool pendingUndo = false;
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
    M5Cardputer.Display.setTextColor(color, kPanelColor);
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

    if (searchRunning) {
        drawText(kPanelX + 4, 44, "Thinking...", 0x07FF, 1);
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
        const std::string uci = Position::moveToUci(lastMove);
        std::snprintf(line, sizeof(line), "Last %s", uci.c_str());
        drawText(kPanelX + 4, 57, line, TFT_WHITE, 1);
    }
    if (lastSearch.hasMove && !lastSearch.fromBook) {
        std::snprintf(line, sizeof(line), "d%u  %llu kn", lastSearch.completedDepth,
                      static_cast<unsigned long long>(lastSearch.nodes / 1000U));
        drawText(kPanelX + 4, 69, line, 0xBDF7, 1);
    } else if (lastSearch.fromBook) {
        drawText(kPanelX + 4, 69, "Opening book", 0xBDF7, 1);
    }

    int historyY = 82;
    const int first = recordCount > 3 ? static_cast<int>(recordCount) - 3 : 0;
    for (int index = first; index < static_cast<int>(recordCount); ++index) {
        const std::string move = Position::moveToUci(records[static_cast<std::size_t>(index)].move);
        std::snprintf(line, sizeof(line), "%d. %s", index + 1, move.c_str());
        drawText(kPanelX + 4, historyY, line, 0xC618, 1);
        historyY += 10;
    }
    drawText(kPanelX + 4, 116, "TAB menu", 0x8410, 1);
    drawText(kPanelX + 4, 126, "U undo", 0x8410, 1);
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

    const std::array<const char*, 3> labels = {"Play as", "Level", "Start game"};
    for (int row = 0; row < 3; ++row) {
        const int y = 52 + row * 23;
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
        } else {
            drawText(126, y, "ENTER", 0x07E0, 1);
        }
    }
    drawText(12, 124, "Arrows move  Enter select", 0x8410, 1);
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

void drawPause() {
    M5Cardputer.Display.fillScreen(kPanelColor);
    drawText(12, 8, "GAME MENU", kAccent, 2);
    const std::array<const char*, 4> labels = {"Resume", "Level", "Undo turn", "New game"};
    for (int row = 0; row < 4; ++row) {
        const int y = 42 + row * 21;
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
    if (keys.del) return Action::Cancel;
    if (keys.tab) return Action::Menu;
    for (char raw : keys.word) {
        const char key = static_cast<char>(std::tolower(static_cast<unsigned char>(raw)));
        if (key == ';' || key == 'w') return Action::Up;
        if (key == '.' || key == 's') return Action::Down;
        if (key == ',' || key == 'a') return Action::Left;
        if (key == '/' || key == 'd') return Action::Right;
        if (key == 'u') return Action::Undo;
    }
    return Action::None;
}

void savePreferences() {
    preferences.putUChar("level", static_cast<std::uint8_t>(levelIndex));
    preferences.putUChar("side", static_cast<std::uint8_t>(sideChoice));
}

void loadPreferences() {
    preferences.begin("cpchess", false);
    levelIndex = std::min<int>(kLevelCount - 1, preferences.getUChar("level", 3));
    sideChoice = static_cast<SideChoice>(
        std::min<int>(2, preferences.getUChar("side", 0)));
}

void engineTask(void*) {
    const SearchResult result = engine.search(searchPosition, levelIndex, gameSeed ^ searchRootKey);
    portENTER_CRITICAL(&resultMutex);
    taskResult = result;
    searchDone = true;
    searchRunning = false;
    searchTaskHandle = nullptr;
    portEXIT_CRITICAL(&resultMutex);
    vTaskDelete(nullptr);
}

void startEngineSearch() {
    if (searchRunning || game.sideToMove() == humanColor || outcome != GameState::Ongoing)
        return;
    searchPosition = game;
    searchRootKey = game.key();
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
    refreshLastMove();
    screen = Screen::Playing;
    cursorSquare = humanColor == Color::White ? 12 : 52;
}

void applyMove(const Move& move) {
    if (recordCount >= records.size()) return;
    Undo undo;
    if (!game.makeMove(move, undo)) return;
    records[recordCount++] = GameRecord{move, undo};
    lastMove = move;
    hasLastMove = true;
    selectedSquare = -1;
    selectedMoves.clear();
    outcome = game.gameState();
    if (outcome != GameState::Ongoing) {
        screen = Screen::GameOver;
    } else {
        screen = Screen::Playing;
        startEngineSearch();
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
    startEngineSearch();
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
    if (game.sideToMove() != humanColor || searchRunning) return;
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
    if (action == Action::Down) setupRow = std::min(2, setupRow + 1);
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
    } else if (action == Action::Confirm && setupRow == 2) {
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
    if (searchRunning || game.sideToMove() != humanColor) return;
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
    if (action == Action::Down) pauseRow = std::min(3, pauseRow + 1);
    if (pauseRow == 1 && (action == Action::Left || action == Action::Right)) {
        levelIndex += action == Action::Left ? kLevelCount - 1 : 1;
        levelIndex %= kLevelCount;
        savePreferences();
    }
    if ((action == Action::Menu || action == Action::Cancel ||
         (action == Action::Confirm && pauseRow == 0)) &&
        !searchRunning) {
        screen = outcome == GameState::Ongoing ? Screen::Playing : Screen::GameOver;
        startEngineSearch();
    } else if (action == Action::Confirm && pauseRow == 2 && !searchRunning) {
        performUndo();
    } else if (action == Action::Confirm && pauseRow == 3 && !searchRunning) {
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
    portENTER_CRITICAL(&resultMutex);
    result = taskResult;
    searchDone = false;
    portEXIT_CRITICAL(&resultMutex);

    if (discardSearch) {
        discardSearch = false;
        if (pendingUndo) {
            pendingUndo = false;
            performUndo();
        }
        redraw();
        return;
    }
    if (result.hasMove && game.key() == searchRootKey &&
        game.sideToMove() != humanColor) {
        lastSearch = result;
        applyMove(result.bestMove);
        cursorSquare = result.bestMove.to;
    }
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
