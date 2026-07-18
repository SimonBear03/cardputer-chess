# Cardputer-Adv hardware test checklist

Use this checklist before calling a firmware binary device-validated. Record the
firmware commit and any failures in the release notes or issue tracker.

## 1. Build and flash

- Charge the Cardputer-Adv and set its side power switch to `ON` for normal use.
- Connect it by USB-C.
- Build and upload:

  ```sh
  uvx --with pip platformio run --target upload
  ```

- If automatic download mode fails, switch the device `OFF`, hold `G0`, apply
  USB power, release `G0`, and retry the upload.
- Confirm the setup screen appears without a reboot loop or corrupted pixels.

## 2. Keyboard and menus

- Change **Play as** through White, Black, and Random.
- Change all eight levels and reboot; confirm the last side choice and level
  persist.
- Change Coach through Off, On demand, and Always; reboot and confirm it persists.
- Navigate with `Fn` plus each printed arrow key.
- Navigate again with bare `;`, `,`, `.`, `/`, then with `W/A/S/D`.
- Confirm with Enter and Space.
- Select a piece, then cancel with Backspace and with `Fn`+Backspace (Esc).
- Open and close the game menu with Tab.
- Open Coach with `H`, browse candidates with Left/Right, close it with `H` and
  Esc, and use Enter to focus a suggested move on the board.

## 3. Board interaction

- Confirm the board is oriented with the human side at the bottom.
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
- During a search, press Tab and confirm the engine stops cleanly before the menu
  resumes interaction.
- During a search, press `U`; confirm the pending search stops and the complete
  human turn is undone.
- In On-demand mode, press `H` on a human turn and confirm three unique legal
  SAN candidates, evaluations, PV text, and loss versus the best line appear.
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
