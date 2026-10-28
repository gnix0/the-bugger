# Changelog

## [Unreleased]

### Added

- Physical RISC-V RV32 validation (test device: ESP32-C6FH4, built-in
  USB/JTAG) using the shared
  embedded fixture, core-context OpenOCD configuration and target-compatible
  GDB. Record source/disassembly, registers, explicit memory/variables,
  stepping, hardware break/watchpoints, execution/reset and disconnect results.
- Board validation checks actual decoded instructions and opcode bytes at the
  stopped PC, including aligned 2/4-byte RISC-V instructions. Document confirmed
  OpenOCD checksum and optional ROM-symbol decoding limitations separately from
  the working debugger workflow.

## [0.1.3] - 2026-10-28

### Changed

- Disassembly distinguishes unverified ELF metadata, user-confirmed remote
  ELF/load addresses and verified native backing-file mappings. Paginated review
  makes the declaration explicit; changed files/configuration invalidate trust
  while historical listings remain available. Confirmation never reads hardware.
- Backward reconstruction can reuse fresh retained boundaries after larger
  listings are evicted, within the shared 4 MiB budget. Each action decodes at
  most 128 rows from a reliable anchor within 1024 bytes; failure preserves the
  listing and its provenance. Closed/reused panes discard their former history.
- Disassembly keeps manual browsing across debugger transitions and distinguishes
  view-local Return to Stop from following the selected frame. Disassembly and
  Memory retain bounded shared history for navigation without reads while running.
- Memory provides temporary per-view range review, authorization and revocation;
  managing consent never reads hardware. Mixed disassembly retains file/line
  attribution, blank lines and braces through bounded local source annotations.
- Memory supports navigation-only MMIO units and explicit-width single-unit
  sampling for the documented, validated STM32F446/OpenOCD/ST-LINK setup.
  User-confirmed backend bindings and per-view consent are separate; accepted
  Console commands expire both before transmission. Reads use the existing
  serialized GDB connection without frontend polling, retries or byte fallback;
  uncertain results block further MMIO until reconciliation or reconnection.
- Lower pane groups start minimized above the footer. Restore them over the
  primary views, resize heights independently, and minimize without losing
  tabs or browsing state. Overlapping groups follow the most recent focus.
  Heights are session-only and survive temporary terminal-size clamping.

### Fixed

- Release inspector models before their GDB controller during session cleanup,
  preventing a Memory/MMIO use-after-free on exit. Keep variable baseline copies
  bounded in strict fortified sanitizer builds.
- Confine rendered text to its assigned terminal row and display-cell width,
  escaping control/invalid bytes instead of allowing ncurses to expand them
  into adjacent content. Preserve complete printable wide characters.
- Retain the prior Disassembly listing and browsing origin when GDB rejects a
  code request, so an unreadable neighboring address cannot leave an empty pane
  that refuses further paging.

- Keybinds scrolling keeps clean cell borders without underlined box glyphs.
  Action buttons center their labels; Memory Options and session profile actions
  fit their text with separated hit regions. All session forms wrap their complete
  explanatory notes and separate them from the action rows. Profile creation
  and reload actions share a padded bottom row, with Back aligned right. Native
  and Embedded setup selectors have a small gap before their fields; Start
  OpenOCD is separated from the preceding field while retaining its gap to
  script browsing, whose controls have equal spacing above and below. The
  Console command row is separated from Save Log when pane height permits.

- Footer text uses the window background, with status on the left and controls
  aligned right on the bottom row; long text uses the last two rows. Session forms, confirmations and
  action/setup menus use centered cards sized to their content. Keybinds shows
  an overflow scrollbar with track paging and thumb dragging.

- Menu footers show context-specific guidance instead of retaining launcher or
  previous-menu messages. Relevant save/action results remain visible, and
  background session updates do not overwrite menu guidance. The mouse fallback
  warning no longer labels normal sessions as DEMO.

## [0.1.2] - 2026-10-25

### Added

- Optional Vim-like input in Config → Keybinds, with Normal/Insert field modes,
  cursor/word editing, distinct field edges and explicit footer feedback.

- Config → Keybinds for personal debugger and workspace/navigation shortcuts,
  compact responsive Action/Key tables and direct keypress capture. Capture and edited
  cells have distinct border emphasis. Undo, confirmed Restore Defaults/discard,
  inert dimmed buttons and explicit Save keep changes under user control.
  Fixed navigation/exit fallbacks and literal text-field editing remain available.

- Console now has an inline GDB Command field below its output with Enter/Send,
  pane-local retained drafts and direct keyboard editing, keeping the workspace
  visible while entering commands. Save Log remains available separately.
- Ctrl+arrow and Ctrl+h/j/k/l shortcuts move content focus spatially between
  visible panes, preserving viewports and stopping at layout edges.
- Source panes can keep several GDB-known files open as session-local tabs.
  Current follows the selected frame; file tabs retain their source across
  stops. Each tab keeps its viewport, and narrow panes can cycle through tabs.

### Changed

- Vim navigation aliases are now opt-in. New preferences default to ordinary
  editing; old preferences disable only stock Vim aliases while preserving custom
  mappings and appearance. The toggle participates in Undo and explicit Save.

- Connection profiles exclude personal keybinds. Version-1 stores remain readable;
  legacy keys are ignored and omitted when writing the key-free version-2 format.
  Newly written stores require an updated app.

### Fixed

- Honor advertised RGB support inside tmux using its matching direct-color
  terminfo description, preserving exact theme foregrounds and backgrounds
  instead of reducing subtle backgrounds to 256-color grays. Keep original-TERM
  and NO_COLOR fallbacks without modifying tmux or the terminal palette.

- Too-small terminal warnings now show current dimensions and no longer label
  live sessions as DEMO; the minimum remains 48 columns by 18 rows.
- Keep Enter separate from Ctrl+J during content focus so keyboard selection
  in Call Stack, Threads and other views continues to work.

## [0.1.1] - 2026-10-24

### Added

- Temporary Source fullscreen through Source Actions or pane actions, keeping
  the application header/footer visible. Restore, Escape or q returns to the
  retained tiled workspace; resizing, tabs and splits are preserved.
- Focused demo/native fullscreen checks for input isolation, restoration and
  actual source stepping.

- Always-on lowercase h/j/k/l navigation matching Left/Down/Up/Right outside
  text fields. Existing arrows, mouse and function keys remain available;
  typed letters stay literal in fields, with no Vim editing modes.

- Centered Config menu with Profiles and Themes, seven built-in theme families
  and 37 variants. Separate Theme/Variant rows, temporary simulated preview,
  Back cancellation and atomic saved appearance preferences.
- Upstream palette revisions and MIT notices in `THIRD_PARTY_NOTICES`, included
  in the binary distribution. No Neovim or Lua dependency.
- Palette/contrast/fallback, preference and terminal interaction coverage;
  profile-management tests now enter through Config.

### Changed

- Spatial arrow/hjkl control navigation, stable focus across redraws, and
  control-only Tab traversal. Tabs activate with Enter; content scrolling starts
  through a deliberate body/scrollbar click and Escape returns to controls.
  Fullscreen excludes hidden focus targets and preserves a reachable Restore.

- ESP-IDF fixture sources explicitly enforce `-Wall -Wextra -Werror`, overriding
  inherited warning-as-error exceptions for the authored component while leaving
  SDK components unchanged. Native, test, release and STM32 builds were already
  strict.

- Catppuccin Macchiato is the default when no valid appearance preference exists;
  Monokai Pro remains selectable. The six-pane workspace and target behavior
  are unchanged.
- Routine contributions combine implementation, tests and relevant documentation.
  Exact implementation hashes are recorded at release or when an investigation
  requires them; historical evidence and owner-controlled Git operations remain.

## [0.1.0] - 2026-10-20

### Added

- Basic GitHub Actions test and static-analysis jobs for `main`/`develop`, pull
  requests and manual dispatch, with pinned actions and retained logs.
- Security disclosure policy and contributor guidance for Gitflow branches,
  pull requests, CI boundaries and coordinated vulnerability reporting.

- Linux x86-64 release packaging with a hardened host executable, corresponding
  source archive, runtime/build information and SHA-256 checksums. GDB, OpenOCD
  and shared libraries remain external; preparation does not publish a release.
- Existing remote/extended-remote connections and reviewed OpenOCD startup,
  script selection, owned lifecycle, explicit Reset/Halt and disconnect policy.
- Physical NUCLEO-F446RE and ESP32-S3 fixtures and session/TUI/tmux validation.
  Reset verification requires an actual current thread/frame and numeric PC.
  Physical RISC-V acceptance is deferred for v0.1.0; no unverified hardware
  compatibility is claimed.

- User and contributor guides, tracked product requirements, a status-oriented
  roadmap and separate maintenance/release procedures. Milestone
  evidence is integrated into validation records with hashes, failures and acceptance states;
  architecture/testing references describe the system and reproducible checks.
- Named XDG/json-c connection/source profiles, recent saved native sessions and
  action-key remapping, accepted in M13 implementation `4030df3`. Review profiles
  before starting tools; confirm replacement/deletion. Persist ordered arguments,
  scripts and source settings with bounded private atomic writes, corrupt-file
  preservation and stale-writer detection. Restore source settings asynchronously
  before execution. Remote/OpenOCD connection execution is provided by the later session work.
- Profile model/source-restore/native/TUI regressions, including relocated-source
  breakpoint resolution, remapped execution and successful-session recents.
- Embedded-first post-v0.1.0 sequencing, retaining F01–F47 IDs and
  M14–M18 release scope. Earlier peripheral/fault, firmware, RTOS/image and
  monitoring workflows; continued native maintenance/shared improvements.
  F22 flashing leads v0.4.x independently of text-channel readiness. Planning
  targets do not claim implementation or hardware support.
- Hardware-required value-change/read/access data watchpoints, with creation
  from Breakpoints, Memory and variable actions. Verify backend type, remove
  software fallback and block execution on unresolved ownership/cleanup.
  Report hit values/scope expiry and retain width/resource errors. M12 accepted
  at `f1dbb8a`; C model, native and terminal regression tests are available.
- Scoped read-only Locals/Watches with types, numeric formats, change indicators,
  explicit reads, lazy aggregate/pointer/C++ expansion and bounded array pages.
  Authorize value-producing requests before dispatch; unknown storage requires
  deliberate inspection, with no automatic MMIO polling. Accepted at `7744643`.
- C model, native C/C++/optimized and live terminal variable-inspection tests;
  corrected live test startup/redraw waits and bounded PTY diagnostic capture.
- Persistent inline Memory address/expression and Go, independent navigation/
  settings, bounded region-aware browsing, hex/ASCII,
  target-endian grouping, explicit refresh and partial/error/stale states.
  M10 accepted in implementation commit `2f951fb`.
- Embedded-first product specification and architecture review, preserving native
  Linux support and assigning remote/capability/metadata prerequisites to M15–M17.
- Live read-only CPU Registers discovered through GDB/MI, with selected-frame/
  thread values, six numeric formats, Refresh, scrolling and change indicators.
  Bounded visible reads and explicit stale/unavailable/error states; honest
  ungrouped fallback instead of guessed architecture register groups.
- C x86/ARM model and native/TUI register regressions, including format-stable
  change baselines, sparse IDs, long values and actual caller-frame selection.
- Reversible pane-local tabs in `...`: included views alone have a filled-circle
  marker, defaults can be removed, and each pane retains at least one tab.
  Minimal terminals use an ASCII marker when the circle is unavailable.
- Live code-breakpoint management through gutter/F9 and the Breakpoints view:
  conditions, ignore counts, pending/multiple locations and required hardware.
- Run to Selected for source lines/instruction addresses, preserving user
  breakpoints and cleaning up temporary state on intervening stops or errors.
- Bounded code-aware disassembly browsing through ELF metadata using libelf;
  explicit raw byte decoding, honest unknown ranges and labeled skipped gaps.
- C ELF mutation, native-GDB breakpoint/browsing and TUI regression coverage.
- Centered launcher with Monokai title/version, subtle separators,
  full-row session choices, compact icons, descriptions and chevrons. The entire
  card centers on both axes and adapts to smaller terminals without new assets
  or rendering dependencies; session and workspace behavior is preserved.
- Overflow-only horizontal bars in live Source/Disassembly, Console, Call Stack
  and Threads, with independent view offsets, drag/page/keyboard navigation and
  fixed source gutters. Vertical scrollbars are one cell wide and match the
  horizontal thumb/track style.
- Persistent surrounding assembly across steps, bounded backward/forward window
  browsing and GDB-derived preceding context. Stepping preserves the viewport
  until PC reaches a five-row top or bottom margin; the toolbar spells out
  Shift+F11. The default upper row is modestly taller, with resizing unchanged.
- Live Source/Disassembly with syntax-colored source, real instruction addresses
  and opcodes, AT&T x86-64 syntax, optional mixed source/assembly and explicit
  unavailable/stale states. Source/instruction stepping follows code-pane focus;
  Step Out and toolbar actions preserve actual stopped context.
- GDB-known file filtering, current-file find/line navigation, safe assembly
  address/symbol browsing, return to stopped context and source-directory/ordered
  prefix recovery. Browsing never changes execution; inspected caller markers
  remain distinct from actual stopped PC.
- C synthetic and actual-GDB code tests covering bounded/stale reads, -O0/-O2/
  stripped/C++ targets, relocated duplicate basenames and literal quoted paths.
  M08 adds the breakpoint UI and Run to Selected on this code model.
- Live Call Stack and Threads with mouse/keyboard frame and thread selection,
  simple argument values, stack paging, Refresh and stale/unavailable states.
- Separate stopped execution PC/thread from an inspected caller or other thread;
  reject obsolete context replies without moving execution.
- C context model and deterministic delayed-reply/lifecycle/bounds tests, plus
  real GDB API/TUI tests using recursive, multithread, optimized and stripped
  targets. Other inspection remains in later gates.
- Fixture terminal-control output now requires explicit --control-bytes;
  ordinary Attach testing retains the printed PID instead of clearing it.

- M05 native Linux launch/attach through the launcher, real Continue/Pause and
  execution state, isolated program output, GDB command input and Save Log.
- Native-session ownership and confirmed exit policy, attached-process
  detach/resume, bounded escaped logs and Linux pidfd crash-cleanup checks.
- C real-GDB API/PTY tests and C/C++ debug-target fixtures, with separate
  native and native sanitizer targets.
- Explicit --demo for the preserved simulated workspace; live inspection panes
  report unavailable instead of presenting sample data.
- M04 nonblocking GDB/MI controller with literal argv spawning, bounded command/
  I/O queues, token matching, deadlines, stale-generation rejection and owned
  process cleanup. MI3 startup negotiates asynchronous/all-stop operation with
  a restricted startup-only MI2 fallback.
- C fake-peer lifecycle/pressure and interactive PTY tests, a separate fake
  controller demonstration, and optional actual-GDB startup checks without targets.
- M03 incremental GDB/MI parser with owned records, nested values, byte-string
  decoding, bounded buffering and line-boundary error recovery.
- C protocol/fragmentation/resource-limit and allocation-failure tests, synthetic
  fixture provenance, and a standalone diagnostic decoder via `make mi-demo`.
- M00 C17 foundation with help, target-version and development-status output.
- Make build, CLI integration checks, sanitizer and static-analysis targets.
- Architecture, testing and milestone acceptance documentation.
- M01 ncursesw demo launcher/configuration and six-pane Monokai workspace,
  with shared mouse/keyboard actions, view tabs/chooser and simulated controls.
- Capability-based color fallback, compact rendering and terminal cleanup.
- C unit/CLI/PTY tests replacing the Python CLI harness.
- Prototype-aligned Monokai surfaces, compact toolbar, styled tabs, semantic
  source/table colors and renderer palette/selection checks.
- M02 split-tree workspace with mouse-resizable shared/nested dividers,
  keyboard resizing, splits right/below, tab and nested pane closing, and
  compact access to all retained panes.
- C geometry stress tests, PTY workspace interactions and an optional isolated
  tmux mouse-forwarding integration test.
- Approved development roadmap through v1.0.0 with independent implementation
  and acceptance gates, documented deferred capabilities, and per-release
  native x86-64/STM32 ARM/ESP32 Xtensa/ESP32 RISC-V regression requirements.

### Fixed

- Close unrelated inherited descriptors in the GDB child while preserving parent
  descriptors and independently owned processes.
- Invalidate variable freshness immediately after resume/context changes even
  while a request is pending, retaining object identities for stale-reply cleanup.
- Check Memory/tmux stderr redirection under fortified compilation. Validate
  optimized argument reads against reported GDB metadata and find ELF gaps with
  bounded scrolling across different linker layouts.
- Watchpoint TUI test targets intended lower panes, inline Memory address input
  and footer actions across redraws; wait for current Memory data without
  weakening application read/watchpoint safety checks.
- Opening/closing a view selector preserves disassembly cache and viewport.
- Disassembly warnings follow visible rows; backward scans skip section gaps
  consistently. Address, byte, mnemonic and operand columns align while complete
  lines remain accessible through horizontal scrolling.
- Recover stopped context from structured thread states when GDB reports running
  and then aborts hardware-breakpoint insertion without a stop notification.
- Backward assembly reads containing only a short prefix retain the forward
  listing and viewport context instead of collapsing the pane to one row.
- One-cell vertical scrollbars use vertical track/thumb glyphs; horizontal
  dashes remain confined to horizontal bars.
- Empty Source/Disassembly panes in a Loaded session say Awaiting stop; STALE
  is reserved for unavailable freshness after execution/context changes.
- Serialize GDB's CLI-derived setting commands correctly; ^done alone did not
  prove settings were applied. Real probes now verify async/all-stop values,
  while native tests verify shell-free arguments and environment fidelity.
- Disassembly navigation can reach every instruction; view row counts are
  shared by rendering and input handling.
- Scrolling moves pane viewports directly, without a secondary row highlight.
  Source/disassembly highlight their current simulated execution positions.
- Remove the preloaded demo breakpoint; add clickable/draggable vertical
  scrollbars when pane content exceeds the viewport.
- Inactive tabs immediately lose the stale inverse highlight after switching.
  Focus follows the promoted active tab, mouse activation keeps normal styling,
  and subsequent keyboard activation addresses the intended tab.

Native launch/control/console, context, code inspection, code/data breakpoints,
Registers, Memory, scoped Locals/Watches and basic profiles/action keys are live.
Remote/OpenOCD sessions and the named STM32/S3 physical workflows are live.
Native launch requires GDB 17+ to preserve literal arguments with shell startup
disabled. CI exercises Ubuntu 26.04/GDB 17+ runtime behavior and Ubuntu 24.04
static analysis/fortified test compilation. Specialized inspectors remain planned;
broader compatibility requires its own evidence.
