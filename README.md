# The Bugger

A terminal-native debugger frontend implemented in C for firmware and
system-level C/C++ development across x86, ARM, and other GDB-supported
architectures, alongside native application debugging. GDB provides the debugger
engine through its structured MI interface; OpenOCD is an optional server for
targets that use it.

Release acceptance requires regression testing on native Linux x86-64,
STM32/ARM (initially the NUCLEO-F446RE through OpenOCD), ESP32/Xtensa, and
ESP32/RISC-V. QEMU-based ARM and RISC-V runs provide supplementary coverage
where available. Compatibility claims identify the setups actually tested;
these acceptance targets do not limit the product architecture.

**Current development build:** native Linux launch/attach, Continue/Pause,
program output, GDB console commands and Save Log are functional. Inspection
panes explicitly report unavailable; stepping, breakpoint controls, profiles
and remote/OpenOCD workflows are not implemented yet. `--demo` opens the
separate, explicitly simulated visual reference.
`0.1.0` is the target version, not a declaration of release readiness.

## Build and run

Build on Linux with a C17 compiler (GCC or Clang), GNU Make, pkg-config and the
ncursesw development headers/library. Native sessions require a compatible GDB
in PATH or an explicitly selected executable. Linux pidfds provide owned-inferior
crash recovery; unavailable support is reported. Application and test harnesses
are C; a C++ compiler is used only for the optional debug-target fixtures.

```sh
make
./build/the-bugger --version
./build/the-bugger --help
./build/the-bugger
```

`--version` prints `the-bugger 0.1.0`. Help/version work without a terminal.
Interactive invocation requires a TTY and usable TERM. Invalid arguments and
non-TTY interactive execution report an actionable error and exit 1.

## Native Linux sessions

Choose **Linux application**, configure **Launch** or **Attach**, then choose
**Review session** and **Start native**. Inputs remain available after errors.

Launch accepts an executable ELF, arguments, working directory, optional
`NAME=value` environment entries and GDB executable. Arguments/environment use
whitespace, quotes and backslashes for grouping, without shell expansion. For
example, arguments `"two words" "" '$HOME;$(false)'` preserve a spaced argument,
an empty argument and literal metacharacters. An environment entry such as
`THE_BUGGER_TEST="hello world"` preserves its value. Paths are entered literally.
Environment overrides are session-only.

The session starts **Loaded**. Click **Continue** or press **F5** to run; click
**Pause** to interrupt. State and PID come from GDB. In Console, **Command** opens
a clearly labelled GDB command field; **Send** executes through MI. Errors leave
the session usable. Other inspection views show unavailable messages and never
substitute sample registers, source, assembly or memory.

A local fixture is available for a first run:

```sh
make fixtures
./build/the-bugger
```

Choose `./build/debug-fixture` as the executable. Continue prints its PID,
directory, arguments, environment marker and ticks; Pause stops it. Console
commands such as `info program` and `echo console-ok\n` exercise debugger output.
The [native checklist](docs/testing.md#m05-manual-acceptance--real-native-linux)
covers configuration errors, output, logs, cleanup and attach.

Attach accepts a permitted PID and optional symbol ELF. GDB checks Linux ptrace
permissions; the frontend does not change system security settings. For a
reproducible same-owner fixture, run `./build/debug-fixture --allow-attach` in
another terminal and enter its printed PID. That option changes only the
fixture's per-process tracing permission. An attached process retains its
original stdout/stderr destination.

**Session** and **Quit** ask before closing an active native session. A launched
program is terminated; an attached process is detached and resumed. Cancel keeps
the session. Outside text fields, `q` closes/backs out of the current screen;
from the workspace it closes the session after confirmation and returns to the
launcher. At the launcher `q` stays open. `Ctrl+Q` quits the application from any
screen, with confirmation for an active session. Text fields accept `q` normally;
Escape cancels input. Ctrl+C/SIGINT, SIGTERM and SIGHUP run cleanup and restore the terminal.
Unexpected disconnect and unconfirmed target cleanup are reported explicitly.

Program output is captured in an isolated PTY, with stdout/stderr combined.
Control and non-ASCII bytes are escaped before display. Console retains up to
512 bounded lines; scroll to inspect older entries and return to the end to
follow output. **Save Log** creates a new private file containing retained text.
Existing files are preserved; open/write failures retain the input for correction.
Interactive program input and a full terminal emulator are not provided yet.

## Workspace and controls

The single default tiled workspace shows Source, Disassembly and Registers above
Memory, Locals and Console. Each pane has tabs and a view chooser. The live
workspace currently implements Console; other live inspection views are marked
unavailable. With `--demo`, views display the simulated Cortex-M4F reference,
including sample source/assembly, values and execution actions. Embedded/remote
configuration still opens an explicit preview and launches no server.

The active tab moves to the left. Mouse activation leaves inactive tabs in their
normal style; keyboard focus follows the active tab. Use clicks or Tab/Shift+Tab,
arrows and Enter. In fields use Left/Right, Home/End, Backspace/Delete and Ctrl+U.
Escape goes back; Quit, `q` in the launcher/workspace, or Ctrl+Q requests exit.
Function keys have visible alternatives. Stepping, breakpoint and Reset actions
remain unavailable in live sessions; demo actions are explicitly simulated.

Click `...` for view selection and pane actions. Wheel or a focused body with
arrows/PageUp/PageDown scrolls; vertical scrollbar tracks page and thumbs drag.
Keys `1`–`9` focus retained pane IDs; `[`/`]` cycles panes. There are no layout
presets, layout selector, floating windows or Vim modes.

Drag one-cell gutters to resize columns, rows or nested splits. Pane actions
split right/below, close a tab, close a nested pane or enter keyboard resizing.
In resize mode Tab/Shift+Tab selects a divider, arrows move it, and Enter/Escape
finishes. Closing a nested pane expands its sibling. Each pane retains at least
one tab; each of the six root tiles retains at least one pane.

The workspace supports up to 32 panes with a minimum tiled size of 20x7 cells per
pane. At least 112x28 displays all six default panes; 140x44 is comfortable.
Smaller terminals show a focused pane with controls to reach every retained pane.
Below 48x18 a resize message preserves state. Tabs, splits and proportions survive
shrinking and return when space permits.

Monokai Pro uses consistent panel, header, tab and footer surfaces. Direct-color
terminfo enables the exact palette; otherwise 256/8-color or monochrome is used.
Standalone Alacritty advertising truecolor uses `alacritty-direct` when installed.
`NO_COLOR=1 ./build/the-bugger` forces monochrome. Use a TERM entry supported by
your terminal; the application does not alter terminal palettes or tmux settings.

In tmux, opt into mouse handling with `set -g mouse on`. Function keys depend on
the sequences the terminal/multiplexer reports; use clickable alternatives when
intercepted. Truecolor forwarding also requires matching terminal capabilities;
an inherited COLORTERM alone does not establish support.

## Tests

```sh
make test
make test-native
make sanitize
make sanitize-native
make analyze
make CC=clang BUILD_DIR=build/clang test
make test-tmux
```

`test` runs deterministic C unit/CLI/PTY suites without GDB or hardware; visual
regressions explicitly use `--demo`. `test-native` runs actual GDB and C/C++ debug
fixtures through the native API and TUI. It requires ptrace permission and tests
launch/attach, output, console/log errors, crash recovery and cleanup. Sanitizer
targets keep instrumentation in `build/sanitize/`; debug targets themselves stay
uninstrumented. LeakSanitizer cannot run inside a tracing sandbox.

`analyze` requires clang-tidy. `test-tmux` requires tmux and an available local
socket; it owns an isolated temporary server and never modifies the user's server.
Physical visual/input acceptance and real-board validation remain separate.
See [testing](docs/testing.md), [test coverage](tests/README.md) and
[progress](docs/progress.md) for exact evidence and outstanding gates.

Artifacts are in ignored `build/`; `make clean` removes it. Compiler/build flags,
`PKG_CONFIG` and `BUILD_DIR` are configurable through Make. Use a fresh build
directory when changing compiler/flags. No Python or external test framework is
required.

## Backend development tools

`make test-mi` and `make mi-demo` exercise the owned incremental MI parser;
[synthetic fixture notes](tests/fixtures/mi/README.md) distinguish valid records,
deliberate failures and decoder exit codes. `make test-controller` and
`./build/controller-demo ./build/fake-mi` exercise an explicitly fake C peer,
without starting a target. These utilities are separate from the native launcher.

`make test-gdb-startup` checks actual native GDB startup, setting values, feature
enumeration and MI exit without an inferior. The resulting
`./build/test-gdb-startup arm-none-eabi-gdb` checks an installed ARM GDB the same
way; it is not firmware/hardware acceptance.

## Project direction

Live source/disassembly, stepping, breakpoints, architecture-neutral registers,
independent Memory views, local variables, watches, frames and threads follow
in bounded development gates. x86-64 disassembly will use AT&T syntax. Basic
profiles and key remapping are planned. Firmware sessions will use an appropriate
GDB/server, optionally OpenOCD, without assuming ARM registers or a universal
probe backend.

The [roadmap](docs/plan.md) defines v0.1.0 scope and the expected path to v1.0.0,
including editing, embedded text channels, additional inspection views and themes.
See the [architecture](docs/architecture.md) and
[visual reference](docs/design/prototype.html) for implementation boundaries and
the workspace design.
