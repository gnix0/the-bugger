# Tests

Run `make test` from the repository root. All tests and support code are C,
built with the same strict compiler flags as the application. No Python or
external test framework is required.

- `test-unit`: demo state transitions, bounded logs, tab closing and per-view
  state preservation.
- `test-layout`: tree ownership, unique pane IDs, geometry bounds/non-overlap,
  gutter coverage, recursive minimum sizes, split/collapse, compact restoration,
  76,800 size/focus cases, 12,000 seeded operations, capacity and pool reuse.
- `test-render`: exact palette, semantic colors, selected rows and monochrome tabs.
- `test-cli`: help/version, non-TTY diagnostics and invalid/literal arguments.
- `test-tui`: starts the actual ncurses application in an isolated PTY, drives
  keyboard and SGR mouse input, and reads its ASCII screen labels. Covers all
  three session previews, editing/retention, toolbar and function keys, reset
  confirmation, tabs/view selection, compact/tiny resize, direct/256-color and
  monochrome rendering, and normal/SIGINT/SIGTERM/SIGHUP terminal restoration.
- `test-workspace`: real PTY mouse press/move/release, global column/row and
  internal dividers, nested seventh/eighth views, tab/split closing, compact
  pane access, keyboard resizing and monochrome keyboard-only splitting.
  P00 regressions inspect actual inactive-tab inverse styling after promotion,
  repeated mouse switching, subsequent Enter and keyboard focus in direct color,
  256-color and monochrome.

PTY expectations and shutdown have five-second deadlines. Each child gets its
own controlling terminal; no user's terminal settings are changed by the tests.
The small VT reader supports ASCII labels plus ncurses line/scroll-region
operations used when panes move and inverse attributes (excluding extended-color
parameters); it is not a
general terminal emulator or a visual-fidelity oracle. UTF-8 input remains a
manual acceptance check. Color tests need the xterm-direct, xterm-256color and
vt100 terminfo entries installed with ncurses on the development machine.

`make sanitize` runs the same C tests with ASan/UBSan in `build/sanitize/`.
LeakSanitizer needs execution outside ptrace-based agent sandboxes. No check
is disabled to obtain a pass. `make analyze` runs clang-tidy over application
sources. See `docs/testing.md` for commands and the manual/tmux checklist.

These tests establish UI/layout evidence, not live debugging or complete
FR-06/07/08/09/10/11/12/13/14/15/16/17 acceptance. GDB/MI fixtures arrive in M03;
fake transport fixtures in M04, real native C/C++ fixtures from M05, native
acceptance in M14 and real board evidence in M17. Every release retests native
x86-64 and the owner's STM32/ARM, ESP32/Xtensa and ESP32/RISC-V hardware;
QEMU ARM/RISC-V adds coverage where available and never replaces board evidence.

`unit/test_render.c` checks direct-color palette pairs, semantic source/register
colors, full-width selected rows and the monochrome active-tab indicator using
an ncurses screen backed by temporary files. It needs `xterm-direct` terminfo,
as does the existing PTY color test. Visual acceptance remains manual.
The standalone Alacritty capability-selection case additionally requires the
installed `alacritty` and `alacritty-direct` terminfo entries.

`make test-tmux` separately runs `integration/test_tmux.c` through an isolated
tmux server. It verifies actual tmux input forwarding for mouse drag, pane menu
clicks, tab styling/focus, splitting, F10, compact resize and exit. It requires tmux, a usable local
socket and `tmux-256color` terminfo; it never uses the owner's existing server.
It removes its temporary config/server even on a test failure. This optional
environment test is separate from the default and sanitizer suites.
