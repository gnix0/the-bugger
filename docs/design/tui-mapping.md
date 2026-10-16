# Prototype-to-terminal mapping (M01)

The HTML in `prototype.html` is the approved visual reference. The first M01
rendering was rejected by the owner for visual differences. These corrections
remain **awaiting manual visual acceptance**; passing interaction tests does
not establish fidelity to the reference.

| Component | Previous discrepancy | Terminal implementation |
|---|---|---|
| Workspace | One background and prominent ASCII boxes | `#221f22` workspace gutters, `#2d2a2e` panels, raised headers and distinct footers; no ASCII boxes |
| Toolbar | Separate branding, buttons and extra header rows | Identity and action strip share a row at 124+ columns; yellow key hints, raised buttons, right-aligned Session/Quit; wraps on narrow screens |
| Tabs | Leading `*` alone, little visual hierarchy | Active tab has selected background, pink text, bold underline; inactive tabs are muted; focused pane has `>` marker |
| Panels | Blank rows around headers and large border overhead | One-row tab strip, one-row inspector/table heading, body and one-row contextual footer; one-cell gutters |
| Source | Whole-line coloring | Separate gutter, line numbers, keywords, types, functions and literals; current line has full-width selection background |
| Inspection | Whole tables colored uniformly | Purple addresses, muted instruction bytes/types, cyan names, green values, yellow changes; selected rows preserve semantic foreground colors |
| Status | Identity, target and execution shared a single style | Pink identity, cyan session label, green labeled simulated run state, explicit disconnected demo ribbon, muted diagnostics/footer |
| Startup | Full-width, loosely grouped controls | Bounded central content area, raised session choices with muted descriptions, aligned labels and fields, selected-input styling |

## Terminal adaptations and limitations

- Fixed character cells cannot reproduce CSS pixel padding, font sizes, rounded
  corners, shadows or one-pixel borders. Background surfaces, cell gutters,
  bold text and underline provide the corresponding hierarchy. Font rendering
  remains the user's terminal's responsibility.
- Toolbar text labels replace browser icons/tooltips, so actions remain
  discoverable without hover support or font-dependent symbols. Narrow screens
  wrap the action strip and retain the existing focused-pane fallback.
- Exact 24-bit palette values use ncurses direct-color terminfo. For standalone
  Alacritty advertising `COLORTERM=truecolor` or `24bit`, the application tries
  its matching `alacritty-direct` entry and falls back to the original entry if
  unavailable. It does not substitute unrelated terminal key/mouse definitions.
- Inside tmux, direct color requires a matching direct-color terminal description
  and RGB forwarding configured by the user. The application does not infer
  tmux RGB support from an inherited `COLORTERM` or modify tmux settings.
  Otherwise ncurses uses 256/8 colors; `NO_COLOR` requests monochrome. Finite
  palettes may merge nearby gray shades. Labels, underline, focus and change
  markers remain meaningful without color.
- The browser's overlay menus are represented by full-page session configuration
  and pane-local view selection, preserving the strictly tiled workspace.
- Source and log text still clip to pane width. This correction does not add
  horizontal scrolling, a live console, dynamic splits or dragging. Those
  remain within their existing milestone boundaries.
- Samples remain Cortex-M demo data in all session previews. Future x86-64
  disassembly uses AT&T syntax, as recorded in the development plan.

## Inspection

Compare at 140x44 or larger first, then at 112x28, 80x24 and below 48x18.
Inspect the actual terminal directly and inside tmux with mouse enabled.
Compare active/inactive tabs, current source/disassembly rows, stepped values,
panel gutters, toolbar alignment, startup fields and monochrome readability.
Run `make test` for C interaction and renderer checks. The renderer assertions
verify palette and cell styling, not subjective visual acceptance.
