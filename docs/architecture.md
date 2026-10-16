# Architecture

## Current implementation (M01)

`src/main.c` handles help/version and starts the terminal application.
`src/tui/app.c` owns startup/configuration/review/workspace pages, keyboard
and mouse controls, form editing and terminal lifetime. Rendering and theme
selection are in separate files. `src/tui/demo.c` owns explicitly simulated
sample data, demo actions, tabs and fixed M01 geometry. These samples are never
debugger-domain state and must be replaced by live data in real sessions.

The build and tests are C17 with strict warnings; Make is the only build driver.
C tests use POSIX pipes, processes and PTYs. Python tests were replaced at the
owner's request. The only current application library dependency is ncursesw.
There is no debugger transport, tool subprocess launch or persistence yet.

The UI waits for input with a 100 ms ncurses timeout and redraws after events
rather than on every timeout. Raw input preserves function keys and Ctrl+Q;
Ctrl+C and SIGINT/SIGTERM/SIGHUP leave through normal cleanup. Two module-owned
globals are documented: a signal-safe exit flag and the screen's color-enabled
flag. Mouse and keyboard share action dispatch and rendered hit regions.

Theme roles separate workspace, panel, raised header, notice and footer
surfaces. Selected-row variants retain semantic foreground colors. Direct-color
terminfo supplies exact RGB values; a matching Alacritty direct entry can be
selected when standalone Alacritty advertises truecolor. The app never changes
the terminal palette or tmux configuration. See the
[prototype mapping](design/tui-mapping.md) for rendering adaptations.

Six fixed rectangles follow the prototype's 36/34/30 columns and 56/44 rows
(rounded to cells). The scaffold preserves per-view scroll/selection and tabs
through compact rendering. M02 introduces interactive split-tree geometry,
dragging and closing; M01 does not expose deceptive split/resize controls.

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
