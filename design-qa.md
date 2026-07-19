# Design QA

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

The implementation captures are code-rendered from the exact production
palettes, geometry, and generated piece masks. Their monospaced font is a close
desktop stand-in for M5GFX's fixed 6×8 bitmap font; exact LCD rasterization and
panel color remain part of the physical-device check.

## Findings

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
  Semantic cursor, legal, last-move, and check colors remain distinct in all
  three palettes.
- Image quality and asset fidelity: all six pieces come from a target-specific
  generated raster sheet, then reduce to fixed two-color 14×14 body/detail masks.
  Solid silhouettes preserve the source identities without a perimeter outline.
  The pawn occupies only the bottom ten rows, the knight has an asymmetric horse
  head, the bishop carries a mitre cut, the rook has battlements, the queen has a
  crown, and the king has a cross. No piece is represented by a letter, emoji,
  text symbol, placeholder, or runtime vector drawing.
- Copy and content: setup, game, Coach, and pause labels use concise
  device-appropriate language. `ENTER START` accurately describes the direct
  setup action, and a framed three-swatch selector exposes all persistent themes.

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

## Interaction and Runtime Checks

- Three-way theme cycling is wired in setup and pause and persists through
  Preferences.
- Enter/Space starts from every setup row; Up/Down selects and Left/Right changes
  settings.
- Setup, playing, promotion, Coach, pause, and game-over screens use the active
  palette and the shared generated piece assets.
- Intro, move/check, and result animations use a 42 ms non-blocking
  update cadence without a framebuffer or delay loop.
- Theme selection uses one complete redraw rather than an overlay animation,
  preventing residual scanline fragments in partially repainted menu gaps.
- The primary interaction paths compile in the final firmware build; no build
  or simulator errors were present.
- Browser and browser-console checks are not applicable to this embedded LCD
  application.
- Physical key behavior, exact LCD appearance, and sustained Coach use are
  covered by the hardware checklist and require an operator walkthrough on the
  flashed device.

## Follow-up Polish

- [P3] Confirm the smallest coordinate labels, muted labels, black-piece detail
  colors, solid-piece contrast, and short animation timing on the physical LCD
  under both bright-room and low-light conditions.

final result: passed
