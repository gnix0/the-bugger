# the-bugger

A terminal-native debugger frontend implemented in C for firmware and
system-level C/C++ development across x86, ARM, and other GDB-supported
architectures, alongside native application debugging. GDB provides the debugger
engine through its structured MI interface; OpenOCD is an optional server for
targets that use it.

For v0.1.0, validation will focus on native Linux x86-64 and the STM32
NUCLEO-F446RE through OpenOCD. These are the first release's acceptance targets;
the product architecture and intended use extend beyond them. Other targets
will require their own validation before compatibility is claimed.

**Development preview:** bare invocation
opens the clickable launcher and a Monokai six-pane workspace. Configuration,
source, instructions, registers, memory, variables, logs and execution actions
are explicitly simulated. No GDB/OpenOCD connection, tool validation, profile
persistence or hardware access is implemented yet. Pane dragging and dynamic
splits are planned.
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

Use clicks or Tab/Shift+Tab, arrows and Enter. Escape goes back; Session reopens
the launcher; Quit, `q` in the launcher/workspace, or Ctrl+Q exits. In fields use
Left/Right, Home/End, Backspace/Delete and Ctrl+U to clear. The toolbar provides
simulated F5/F9/F10/F11/Shift+F11 actions and clickable alternatives; Reset asks
for confirmation. Tab into a pane body for arrow/PageUp/PageDown scrolling, or
use the wheel. Click `...` for the view chooser; arrows/Enter or clicks select.
Keys `1`–`6` focus panes. Future-view tabs explicitly say **NOT IMPLEMENTED**.

At least **112 columns by 28 rows** displays all six panes. Smaller terminals
show one focused pane with six accessible pane buttons; below 48x18 a controlled
resize message preserves state.

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
```

Artifacts are kept in ignored `build/`. `make clean` removes that directory.
The compiler defaults to `cc`; `CC`, `CPPFLAGS`, `CFLAGS`, `LDFLAGS`, `LDLIBS`,
`PKG_CONFIG`, and `BUILD_DIR` can be set on the Make command line. Use a fresh
build directory or `make clean` when changing compiler or build flags.
See [testing](docs/testing.md) for suite coverage and terminal checks.

## Project direction

The debugger is being developed around live source/disassembly, execution
control, breakpoints, architecture-neutral register inspection, memory, local
variables, watches, frames and threads. The configurable workspace will support
mouse-resizable panes and dynamic tiled splits. Saved session profiles and
key remapping are also planned.

Native sessions will use a local GDB executable. Firmware sessions will use a
target-compatible GDB and an appropriate GDB server, optionally OpenOCD.
The design keeps target architectures separate from terminal presentation.
For x86-64, disassembly will use AT&T syntax.

See the [architecture](docs/architecture.md), [development plan](docs/plan.md),
[progress](docs/progress.md), and [visual reference](docs/design/prototype.html)
for implementation details and current coverage.
