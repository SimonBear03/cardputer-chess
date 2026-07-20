# Design QA

> **Current scope:** The Home and New Match shell now follows the selected
> combined retro-console reference. The board, pieces, and three palettes below
> remain the accepted visual baseline. Historical setup screenshots are not
> evidence for the new shell. The unfinished web simulator was not used.

## Current shell evidence

- Selected source visual truth:
  `/Users/simon/.codex/generated_images/019f7830-7f59-7980-aa36-07eef070f9cc/exec-49d98012-0280-4553-8535-97bb248074d7.png`
- Target viewport: 240×135 pixels, matching the Cardputer-Adv LCD
- Reference states: Home with Continue selected; New Match focused on Level 4
- Implemented states: Home with lower, evenly spaced actions and an arrow-only
  selection cursor; New Match with a four-segment setting rail, smaller
  neighboring settings, centered active value, and no enclosing selection box
- Implementation screenshot: unavailable by design for this pass. Simon asked
  not to use the unfinished simulator, and the physical LCD cannot be captured
  directly by the firmware toolchain.
- Code/build evidence: `src/main.cpp` compiles for `m5stack-stamps3`; the 303
  host assertions and both engine fingerprints remain unchanged.

The source and an implementation screenshot therefore cannot yet be placed in
the required same-viewport comparison. Exact LCD spacing, rasterization, color,
and absence of clipped text remain pending a physical-device inspection or a
straight-on photo from Simon.

## Evidence

- Source visual truth, Classic:
  `/Users/simon/.codex/generated_images/019f7830-7f59-7980-aa36-07eef070f9cc/exec-bc23ca04-a892-499b-ac61-58cde3fdf3ed.png`
- Source visual truth, Neon:
  `/Users/simon/.codex/generated_images/019f7830-7f59-7980-aa36-07eef070f9cc/exec-3c7d8b3a-7db9-47db-a20e-2ba966c085c3.png`
- Source visual truth, solid Staunton pieces:
  `docs/assets/piece-source-solid-staunton.png`
- Implementation screenshots:
  `build/ui-preview-classic-game.png`, `build/ui-preview-neon-game.png`,
  `build/ui-preview-royal-game.png`, `build/ui-preview-classic-setup.png`,
  `build/ui-preview-neon-setup.png`, and `build/ui-preview-royal-setup.png`
- Viewport: 240×135 pixels, matching the Cardputer-Adv LCD
- State: representative opening position, White to move, level 4, Coach on
  demand; setup screen focused on Theme
- Full-view comparison evidence:
  `build/design-compare-classic.png` and `build/design-compare-neon.png`
- Focused board/piece comparison evidence:
  `build/design-compare-classic-board.png` and
  `build/design-compare-neon-board.png`
- Solid-piece source/reduction/native-view comparison evidence:
  `build/design-compare-solid-pieces.png`
- Current-run piece-contrast audit captures:
  `build/audit-contrast-01-classic-before.png` through
  `build/audit-contrast-06-royal-after.png`
- Current-run before/after audit comparison:
  `build/audit-contrast-comparison.png`
- Current-run checkerboard-balance captures:
  `build/audit-board-balance-01-classic-before.png` through
  `build/audit-board-balance-06-royal-after.png`
- Current-run checkerboard before/after comparison:
  `build/audit-board-balance-comparison.png`

The implementation captures are code-rendered from the exact production
palettes, geometry, and generated piece masks. Their monospaced font is a close
desktop stand-in for M5GFX's fixed 6×8 bitmap font; exact LCD rasterization and
panel color remain part of the physical-device check.

## Accepted board and piece findings

No actionable P0, P1, or P2 mismatch remains.

- Fonts and typography: the title, turn badge, status, history, and key hints
  preserve the source hierarchy without wrapping or clipping at 240×135. The
  firmware uses the native M5GFX bitmap font for maximum small-size clarity.
- Spacing and layout rhythm: the 122×122 framed board and native-font coordinate
  gutters fit beside the 107-pixel information panel while preserving the
  source's board-first split. Separators, badges, four setup rows, overlays, and
  footer actions stay inside the physical viewport.
- Colors and visual tokens: Classic retains navy, cream, teal, cyan, and gold;
  Neon retains midnight, violet, aqua, magenta, and warm-ivory pieces. Royal
  extends the same direction with midnight plum, ivory, burgundy, and brass.
  Classic now balances all three competing relationships: its light/dark squares
  measure 2.40:1 apart after RGB565 quantization, while its weakest piece/square
  pair remains 2.56:1. Neon and Royal keep their user-approved palettes from the
  prior contrast pass. Semantic cursor, legal, last-move, and check colors remain
  distinct in all three themes. This is a targeted embedded-LCD contrast check,
  not a claim of full accessibility compliance.
- Image quality and asset fidelity: all six pieces come from a target-specific
  generated raster sheet, then reduce to fixed two-color 14×14 body/detail masks.
  Solid silhouettes preserve the source identities without a perimeter outline.
  The pawn occupies only the bottom ten rows, the knight has an asymmetric horse
  head, the bishop carries a mitre cut, the rook has battlements, the queen has a
  crown, and the king has a solid same-color cross. No piece is represented by a
  letter, emoji, text symbol, placeholder, or runtime vector drawing.
- Copy and content: Home, New Match, game, Coach, and pause labels use concise
  device-appropriate language. The active New Match value is framed by `< >`,
  while smaller neighboring settings communicate Up/Down navigation without a
  separate instruction block.

The source concepts contain more sculpted detail and denser ornament than can
remain legible on a 240×135 panel. The reduced silhouettes and compact panel are
intentional hardware adaptations, not unresolved drift. Coordinate labels from
the concepts now appear in the production layout and remain orientation-aware.

## Comparison History

### Iteration 1

- [P2] Raw edge/fill downsampling produced hollow or striped artifacts in the
  pawn, rook, queen, and king at 14 pixels. The reduction now derives one solid
  source silhouette and a consistent one-pixel boundary. Post-fix evidence:
  `build/design-compare-classic-board.png` and
  `build/design-compare-neon-board.png`.
- [P2] The first setup pass lacked enough game identity and gave no visual cue
  for each theme. The final screen adds a colored title band, accent rail,
  generated knight mark, and three-token palette swatches. Post-fix evidence:
  `build/ui-preview-classic-setup.png` and
  `build/ui-preview-neon-setup.png`.

### Iteration 2

- [P2] The pawn and bishop had similar height and weak silhouettes in the first
  reduction. The pawn is now a compact ten-row form with a round head and broad
  base; the bishop remains tall with a pointed, diagonally cut mitre. Post-fix
  evidence: `build/piece-glyph-preview-v2.png`,
  `build/design-compare-classic-board.png`, and
  `build/design-compare-neon-board.png`.
- [P2] The five-row setup made Enter behave differently by focus and the three
  palette blocks looked like decoration. The final screen uses four settings,
  starts from any row, labels the footer `ENTER START`, and frames the active
  member of an explicit three-theme indicator. Post-fix evidence:
  `build/ui-preview-classic-setup.png`, `build/ui-preview-neon-setup.png`, and
  `build/ui-preview-royal-setup.png`.

### Iteration 3

The revised full-view and focused comparisons found no remaining actionable
P0/P1/P2 issue. Board coordinates, proportions, palette, hierarchy, generated
assets, and visible copy are coherent at the target viewport.

### Iteration 4

- [P1] A full contrasting boundary consumed too much of each 14×14 mask and made
  the pieces visually noisy on the physical LCD. The replacement masks use
  target-specific generated Staunton silhouettes as solid bodies, reserving the
  second color for a small identity or base detail rather than tracing the
  perimeter.
- [P2] The old pawn, bishop, and knight depended on internal fill/edge changes
  that weakened their true-size silhouettes. The final pawn is four rows shorter
  than the major pieces, the bishop has a diagonal mitre cut, and the knight is
  intentionally asymmetric. The combined source, enlarged reduction, and three
  native 240×135 theme views in `build/design-compare-solid-pieces.png` show no
  remaining actionable mismatch.

### Iteration 5

- [P1] The physical-screen review exposed low solid-piece contrast that was not
  obvious in the enlarged sprite preview. Measured source pairs confirmed
  Classic white/light at 1.13:1, Neon black/dark at 1.18:1, Royal white/light at
  1.14:1, and Royal black/dark at 1.81:1.
- Classic now uses a deeper tournament-gold light square, Neon a brighter violet
  dark square, and Royal bronze/rose squares. White and black bodies were nudged
  toward ivory and near-black. All twelve RGB565 piece/square combinations now
  range from 3.18:1 to 5.67:1 without reintroducing perimeter outlines. Evidence:
  `build/audit-contrast-comparison.png`.

### Iteration 6

- [P1] Raising all piece/square pairs over 3:1 compressed both checkerboard tones
  into the same middle luminance range. The pieces improved, but the light and
  dark squares became difficult to distinguish on the physical LCD.
- The final change is deliberately scoped to Classic, the only theme reported as
  unclear: bright tournament gold replaces the mid-gold square and deep teal
  replaces the mid-teal square. After RGB565 quantization, Classic's square-pair
  contrast rises from 1.16:1 to 2.40:1 while the weakest piece/square pair remains
  2.56:1. Neon and Royal remain unchanged from the approved prior pass. Evidence:
  `build/audit-board-balance-comparison.png`.
- [P2] The king's cross used the inverted detail color and appeared pasted onto
  the body. The top cross pixels now use the body color; only the base retains
  the shared contrast accent.
- [P2] The `H COACH`/`TAB MENU` row visually touched `U UNDO` at the bottom of
  the panel. The first action row moves up two pixels, leaving a clean blank row
  while the undo hint remains fully inside the 135-pixel viewport.

## Interaction and Runtime Checks

- Three-way theme cycling is wired in setup and pause and persists through
  Preferences.
- Home offers Continue and New Game without exposing match settings. Enter/Space
  starts from every New Match setting; Up/Down moves through the vertical wheel,
  Left/Right changes the centered value, and Back returns Home without erasing a
  saved match.
- Home, New Match, playing, promotion, Coach, pause, and game-over screens use the active
  palette and the shared generated piece assets.
- Board navigation redraws only dirty 15-pixel squares, and menu navigation
  redraws only dirty rows; there is no timed full-screen repaint loop.
- Engine and Coach thinking use a 300 ms three-dot animation that redraws only
  the three small status dots per frame. Completed analysis exposes the best
  move in a bounded `NEXT <SAN>` panel line, and all variable panel copy is
  capped at 16 characters.
- Match-start, positive Coach feedback, and game-result accents run for only six
  localized 120 ms frames and never repaint the board.
- Theme selection uses one complete redraw rather than an overlay animation,
  preventing residual scanline fragments in partially repainted menu gaps.
- The primary interaction paths compile in the firmware build. The unfinished
  simulator was intentionally excluded from this upgrade's evidence.
- Browser and browser-console checks are not applicable to this embedded LCD
  application.
- Physical key behavior, exact LCD appearance, and sustained Coach use are
  covered by the hardware checklist and require an operator walkthrough on the
  flashed device.

## Follow-up Polish

- [P3] Confirm the smallest coordinate labels, muted labels, revised solid-piece
  contrast, and dirty-region stability on the physical LCD under both
  bright-room and low-light conditions.

final result: blocked
