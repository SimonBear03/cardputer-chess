# Cardputer-Adv hardware test checklist

Use this full checklist before calling a firmware release device-validated. For
a quick personal test, sections 1–3 cover flashing, controls, and the display.
Record the firmware commit and any failures when testing a release.

## 1. Build and flash

- Charge the Cardputer-Adv and set its side power switch to `ON` for normal use.
- Connect it by USB-C.
- Build and upload:

  ```sh
  platformio run --target upload
  ```

- If automatic download mode fails, switch the device `OFF`, hold `G0`, apply
  USB power, release `G0`, and retry the upload.
- Confirm the setup screen appears without a reboot loop or corrupted pixels.
- Start a game, complete several moves, switch the Cardputer off, and switch it
  back on. Confirm it boots directly to the same board, side, move history,
  last-move highlight, and undo state.
- Switch it off while the engine is thinking immediately after a human move.
  Confirm the completed human move is restored and engine thinking restarts.
- Choose New Game from the game menu, reboot before starting another game, and
  confirm the old game is not resumed.

## 2. Keyboard and menus

- Change **Play as** through White, Black, and Random.
- Change all eight levels and reboot; confirm the last side choice and level
  persist.
- Change Coach through Off, On demand, and Always; reboot and confirm it persists.
- Change Theme among Classic, Neon, and Royal from setup and the game menu;
  reboot and confirm it persists.
- On setup, confirm Up/Down changes rows, Left/Right changes values, and Enter or
  Space starts immediately from every row.
- Navigate with `Fn` plus each printed arrow key.
- Navigate again with bare `;`, `,`, `.`, `/`, then with `W/A/S/D`.
- Confirm with Enter and Space.
- Select a piece, then cancel with Backspace and with `Fn`+Backspace (Esc).
- Open and close the game menu with Tab.
- Open Coach with `H`, browse candidates with Left/Right, close it with `H` and
  Esc, and use Enter to focus a suggested move on the board.

## 3. Board interaction

- Confirm the board is oriented with the human side at the bottom.
- Confirm `a`–`h` and `1`–`8` labels are legible and rotate correctly when
  playing Black.
- Confirm pawns, knights, bishops, rooks, queens, and kings use distinct piece
  silhouettes in all themes, with the pawn clearly shorter than the pointed
  bishop and both colors readable on light and dark squares.
- In Classic, confirm the gold and teal squares are obviously different while
  both white and black pieces remain readable.
- Confirm the king's cross uses the same body color as the rest of the king.
- Confirm there is visible space between the `H COACH` and `U UNDO` footer rows.
- Select several piece types and verify only legal destination squares light up.
- Move onto a highlighted square and confirm origin, destination, and history
  update.
- Verify the checked king is highlighted and illegal self-check moves are absent.
- Reach a promotion and choose queen, rook, bishop, and knight in separate runs.
- Exercise kingside castling, queenside castling, and en passant.

## 4. Engine and game flow

- Start as White and as Black; when playing Black, confirm the engine makes the
  opening move without blocking keyboard/UI updates.
- Try Beginner, Club, Expert, and Maximum. Confirm the UI remains responsive
  while `Thinking...` is shown and the engine eventually makes a legal move.
- Play at least ten non-book engine turns on Maximum and confirm no task-watchdog
  reset, reboot, or repeated screen flashing occurs during the ten-second budget.
- During a search, press Tab and confirm the engine stops cleanly before the menu
  resumes interaction.
- During a search, press `U`; confirm the pending search stops and the complete
  human turn is undone.
- In On-demand mode, press `H` on a human turn and confirm three unique legal
  move suggestions, evaluations, short continuation lines, and loss versus the
  best line appear.
- Repeatedly open, browse, close, and reopen Coach; confirm PV formatting never
  resets or reboots the device.
- In Always mode, confirm Coach analysis begins on each human turn without
  blocking cursor movement; play immediately and confirm the opponent search
  starts cleanly after Coach cancellation.
- Play the first, second, third, and an unlisted move in separate runs; confirm
  the quality feedback is sensible and an unlisted move says `Outside top 3`.
- Finish one checkmate and one stalemate; verify the result overlay and new-game
  path.
- Verify undo works from the game-over screen.

## 5. Stability

- Play at least one 40-move game at Maximum level.
- Confirm there are no spontaneous resets, engine-error messages, frozen keys,
  or persistent display artifacts.
- Navigate continuously between setup, game, Coach, promotion, and pause screens;
  confirm there is no periodic full-screen flicker or blank flash.
- Hold or repeatedly tap board and menu navigation keys; confirm only the
  changed squares or rows update and the rest of the frame stays visually still.
- Confirm theme changes redraw cleanly without scanlines or residual pixels.
- Confirm move, check, and win/loss/draw feedback remains clear without a
  repeated pulse or redraw loop.
- Check that the case remains comfortable to hold during repeated ten-second
  searches.
- Run once on battery and confirm expected brightness and acceptable battery
  drain for the intended use.

## 6. Record the result

Capture:

- Git commit
- Cardputer-Adv hardware revision
- Upload/toolchain version
- Pass/fail for each section
- Photos or serial output for any display, reset, or input defect
