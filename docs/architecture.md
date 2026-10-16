# Architecture

## Current implementation (M02)

`src/main.c` handles help/version and starts the terminal application.
`src/tui/app.c` owns startup/configuration/review/workspace pages, keyboard
and mouse controls, form editing and terminal lifetime. Rendering and theme
selection are in separate files. `src/tui/demo.c` owns explicitly simulated
sample data, demo actions and tabs. The layout module owns tiled geometry.
These samples are never
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

The workspace preserves per-view scroll/selection and tabs through compact
rendering, with interactive split-tree geometry, dragging and closing as
described below.

P00 preserves active-tab promotion to the left. On tab activation, focus moves
to that pane's first tab control, which will represent the promoted tab on the
next paint. Mouse input suppresses the additional inverse keyboard-focus marker;
keyboard input restores it. This prevents the old numeric control index from
highlighting/activating a different tab after reordering. The active-tab theme
and monochrome selection indicator remain intact.

## Approved direction

The corrected UI uses one configurable workspace: Source / Disassembly /
Registers above Memory / Locals / Console. Follow AGENTS.md and the approved
HTML visual/interaction reference at `docs/design/prototype.html`.
Preserve the Monokai appearance, toolbar,
tabs, pane menus and startup configuration within terminal constraints.
There are no layout presets or selectors. Resizing and dynamic splits edit the
current tiled workspace. Rich geometry/state restoration is F01 (v0.1.4), not
v0.1.0. F06 (v0.2.1) will decide how to organize the later view expansion;
pages are a candidate, not an approved current layout. See [the roadmap](plan.md).

Introduce modules only as their milestone needs them:

| Milestone | Boundary | Responsibility |
|---|---|---|
| M01 | TUI | ncursesw rendering, theme, input and semantic actions |
| M02 | Layout | Binary split tree, tab state, geometry and hit testing |
| M03 | GDB/MI parser | Owned incremental structured records and deterministic corpus |
| M04 | Process/MI controller | Nonblocking queues, tokens, async state, generations, deadlines and owned children |
| M05 | Native session | Launch/attach, Continue/Pause, console, isolated output and Save Log |
| M06–M08 | Context and code | Threads/frames, synchronized code/stepping/source recovery and code breakpoints/run-to |
| M09–M12 | Inspection | Registers, independent Memory, lazy variable trees and hardware data watchpoints |
| M13 | Persistence | Basic connection/source profiles, recent sessions and key mappings |
| M14 | Native integration | Complete native validation before remote/hardware acceptance |
| M15–M16 | Remote/OpenOCD | Existing endpoints, script catalog, managed server and explicit target actions |
| M17–M18 | Release validation | Real boards and every-release regression/acceptance |

One event loop will service terminal input, child pipes, signals and deadlines
using nonblocking I/O and `poll()`. Rendering consumes model snapshots; views
do not parse debugger output. Keyboard and mouse controls dispatch the same
actions. Bounded processing keeps output floods from starving input.

Use explicit MI3, with MI2 fallback only on interpreter startup failure before
connecting to a target. Enable asynchronous execution and retain all-stop
semantics for v0.1.0. Parse nested structured values and streams rather than
human CLI output. Correlate replies by token and reject old session/stop/context
generations. Publish coherent stopped snapshots; show stale data while running.
Use small owned MI-tree and correlated-request interfaces; replies carry enough
session/stop/context identity to reject obsolete work. Console commands without
an MI equivalent still travel through MI; do not parse human CLI text into state.

Separate actual stopped PC from selected caller-frame location. Source focus
selects source stepping, Disassembly focus instruction stepping; toolbar and
inspection-pane clicks retain the last code-pane mode. Display it explicitly.
Code highlights use GDB's mappings even for optimized/inlined/tail-call code;
do not infer statement changes from instruction counts. Browsing changes neither
PC nor execution markers. x86-64 uses AT&T syntax; mixed source/assembly falls
back honestly if source is absent. Source search directories and ordered prefix
substitutions use GDB, preserving consistent source/breakpoint identities.

Introduce typed view-instance settings independently of shared target snapshots.
Each Memory instance retains its own address/expression, grouping and scroll.
Fetch only bounded visible ranges/expanded children. MI variable objects support
lazy Locals/Watches and array paging; delete/recreate them as context changes.
Conservatively reject mutating automatic expressions/inferior calls. Manual GDB
console operations are deliberate; dedicated value editing is v0.2.0. Register
names/values come from the target; group only where structured metadata supports
it, otherwise use an ungrouped display. No CLI-text or hardcoded-ARM canonical set.

Breakpoint properties expose Default / Require hardware. Data watchpoints are
hardware-assisted only in v0.1.0: reconcile the backend-reported type and reject
software fallback, width/slot/support errors explicitly. Temporary run-to state
must be distinguishable from user breakpoints and cleaned up on other stops.

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
Ask on embedded disconnect whether to remain halted or resume where supported.
Expose limitations/failures rather than promising state after an arbitrary
server's disconnect hooks; never silently replace externally owned scripts.
Explicit confirmed Reset/Halt is separate from connecting. Native Launch may
Restart with confirmation; attached native processes have no Reset action.

Use json-c for versioned JSON profiles, structured argv, ordered scripts,
source lookup/mappings, recent sessions and key mappings. Persist under the XDG config directory with
atomic replacement and recoverable validation errors. Environment overrides
are session-only to avoid persisting credentials. Memory outside known ordinary
RAM is read on request; no background peripheral polling.
Rich workspace/watch/breakpoint restoration is deferred to F01; shareable
import/export with personal/shared separation is F12. Environment overrides are
not saved. Save Log exports the retained readable log, with bounded retention
and visible file errors; advanced filters/recording and program input come later.

## Release boundaries

Validate native Linux first, then STM32 and the owner's Xtensa/RISC-V ESP32
hardware. Every release, including patches, repeats that architecture matrix;
QEMU ARM/RISC-V adds coverage where available without replacing boards or adding
a simulator product feature. Configuration details are recorded when testing,
not demanded during P00. Unsupported target-specific features are explicit.

Exclude in-application flashing, editing, peripheral/SVD/RTOS-specific inspectors,
live monitoring, trace/profiling, IDE/editor features and plugin systems from
v0.1.0. Future gates in docs/plan.md define dependencies: embedded text near
v0.4.0, built-in themes near v0.5.0, specialized stops near v0.7.x, plotting
near v0.8.x, automation/custom themes and multicore coordination near v1.0.0.
No new frontend macro language or built-in/dedicated simulator integration.
Public library APIs are not introduced. Follow AGENTS.md for release acceptance.

## M02 tiled workspace

`src/layout/layout.c` is independent of curses and debugger data. Six root tiles
retain the approved 36/34/30 column and 56/44 row proportions. Each tile contains
a binary split tree. A leaf names a stable pane slot; an internal node stores
its direction, child indices and fixed-point ratio. The workspace owns a bounded
pool of 32 pane slots and 64 tree nodes, avoiding heap ownership complexity.
Closed nodes/slots are reused. There is no preset catalog or layout selector.

Geometry uses half-open rectangles and one-cell gutters. Recursive subtree
minimums constrain resizing (each leaf needs 20 columns and 7 rows); globally
shared row/column dividers respect all affected subtrees. New splits require
currently available space. Closing a nested leaf promotes its sibling into
the parent; a root leaf cannot close. Tabs retain per-view selection/scroll,
and each pane retains at least one tab. These rules follow the HTML prototype.

Geometry is recomputed after input, without changing requested ratios on
terminal resize. If the grid or its subtrees cannot fit, only the focused leaf
is painted; compact Prev/Next controls and `[`/`]` reach every retained pane.
Returning to sufficient dimensions restores the tree. Resizing a terminal during
a drag cancels that drag to avoid applying obsolete coordinates.

The UI maps gutter hit tests to layout operations. ncurses parses mouse input;
for terminals advertising an xterm-compatible `kmous`, the application explicitly
enables held-button motion mode 1002 and disables it at shutdown. The installed
ncurses XM capability otherwise enables click mode alone. Keyboard resize mode
selects the same dividers and uses the same constrained coordinate updates.
Physical mouse appearance/usability remains subject to owner acceptance, even
when the PTY and real tmux forwarding tests pass.
