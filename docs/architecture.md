# Architecture

## Current implementation (M00)

`src/main.c` handles the deliberately small CLI. The target version has one
definition in `include/the_bugger/version.h`. There is no debugger transport,
session state, terminal initialization, subprocess launch, or configuration
persistence yet. The build uses C17 and treats enabled compiler warnings as
errors. Python is used only to exercise the executable in tests.

## Approved direction

The corrected UI uses one configurable workspace: Source / Disassembly /
Registers above Memory / Locals / Console. Follow AGENTS.md and the approved
HTML visual/interaction reference at `docs/design/prototype.html`.
Preserve the Monokai appearance, toolbar,
tabs, pane menus and startup configuration within terminal constraints.
There are no layout presets or selectors. Resizing and dynamic splits edit the
current tiled workspace; optional geometry restoration restores the user's own
workspace. See `docs/plan.md` for the revised milestone deliverables and gates.

Introduce modules only as their milestone needs them:

| Milestone | Boundary | Responsibility |
|---|---|---|
| M01 | TUI | ncursesw rendering, theme, input and semantic actions |
| M02 | Layout | Binary split tree, tab state, geometry and hit testing |
| M03 | GDB/MI and process transport | Incremental parsing, tokens, async events, deadlines and owned children |
| M04 | Session and model | Native launch/attach, selected context and live source/disassembly/breakpoints |
| M05 | Inspection model | Architecture-neutral registers, memory, variables, frames and threads |
| M06 | Remote/OpenOCD and profiles | Script catalog, managed server, existing endpoints and XDG persistence |

One event loop will service terminal input, child pipes, signals and deadlines
using nonblocking I/O and `poll()`. Rendering consumes model snapshots; views
do not parse debugger output. Keyboard and mouse controls dispatch the same
actions. Bounded processing keeps output floods from starving input.

Use explicit MI3, with MI2 fallback only on interpreter startup failure before
connecting to a target. Enable asynchronous execution and retain all-stop
semantics for v0.1.0. Parse nested structured values and streams rather than
human CLI output. Correlate replies by token and reject old session/stop/context
generations. Publish coherent stopped snapshots; show stale data while running.

Launch owned tools through argv vectors. Disable GDB's inferior startup shell,
quote MI arguments separately, and isolate inferior output in a PTY. Sanitize
control characters before presentation. Preserve ownership information so
external servers and attached processes are not killed during cleanup.

Use ncursesw capability checks for color and mouse behavior, with 256-color
and monochrome fallbacks. Compact mode preserves the split tree and view state.
Function keys have visible alternatives and remapping; terminal/tmux acceptance
must include click, wheel, drag, resize and unsupported key sequences.

OpenOCD discovery is a script catalog, not a Tcl interpreter. A board script
stands alone unless the user explicitly chooses additional files. Display exact
ordered argv and paths; start managed servers on loopback. Never add implicit
flash/erase/reset/resume commands. Arbitrary user scripts can have side effects.

Use json-c for versioned JSON profiles, structured argv, ordered scripts,
layout and key mappings. Persist under the specified XDG config directory with
atomic replacement and recoverable validation errors. Environment overrides
are session-only to avoid persisting credentials. Memory outside known ordinary
RAM is read on request; no background peripheral polling.

## Release boundaries

Validate native Linux before STM32 hardware. Exclude in-application flashing,
CMSIS-SVD, RTOS, trace/profiling, other architecture validation, IDE/editor
features and plugin systems from v0.1.0. Public library APIs are not introduced
by M00. Follow AGENTS.md for complete requirements and acceptance gates.
