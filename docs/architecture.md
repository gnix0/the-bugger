# Architecture

## Current implementation (M04)

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
The independent MI parser and controller described below are implemented and
can start a debugger process. The TUI does not yet consume them and continues
to display explicitly simulated data. Native target sessions, inspection,
OpenOCD management and persistence are not implemented yet.

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

## M03 owned MI parser

`src/gdb/mi.h` defines an internal C API; `mi.c` uses only the C library.
`tb_mi_parse` parses one record without its line delimiter. The returned record,
token, class, ordered result tree and decoded strings are independently owned;
release them together with `tb_mi_record_free`. Duplicate names and unnamed
list elements are preserved. `tb_mi_find` returns the first matching sibling.
Strings carry an authoritative byte length, including embedded decoded NULs;
tokens retain their exact digits rather than assuming an integer width.

Zero-initialize `TbMiParser`, feed arbitrary byte chunks, finish at EOF and
destroy its buffer. CR, LF and CRLF frame records, including a split CRLF.
Blank lines are ignored; an unterminated EOF tail is reported as truncated.
Callbacks receive either an owned record or a temporary error with a zero-based
record byte offset. The callback must not reenter/destroy the parser. Feed's
success means input was consumed; recoverable parse errors arrive through the
callback. Malformed and oversized records recover at the next line delimiter.
Allocation failure is fatal until destroy/reinitialization, with no retry of
partially consumed input. No callbacks retain pointers into input buffers.

Bounds are 1 MiB per encoded record excluding its delimiter, 16,384 value nodes
including containers, and 64 nested containers. Overlong lines are discarded
without growing the buffer further. Limit diagnostics are explicit; a future
controller must fail the affected operation rather than publish partial data.
These are defensive internal limits, not target architecture restrictions.

The parser handles result, execution/status/notification async, console/target/
log stream and prompt records. Unknown class/field names are retained. MI tuples
contain named results; lists contain either results or values, without mixing
the two. C string escapes are decoded as bytes; invalid/out-of-range escapes
are rejected. Raw stream text retains its spaces and gains the documented
implicit newline. Quoted streams have no added newline.

The grammar follows the official GDB manual's
[MI output syntax](https://sourceware.org/gdb/current/onlinedocs/gdb.html/GDB_002fMI-Output-Syntax.html)
and [stream records](https://sourceware.org/gdb/current/onlinedocs/gdb.html/GDB_002fMI-Stream-Records.html).
It is shared by MI2/MI3. The controller supplies interpreter selection,
request correlation and execution-state foundations; the parser itself has no
command encoder, transport, subprocess lifecycle or debugger-domain state.

`make mi-demo` runs the C-only diagnostic decoder with one-byte reads.
It escapes control/non-ASCII bytes before printing and launches no debugger.
Synthetic fixture provenance and expectations are in `tests/fixtures/mi/README.md`.

## M04 nonblocking controller and owned process

`src/gdb/controller.{h,c}` is an internal, single-threaded C interface independent
of curses. It owns one spawned GDB child, three close-on-exec pipes, the parser
and up to 64 requests. `posix_spawnp` passes a literal executable and fixed argv:
`--quiet --nx --interpreter=mi3`. No shell, target arguments or connection command
is used. The child has a separate process group, reset signal mask and default
signal dispositions; cleanup signals only its owned PID, never the group.
All pipe endpoints stay above stdio, even when the host closed descriptors 0–2.
Parent endpoints are nonblocking; child endpoints remain blocking.

Startup waits for the MI prompt and successful `-gdb-set mi-async on` and
`-gdb-set non-stop off` replies before reporting READY (no target). One MI2 retry
is allowed only after the first child is reaped, both output pipes are drained,
and its bounded startup stderr contains GDB's explicit English unrecognized-MI3
interpreter diagnostic, before any prompt or command. Missing tools, timeouts,
generic failures, localized/unrecognized diagnostics and post-prompt failures
never trigger speculative retries. Setup errors fail visibly. Stderr remains
separate from parsed MI and is passed as length-delimited bytes.

Requests receive monotonically increasing tokens within a session, independent
caller cookies, deadlines from enqueue, and session/stop/context stamps. Commands
are assembled from a restricted operation name and separately quoted C-string
arguments (fixed-width octal for control/non-ASCII bytes); encoded commands are
bounded to 16 KiB. Raw command fragments are not accepted. This is MI encoding,
not shell quoting. Partial writes retain their offsets and ordering.

Replies match only fully written requests with exact tokens. STOP/CONTEXT queries
require a stopped target. A resume/new stop invalidates stop-dependent replies;
explicit context changes invalidate context-dependent replies. Obsolete queued
queries are dropped before writing; already-sent stale results are not published
as successful replies. Unknown/late tokens are diagnostic stale events.
Repeated `*running` does not create extra epochs, and a late `^running` cannot
overwrite a newer `*stopped`. New sessions reset the parser/pipes and advance
session identity. The future thread/frame model will call the context hook.

Each accepted public request receives one reply, stale, timeout or cancellation
completion. Fully sent timeouts do not imply backend cancellation: late replies
are ignored and no command is retried. An incomplete write timeout closes the
session because the wire may contain an unfinished command. Protocol/limit/
allocation errors also fail the controller instead of exposing partial state.
Callbacks borrow events/records until return and must not mutate/reenter the
controller; consumers must copy data they retain.

`tb_gdb_pollfds` and `tb_gdb_timeout` allow a future terminal event loop to poll
all sources together, then call `tb_gdb_pump(..., 0)`. The standalone pump waits
at most 25 ms or the next deadline, handles EINTR by returning to its caller,
and reads/writes at most 16 KiB per pipe per turn. It checks deadlines around
poll and handles closed command input even when no write is queued. Idle waits
use poll. Callback work must also remain bounded; the transport does not render.
SIGPIPE masking/consumption is local to each write, preserving the caller's
handler/mask and previously pending signal.

Close cancels requests and closes command input for cooperative EOF exit.
After 300 ms it sends TERM, then KILL after another 200 ms if needed. Reaping uses
`waitpid(owned_pid, ..., WNOHANG)`, clearing ownership before exit callbacks.
The host must not reap the controller's child; unexpected ECHILD is diagnosed
and clears the PID without signalling it again. A reaped child's pipes get
200 ms to drain before being closed if another process retained an endpoint.
Destroy refuses a live controller: close and pump until DISCONNECTED first.
The host must service the pump during shutdown. M05 must implement native
detach/target policy before using this process-close primitive for live sessions.

The C fake peer and interactive diagnostic harness are in `tests/support/`,
with failure/pressure/PTY coverage in `tests/integration/`. They are explicitly
fake; the application remains DEMO. An optional actual-GDB startup test performs
only negotiation, feature enumeration and MI exit, without any inferior/target.
The protocol state follows the official GDB manual's
[input syntax](https://sourceware.org/gdb/current/onlinedocs/gdb.html/GDB_002fMI-Input-Syntax.html),
[async records](https://sourceware.org/gdb/current/onlinedocs/gdb.html/GDB_002fMI-Async-Records.html)
and [interpreter options](https://sourceware.org/gdb/current/onlinedocs/gdb.html/Mode-Options.html).

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
