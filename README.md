# The Bugger

A terminal-native debugger frontend implemented in C for firmware and
system-level C/C++ development across x86, ARM, and other GDB-supported
architectures, alongside native application debugging. GDB provides the debugger
engine through its structured MI interface; OpenOCD is an optional server for
targets that use it.

Release acceptance requires regression testing on native Linux x86-64,
STM32/ARM (initially the NUCLEO-F446RE through OpenOCD), ESP32/Xtensa, and
ESP32/RISC-V. QEMU-based ARM and RISC-V runs provide supplementary coverage
where available. Compatibility claims will identify the setups actually tested;
these acceptance targets do not limit the product architecture.

**Development preview:** bare invocation
opens the clickable launcher and a Monokai six-pane workspace. Configuration,
source, instructions, registers, memory, variables, logs and execution actions
are explicitly simulated. No GDB/OpenOCD connection, tool validation, profile
persistence or hardware access is implemented yet. The workspace supports
pane resizing and dynamic tiled splits.
`0.1.0` is the target version; it is not a declaration of release readiness.

## Build and run

Build with a C17 compiler (GCC or Clang), GNU Make, pkg-config and the ncursesw
development headers/library. The application and tests are written in C.
Sanitizer support is needed for `make sanitize` and clang-tidy for `make analyze`.
GDB, OpenOCD and a board are not required to run the demo.

```sh
make
./build/the-bugger --version
./build/the-bugger --help
./build/the-bugger
```

`--version` prints `the-bugger 0.1.0`. Help/version work without a terminal.
Bare invocation requires a TTY and usable TERM; redirected execution reports
an actionable diagnostic and exits with status 1. Invalid arguments also exit 1.

Select a session, edit its sample configuration, choose **Review preview**, then
**Preview workspace**. Nothing is executed or saved. The Cortex-M4F sample from
the visual reference remains clearly marked DEMO in every session preview.

## Workspace and controls

The default tiled workspace shows Source, Disassembly and Registers above
Memory, Locals and Console. Each pane has tabs and a view chooser. Watches,
Call Stack, Breakpoints and Threads are available as demo views. Symbols,
Peripherals and Stack are explicitly marked as unimplemented.

The active tab moves to the left. Mouse activation leaves inactive tabs in their
normal style; keyboard focus remains visible and follows the activated tab.

Use clicks or Tab/Shift+Tab, arrows and Enter. Escape goes back; Session reopens
the launcher; Quit, `q` in the launcher/workspace, or Ctrl+Q exits. In fields use
Left/Right, Home/End, Backspace/Delete and Ctrl+U to clear. The toolbar provides
simulated F5/F9/F10/F11/Shift+F11 actions and clickable alternatives; Reset asks
for confirmation. Tab into a pane body for arrow/PageUp/PageDown scrolling, or
use the wheel. Click `...` for the view chooser; arrows/Enter or clicks select.
Scrolling moves the viewport directly. Source and Disassembly highlight their
current simulated execution positions; stepping moves these highlights.
Overflowing panes have a vertical scrollbar: click the track to page or drag
the thumb. Demo sessions start with no breakpoints.
Keys `1`–`9` focus retained pane IDs; `[`/`]` cycles all panes. Future-view tabs
explicitly say **NOT IMPLEMENTED**.

Drag the one-cell gutters to resize columns, rows or nested splits. In `...`,
choose **Pane actions >** to split right/below, close the selected tab, close a
nested pane or enter keyboard resizing. In resize mode, Tab/Shift+Tab selects a
divider, arrows move it, and Enter/Escape finishes. Pane menus support wheel and
arrow navigation when their choices exceed the available height.

Splitting keeps the original pane and opens a Disassembly pane beside/below it;
use its view chooser to change the view. Closing a nested pane expands its
sibling. Each pane keeps at least one tab, and each of the six root tiles keeps
at least one pane. The workspace supports up to 32 panes, with a minimum tiled
size of 20 columns by 7 rows per pane. Splits that cannot fit report a diagnostic.

At least **112 columns by 28 rows** displays all six panes. Smaller terminals
show one focused pane. The initial workspace has six accessible pane buttons;
with additional panes, Prev/Next controls and `[`/`]` reach every pane. Splits,
tabs and proportions survive terminal shrinking and return when space permits.
Below 48x18 a controlled resize message preserves state.

The Monokai Pro workspace uses distinct panel, tab, toolbar and footer surfaces,
with semantic source and value colors. Direct-color terminfo enables the exact
24-bit palette; otherwise the application uses 256/8-color or monochrome.
Standalone Alacritty advertising truecolor uses its matching `alacritty-direct`
entry when installed. `NO_COLOR=1 ./build/the-bugger` forces monochrome, retaining
focus, tab and change indicators. Do not change TERM to an entry your terminal
does not support.

In tmux, opt into mouse handling with `set -g mouse on`. Special keys depend on
the terminal's reported sequences; use the clickable toolbar when a key is
intercepted. The application does not change tmux configuration.
Truecolor inside tmux also requires RGB forwarding and a matching direct-color
terminal description; an inherited `COLORTERM` alone does not establish support.

## Build checks

```sh
make test
make sanitize
make analyze
make CC=clang BUILD_DIR=build/clang test
make test-tmux
```

Artifacts are kept in ignored `build/`. `make clean` removes that directory.
The compiler defaults to `cc`; `CC`, `CPPFLAGS`, `CFLAGS`, `LDFLAGS`, `LDLIBS`,
`PKG_CONFIG`, and `BUILD_DIR` can be set on the Make command line. Use a fresh
build directory or `make clean` when changing compiler or build flags.
See [testing](docs/testing.md) for suite coverage and terminal checks.
`test-tmux` additionally requires tmux and uses an isolated temporary server to
check tab styling/focus, mouse forwarding, dragging, splitting and terminal resizing.

## MI parser development tools

An independent C parser handles structured GDB/MI records, incremental input,
nested values, escaped byte strings and malformed-record recovery. It is not
yet connected to the workspace; all debugger views remain simulated.

```sh
make test-mi
make mi-demo
./build/mi-dump --chunk 1 tests/fixtures/mi/invalid.mi
```

The demo decodes 22 synthetic records one byte at a time without launching GDB.
The invalid fixture deliberately reports six errors, recovers for the following
two records, and exits 1. The decoder also accepts a file or standard input;
control bytes are printed as escapes. See the
[fixture notes](tests/fixtures/mi/README.md) and
[manual checks](docs/testing.md#m03-manual-acceptance).

## Project direction

The debugger is being developed around live source/disassembly, execution
control, breakpoints, architecture-neutral register inspection, memory, local
variables, watches, frames and threads. The configurable workspace provides
mouse-resizable panes and dynamic tiled splits. Saved session profiles and
key remapping are planned.

Native sessions will use a local GDB executable. Firmware sessions will use a
target-compatible GDB and an appropriate GDB server, optionally OpenOCD.
The design keeps target architectures separate from terminal presentation.
For x86-64, disassembly will use AT&T syntax.

The [roadmap](docs/plan.md) defines the v0.1.0 debugging scope and the expected
path to v1.0.0, including editing, embedded text channels, additional inspection
views and themes. These are planned capabilities, not features of the current
demo. Release checks and actual coverage are documented in
[testing](docs/testing.md) and [progress](docs/progress.md).

See the [architecture](docs/architecture.md), [development plan](docs/plan.md),
[progress](docs/progress.md), and [visual reference](docs/design/prototype.html)
for implementation details and current coverage.
