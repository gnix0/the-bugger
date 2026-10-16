# Tests

Run `make test` from the repository root. All tests and support code are C,
built with the same strict compiler flags as the application. No Python or
external test framework is required.

- `test-unit`: demo state transitions, bounded logs, tab preservation,
  half-open hit targets and bounds/non-overlap over 76,800 screen/focus cases.
- `test-cli`: help/version, non-TTY diagnostics and invalid/literal arguments.
- `test-tui`: starts the actual ncurses application in an isolated PTY, drives
  keyboard and SGR mouse input, and reads its ASCII screen labels. Covers all
  three session previews, editing/retention, toolbar and function keys, reset
  confirmation, tabs/view selection, compact/tiny resize, direct/256-color and
  monochrome rendering, and normal/SIGINT/SIGTERM/SIGHUP terminal restoration.

PTY expectations and shutdown have five-second deadlines. Each child gets its
own controlling terminal; no user's terminal settings are changed by the tests.
The small VT reader supports the ASCII labels used for assertions; it is not a
general terminal emulator or a visual-fidelity oracle. UTF-8 input remains a
manual acceptance check. Color tests need the xterm-direct, xterm-256color and
vt100 terminfo entries installed with ncurses on the development machine.

`make sanitize` runs the same C tests with ASan/UBSan in `build/sanitize/`.
LeakSanitizer needs execution outside ptrace-based agent sandboxes. No check
is disabled to obtain a pass. `make analyze` runs clang-tidy over application
sources. See `docs/testing.md` for commands and the manual/tmux checklist.

These tests establish M01 UI scaffold evidence, not live debugging or complete
FR-06/07/08/09/10/11/12/13/14/15/16/17 acceptance. GDB/MI fixtures arrive in M03;
real native C/C++ fixtures in M04; real board evidence remains required in M07.

`unit/test_render.c` checks direct-color palette pairs, semantic source/register
colors, full-width selected rows and the monochrome active-tab indicator using
an ncurses screen backed by temporary files. It needs `xterm-direct` terminfo,
as does the existing PTY color test. Visual acceptance remains manual.
The standalone Alacritty capability-selection case additionally requires the
installed `alacritty` and `alacritty-direct` terminfo entries.
