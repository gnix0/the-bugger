# Architecture

## Current implementation (M12 accepted)

The product is embedded-first, with shared target-neutral inspectors. Linux is
the frontend host and the currently operational target adapter, not the universal
target OS. Native development/acceptance continues through M14; remote/OpenOCD and
physical embedded validation remain M15–M17. See the review below for actual gaps.

`src/main.c` handles help/version and starts the terminal application.
`src/tui/app.c` owns startup/configuration/review/workspace pages, keyboard
and mouse controls, form editing and terminal lifetime. Rendering and theme
selection are in separate files. `src/tui/demo.c` owns explicitly simulated
sample data, demo actions and tabs. The layout module owns tiled geometry.
These samples are never
debugger-domain state and must be replaced by live data in real sessions.

The build and tests are C17 with strict warnings; Make is the only build driver.
C tests use POSIX pipes, processes and PTYs. Python tests were replaced at the
owner's request. Application libraries are ncursesw and libelf (ELF metadata
only; GDB performs instruction decoding).
The MI parser/controller and native session module now serve real Linux
launch/attach sessions through the TUI. Console/output and Continue/Pause are
live. Source/Disassembly and stepping use the code model; Call Stack and Threads
use the selected-context model. Code breakpoints and Run to Selected use the
breakpoint model, which now also provides M12 hardware data watchpoints.
CPU Registers use the register model. M10 Memory is accepted
at `2f951fb` and uses the persistent inspector described below. Locals/Watches
use the scoped variable model accepted at `7744643`, described below. Other
inspection views have explicit unavailable messages. Explicit --demo
retains simulated presentation separately. Embedded/remote remain previews;
inspection, OpenOCD management and persistence await their own gates.

The UI uses nonblocking ncurses input and polls terminal/backend/PTY/pidfd
descriptors together, redrawing after input or session revisions. Idle waits are
100 ms without a session, bounded to 25 ms with one. Raw input preserves function keys and Ctrl+Q;
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

The owner-refined launcher uses the existing render/theme primitives: a bounded
card centered in the usable terminal area, full-row hit regions including choice
descriptions, simple ASCII icons/chevrons and a compact height below 28 rows.
No image assets, new dependency, animation or background work is needed. It keeps
native-vs-preview availability, missing profile persistence and q/Ctrl+Q policy
explicit; session configuration and the debugging workspace are unchanged.

## M10 embedded-readiness review

Reviewed 2026-10-18 against actual modules, the HTML prototype and official
protocol documentation. The table records the inspected pre-correction baseline
and the assigned resolution; M10 corrections passed validation below and were accepted. These
are scoped findings, not a claim that embedded
connections already work. Existing Linux behavior is retained. The architecture
does not need a general framework rewrite before M10 can finish.

| Finding and evidence | Severity/consequence | Required action and gate |
|---|---|---|
| **Confirmed interaction defect:** `app.c` Memory Address action opens `MEMORY_INPUT`; the prototype Memory toolbar and IAR place navigation inside the inspector. | M10 blocker: breaks persistent navigation and separates controls from the data. | M10: inline field/Go, keyboard/mouse operation and pane-local drafts; bounded edge browsing with known-region guards. |
| **Confirmed adapter coupling:** Memory byte-order discovery currently opens `/proc/PID/exe` in `model/memory.c`; no target-neutral region source exists. | M10 prerequisite: remote identifiers cannot safely be treated as local PIDs; absent metadata must not imply safe peripheral reads. | M10: move native metadata into `session/native.c`; inject byte order/ordinary-readable regions into Memory. M15 supplies remote metadata or explicit unknown state. |
| **Confirmed current limitation:** `TbNative` owns models, launch/attach validation, PTY/pidfd recovery and execution methods; `app.c` routes live actions through it. Remote/OpenOCD launcher entries are previews. | High for M15, not a current native defect. Reusing native kill/proc/start logic for remote sessions would be incorrect. | M15: minimal shared session facade/capabilities and separate native/remote lifecycle; tests with absent/nonlocal target PID and external-server survival. Preserve pidfd safety for native. M16 owns only its spawned OpenOCD. |
| **Confirmed metadata gap:** `code.c` calls `tb_images_native`; `images.h` also exposes architecture-neutral ELF loading/range queries. GDB performs actual decoding. | High remote dependency: code boundaries/relocation must come from target-appropriate image metadata, never local `/proc` for a remote PID. | M15: connect ELF/target image metadata and explicit unknown fallback; test Cortex-M Thumb addresses, executable/data boundaries and remote relocation without changing PC. No duplicate disassembler. |
| **Confirmed capability gap:** native Continue can start an executable; Step uses MI context; Reset is not a live remote operation. Controller requires MI async/all-stop setup. | High before embedded connection: executable start, reset, halt, resume and disconnect differ across backend/target. | M15: explicit session/action capabilities and clear unsupported MI setup errors; M16: confirmed backend Reset/Halt/disconnect choice, no auto load/flash/erase/reset/resume. Test errors and state transitions. |
| **Sound shared foundations:** MI parser/controller are curses-independent and enforce session/stop/context generations; selected-context and registers discover GDB thread/frame/register data. Register groups honestly remain ungrouped. | No demonstrated architectural defect; synthetic ARM cases are not hardware proof. | Keep. M15 validates architecture changes, single-thread bare-metal contexts, register availability/byte order and supported target descriptions. M17 checks actual ARM/Xtensa/RISC-V; specialized peripheral/RTOS metadata stays deferred. |
| **Confirmed inspection limits:** Memory uses 64-bit addresses and 8-bit bytes; safe expressions force C parsing. User calls/casts/writes are rejected. Pointer formatting can otherwise read character pointees while resolving an address. | Last item is an M10 safety risk; non-octet or wider-address targets are future compatibility limits, not failures of the initial targets. | M10: resolve validated addresses as scalar integers to avoid automatic string printing. M11: bounded safe variable expressions. M15: disclose unsupported address units/width, preserve unknown states; do not claim every GDB architecture works. |
| **Confirmed test gaps:** native suites exercise Linux GDB, C peer cases cover some ARM/big-endian shapes; no physical board evidence exists. Source TUI once rejected STALE anywhere, including unrelated Registers. | High if interpreted as embedded support; test assertions must match their own view lifecycle. | M10: Memory unknown-map/MMIO-boundary, delayed/partial replies, inline/scroll/session tests and view-scoped assertions. M14 native acceptance; M15 fake/real remote; M16 offline server faults; M17 physical boards; M18 release matrix. |

Memory's revised contract is an owned per-instance navigation/configuration state
and a separately versioned published byte snapshot. New requests cannot relabel
old bytes at a new address. Errors retain the old snapshot with stale/error state;
missing bytes are `??`, never invented zeros. Reads remain stop/context-scoped,
single-flight and at most 1024 bytes. Idle, tab activation and stops do not probe
unknown memory. Scrolling requests another block only on a deliberate boundary
gesture within established ordinary-memory bounds; there is no speculative
prefetch or unbounded accumulated listing. Explicit Go/Refresh remains available
for exact RAM/Flash/MMIO requests when region metadata is absent. Display grouping
is a presentation choice, not a guarantee about backend MMIO access width.

Native metadata may establish anonymous/ordinary-file readable process mappings;
device/special-file maps are not implicitly safe. Remote memory-map integration
belongs to M15/M16 and must distinguish known ordinary regions from unknown or
side-effect-prone addresses. GDB's remote XML format is not a universal direct MI
query API, and its default assumptions about omitted regions are not frontend
permission to probe them. No remote map is synthesized from STM32 constants.

Primary references consulted, with relevant sections:

- [IAR Memory window](https://docs.iar.com/ewarm/10.1x/en/c-spy-debugging/memory-and-registers/reference-information-on-memory-and-registers/memory-window.html): Toolbar (Go to), Display area, independent instances; functional reference only.
- [GDB/MI data manipulation](https://sourceware.org/gdb/current/onlinedocs/gdb.html/GDB_002fMI-Data-Manipulation.html): `-data-evaluate-expression`, `-data-read-memory-bytes`, register commands. Memory reads return successfully read blocks; partial discovery is heuristic, not an exhaustive readability map.
- [GDB memory-map format](https://sourceware.org/gdb/current/onlinedocs/gdb.html/Memory-Map-Format.html): remote `qXfer:memory-map:read`, RAM/ROM/Flash types and omitted-range assumptions.
- [GDB memory region attributes](https://sourceware.org/gdb/current/onlinedocs/gdb.html/Memory-Region-Attributes.html): access modes and widths; display grouping does not set them.
- [GDB target descriptions](https://sourceware.org/gdb/current/onlinedocs/gdb.html/Target-Description-Format.html): architecture/features/register descriptions; backend and GDB support varies.

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
are assembled from a restricted operation name and plain safe ASCII tokens or
quoted C-string arguments (fixed-width octal for control/non-ASCII bytes); encoded commands are
bounded to 16 KiB. Raw command fragments are not accepted. This is MI encoding,
not shell quoting. Partial writes retain their offsets and ordering.

GDB's CLI-derived -gdb-set/-gdb-show consume a raw setting tail. They use a
restricted setting name and literal arguments without CR/LF instead of quoting
the name or an entire environment assignment. Quoted names can misleadingly
return ^done without applying the setting. M05's real tests exposed this M04
prerequisite defect; startup probes now query actual mi-async/non-stop values,
and native fixtures verify shell-free argv and exact environment assignments.

Replies match only fully written requests with exact tokens. STOP/CONTEXT queries
require a stopped target. A resume/new stop invalidates stop-dependent replies;
explicit context changes invalidate context-dependent replies. Obsolete queued
queries are dropped before writing; already-sent stale results are not published
as successful replies. Unknown/late tokens are diagnostic stale events.
Repeated `*running` does not create extra epochs, and a late `^running` cannot
overwrite a newer `*stopped`. New sessions reset the parser/pipes and advance
session identity. The thread/frame model calls the context hook on selection
and refresh, invalidating older inspection requests.

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
The host must service the pump during shutdown. The native session implements
target release before using this process-close primitive.

The C fake peer and interactive diagnostic harness are in `tests/support/`,
with failure/pressure/PTY coverage in `tests/integration/`. They are explicitly
fake; they remain separate from the native workspace. An optional actual-GDB startup test performs
only negotiation, feature enumeration and MI exit, without any inferior/target.
The protocol state follows the official GDB manual's
[input syntax](https://sourceware.org/gdb/current/onlinedocs/gdb.html/GDB_002fMI-Input-Syntax.html),
[async records](https://sourceware.org/gdb/current/onlinedocs/gdb.html/GDB_002fMI-Async-Records.html)
and [interpreter options](https://sourceware.org/gdb/current/onlinedocs/gdb.html/Mode-Options.html).

## M05 native session and logs

`src/session/native.{h,c}` owns sequential configuration/actions over the
controller. Callbacks copy status and mark completions; requests are issued
after callbacks return, respecting controller ownership/reentrancy rules.
Startup validates a readable ELF/PID and directory, resolves the ELF before
changing GDB's working directory, copies bounded structured arguments/environment,
disables auto-load, pagination, confirmations and the inferior startup shell,
and preserves normal ASLR. It does not edit host ptrace policy or user GDB files.
Environment is session-only. The small quoted-word parser performs no expansions.

Launch loads symbols/executable, args/cwd/env and an owned output PTY, then reports
Loaded. Continue starts the program; subsequent Continue resumes a stopped
program. Pause uses MI interrupt. There is no automatic main breakpoint or
stepping in this gate. Attach uses the selected PID and optional ELF, stops on
success and retains actionable permission failures. Attached processes retain
their existing stdio. Source/assembly/register/memory/variable inspection stays
unavailable in M05; M06 supplies frame/thread inspection and M07 code/stepping.
Demo data is never live data.
Basic console run/r/start/starti operations also establish launch ownership;
failed operations do not retain a pending ownership claim. Multi-inferior
coordination and arbitrary session-topology changes are outside this gate.

The PTY captures launched stdout/stderr together without using the debugger MI
pipe or frontend terminal. Only the owned slave's newline translation is changed;
terminal controls and non-ASCII bytes are escaped before display/export. Program
input and a full terminal emulator are deferred. Each pump drains at most 16 KiB.
Logs retain 512 lines of at most 255 bytes including channel prefixes; long
streams are split, partial lines flush independently per channel after an idle interval, and old lines
evict from a ring. Console panes scroll independently and normally follow the
tail. GDB console input is a deliberate single command carried by
`-interpreter-exec console`; embedded record delimiters are rejected and managed
interpreter/async changes have an explicit diagnostic.

Save Log flushes pending fragments and exports the bounded readable history.
It creates a new file with O_EXCL and mode 0600, preserving existing files and
reporting open/short-write/close failures. It exports retained text, not a profile,
and cannot recover evicted history.

Normal Quit/Session requests use a full-page confirmation. The session interrupts
a running target, kills a still-owned launched inferior through GDB or
detaches/resumes an attached target, then closes/reaps GDB. Signal exits run the
same cleanup outside the handler. A stable Linux pidfd acquired from the actual
launch notification verifies/recover-kills the owned inferior if GDB dies;
numeric inferior PIDs are never used for recovery signals. An explicitly detached
inferior relinquishes ownership. Missing pidfd support, release failures and
unconfirmed exit are diagnosed, not counted as successful cleanup. The debugger
child is reaped by the controller; the kernel/adopting parent reaps an inferior
orphaned by a debugger crash.

`--demo` is an explicit visual-reference path. Bare invocation uses the native
launcher; unimplemented server flows remain labelled previews. Live tabs/layout
reuse presentation state only, not sample debugger values. No new presets,
floating windows, workspace pages, themes or editor modes were added.

## M06 selected context, Call Stack and Threads

`src/model/context.{h,c}` owns a bounded debugger snapshot independently of curses
and target architecture. The native session owns this model and passes controller
events to it first. Context requests use distinct cookies so their completion
cannot complete a native configuration/action request. Callbacks only copy
borrowed MI fields and mark work; the native pump issues new requests after
callbacks return and while session actions are idle.

Stopped snapshots carry session/stop/context generations, the original stopped
thread/PC frame and the independently selected thread/frame. Selecting a caller
does not move execution. GDB stop records can omit the innermost level; a supplied
stop frame address identifies frame zero. A later thread/frame selection never
rewrites that original stop frame. Resume, exit and disconnect mark retained data
stale; thread-created/exited/selected notifications schedule invalidation outside
the callback. Context requests from older generations are discarded. Manual
console commands invalidate context before execution and refresh it afterward.

Only visible context views trigger automatic reads, only while stopped. Refresh
and selections also wait for a stopped target. Sequential `-thread-info`,
`-stack-info-frame`, bounded `-stack-list-frames --no-frame-filters` and
`-stack-list-arguments --no-frame-filters --simple-values` produce an owned
snapshot, published coherently after the sequence. Partial unwinds, missing
source/symbols, optimized-out arguments and command failures remain explicit;
no CLI output is parsed and no target expressions/functions are evaluated.
The frontend retains the frames GDB reports, including inline/tail-call frames,
without inventing missing frames. There are no RTOS inspectors in this gate.

Bounds: first 128 reported threads, 32 frames per stack page plus one lookahead,
and at most the first 4096 frame levels. Strings have fixed render-safe bounds;
arguments use simple values rather than eager aggregate expansion. Requests have
three-second deadlines, no automatic retry loop and no idle/running polling.
Last selection wins when pending thread choices supersede each other; frame
selection is blocked during a thread-selection request to avoid selecting a
frame in the wrong thread. Stack pages preserve GDB's selected frame.

Live rows have mouse selection and arrow-key browsing followed by Enter.
`>` marks the selected context and `*` marks the stopped thread's innermost
frame; caller inspection is labelled. Stack paging, Refresh, scrollbars and
normal tiled resize/compact behavior reuse the existing pane controls. Snapshot
revisions cause redraws through the native event loop. Source/Disassembly now
consume this context; Locals/Watches also consume it through M11. Code visibility requests selected
thread/frame metadata without fetching a whole stack when its view is hidden.
Raw source paths are retained separately from escaped display labels.

The protocol follows GDB's official
[stack commands](https://sourceware.org/gdb/current/onlinedocs/gdb.html/GDB_002fMI-Stack-Manipulation.html)
and [thread commands](https://sourceware.org/gdb/current/onlinedocs/gdb.html/GDB_002fMI-Thread-Commands.html).

## M07 live code and execution navigation

`src/model/code.{h,c}` owns per-pane source buffers, assembly windows, mixed-mode
settings and browsing positions independently of curses. The native session owns
it alongside context, routes distinct request cookies, and pumps both only after
controller callbacks return. Session/stop/context generations reject obsolete
backend replies; per-view versions additionally reject an outstanding assembly
reply after a local mixed/follow change. Resume retains explicitly stale code.
Stopped source/assembly follow GDB's selected frame; original stopped-PC markers
remain distinct from inspected caller markers. No compiler semantics are inferred.

`-file-list-exec-source-files` supplies a bounded file catalog, including before
execution. Exact file/fullname identities preserve spaces and duplicate basenames.
Source reads accept regular files only, bounded to 1 MiB / 32,768 lines per pane;
text find and line navigation operate on this local cache. Rendering escapes
terminal controls/non-ASCII bytes and shares the demo's C/C++ lexical colors.
These are bounded synchronous local reads; unusually slow filesystems remain a
latency limitation. There is no eager scan of the project's files or target.
Unresolved relative source paths require GDB lookup recovery, rather than an
ambiguous read relative to the frontend's working directory.

`-data-disassemble` requests 256-byte address windows, retaining at most 128
instructions with opcodes and optional source groups (`-- 2` / `-- 5`). Forward
navigation uses the last decoded instruction's byte length, with 16 previous
window anchors retained for Back; it never guesses a backward instruction boundary.
Stopped refreshes retain the current window if it contains PC, and can restore
previously decoded ranges after returning from another function. A new window
may use the structured first instruction's function offset for one bounded
pre-context read (at most 1024 preceding bytes plus the normal 256-byte forward
range). The metadata-free compatibility path retains four preceding instructions;
the native indexed path retains up to 64 preceding instructions and the bounded
forward listing. Missing/too-distant metadata falls back to the known PC boundary.
All bytes are fetched fresh after stops; saved anchors are browsing context,
not trace or retained historical target state. Scrolling beyond a window edge
can load a known earlier/next window without resuming execution.
Without an earlier saved anchor, a known nearby function origin can supply a
bounded backward decode ending at the current window, retaining at most 128
preceding instructions. Unavailable or distant origins require explicit address/
symbol navigation; the frontend does not disassemble backwards from arbitrary bytes.
A short backward-prefix read prepends its rows to a bounded owned copy of the
current forward listing instead of replacing that listing. At most 128 combined
instructions remain; pending copies are released on completion, cancellation,
view-navigation changes, reset and destruction. The TUI preserves the old first
row's position, adjusted by the requested upward scroll, when it appears in the
combined result. A full older window uses its end instead. This avoids a one-row
result collapsing the display or jumping it to the far end of the retained data.
Mixed failures fall back independently per pane to assembly. Source text is
interleaved when its exact file matches the cached source; other source groups
keep their file/line labels with an explicit unavailable-text message. Current
x86 targets reported by GDB as i386-family explicitly select AT&T syntax.

Address navigation accepts hexadecimal addresses or simple identifiers/qualified
symbols, excluding calls/operators/command fragments. Symbol addresses use
`-data-evaluate-expression --language c` and a quoted symbol's address, avoiding
C++ overloaded address-of calls. Browsing never issues execution/PC-write commands.
Return to Stopped selects the original stopped thread/frame zero. Source lookup
settings use MI source directories and ordered GDB substitute-path rules, with
separate GDB-CLI and MI encoding; only successful replies update saved-in-session
settings. Clear resets both overrides. Basic profile persistence remains M13.

The native module issues MI next/step/next-instruction/step-instruction/finish
against the selected thread's execution frame zero. Clicking Source/Disassembly
chooses the corresponding F10/F11 mode; toolbar and inspector clicks retain it.
Step Out remains finish. Full-page tiled Actions, file filtering and source
settings screens share normal mouse/Tab/arrows/Enter/Escape editing. Source/ASM
scroll and horizontal pan change only the viewport; `>` denotes actual stopped
execution and `~` an inspected frame. No secondary execution-like cursor is added.
Breakpoints/gutter/F9/Run to Selected are provided by M08 below; later inspection
gates remain intact.

Live pane scrolling uses shared bounded content/viewport measurements. Optional
horizontal tracks reserve a row above the action footer; optional vertical
tracks have one-cell hit regions, vertical `|`/`#` glyphs and the horizontal
bar's colors/weight. Width/height calculations account for both
bars before painting and hit testing. Source keeps its line gutter fixed while
code text pans. Source, Disassembly, Console, Call Stack and Threads retain
horizontal offsets per pane/view, clamp to content bounds, and expose thumb
dragging, track paging and Left/Right navigation. A fitting axis renders no bar.
Later live views must supply their bounded row width to this same viewport path.
Demo vertical bars use the same narrow style; its static reference has no horizontal
data-navigation implementation. This adds no layout modes or debugger actions.
Live assembly initially includes prior decoded context where available, then
keeps its viewport until PC reaches a five-row top or bottom margin (scaled in
short panes and clamped to available context). Manual browsing does not
automatically snap back until stepping/Stopped. The default upper row receives
62% of the usable height; divider dragging continues to set the user's ratio.

Protocol references: GDB's [MI data commands](https://sourceware.org/gdb/current/onlinedocs/gdb.html/GDB_002fMI-Data-Manipulation.html),
[execution commands](https://sourceware.org/gdb/current/onlinedocs/gdb.html/GDB_002fMI-Program-Execution.html),
[file commands](https://sourceware.org/gdb/current/onlinedocs/gdb.html/GDB_002fMI-File-Commands.html)
and [source lookup](https://sourceware.org/gdb/current/onlinedocs/gdb.html/Source-Path.html).

## M08 code boundaries and breakpoints

Pane `...` membership toggles query only that pane's tabs. Included options
alone receive a filled-circle marker, with ASCII fallback. Shared indexed
removal preserves active selection when another tab is removed and protects
the final tab. Adding activates the new view; promotion and per-view positions
remain. No global membership state or persistence is introduced.

`src/model/images.{c,h}` uses libelf/GElf for ELF32/64 in either byte order.
Allocated/executable sections, function/object symbols and ARM mapping symbols
classify ranges and supply trusted anchors. Native `/proc/PID/maps` plus PT_LOAD
offsets establish mapped-image load bias. Library/PID lifecycle events invalidate
metadata. Unsupported/unavailable metadata stays unknown, never fabricated.
Limits are 16 images, 256 allocated sections/image, 8192 retained symbols/image,
65536 scanned symbols/image and 64 MiB/image; limited catalogs are disclosed.

Indexed instruction caches merge sorted/deduplicated windows, retaining at most
128 instructions. Backward extension decodes forward from a known anchor in
256-byte chunks, at most 4096 chunks. It skips unknown/non-code boundaries to
the next known code anchor instead of guessing a variable-length x86 boundary.
Failures retain the prior listing. Raw decoding is explicit; repeated byte or
instruction patterns alone never classify code. Gap rows say how many bytes
were not decoded. Visible instruction metadata determines warnings, independent
of the last request or offscreen rows.

Visibility changes do not invalidate a cached code view; stop/context/settings
changes do. Opening/closing a selector therefore preserves cache and viewport.
One assembly formatter supplies both rendered rows and horizontal extent,
aligning address, byte, mnemonic and operand columns without discarding bytes.

`src/model/breakpoints.{c,h}` owns a bounded 256-row MI snapshot, parent/child and
pending locations, conditions, ignore counts, hits, enabled state and hardware
policy. Structured source/line arguments preserve literal quoted paths.
Notifications/stops mark the table dirty; no idle polling or parsed CLI table.
Gutter/F9 and the tiled Manage screen share this model.

Run to Selected inserts a separately owned, thread-specific temporary breakpoint.
An intervening stop cancels continuation and triggers cleanup. Stop generations,
notifications and a pre-insertion ID baseline protect user breakpoints through
races/timeouts. Ambiguous ownership blocks execution instead of deleting guessed
entries. Cleanup failure offers explicit Manage > Refresh retry without spinning.

Some GDB hardware-insertion aborts emit ^running then ^error with the same token
and no *stopped. The controller schedules one bounded -thread-info query. Only
a structured result confirming every thread stopped establishes stopped state;
the context model then refreshes the actual reported frame. Missing/running
thread states stay unconfirmed, with a useful diagnostic. CLI warnings are logs,
not evidence of execution state. This is native evidence, not remote-board proof.

## M09 read-only CPU registers

`src/model/registers.{c,h}` owns discovered numeric IDs/names and cached values.
`-data-list-register-names` preserves empty-name positions; no ARM/x86 list is
canonical. `-data-list-register-values --thread ... --frame ...` reads only
requested visible rows, in batches of at most 32. Catalog capacity is 512 IDs;
values are bounded to 1024 bytes and flagged when bounded. Missing values and
errors are explicit, not zero-filled registers. Failures require explicit retry.

The TUI declares visible row ranges each render pass. A separate context-demand
flag allows a Registers-only workspace without conflicting with code demand.
No register reads occur while running, hidden or idle. A shared format selector
uses GDB Hex, Decimal, Octal, Binary, Raw and Natural representations. Canonical
hexadecimal samples drive change comparison independently of display format;
thread/frame/architecture changes reset baselines. Context generations and local
format versions reject obsolete replies. Long/unavailable canonical values do
not claim changes that cannot be established from complete samples.

The pane uses existing horizontal/vertical scrolling, Refresh and `*` plus color
for changed values. It labels selected context and stale data. GDB/MI provides
no standard structured group-discovery command here; All (ungrouped) is honest
fallback, without CLI-table parsing or guessed metadata. No writes, peripheral
polling, SVD interpretation or register editing are added.

Reference: GDB [MI data commands, register names and values](https://sourceware.org/gdb/current/onlinedocs/gdb.html/GDB_002fMI-Data-Manipulation.html).

## M10 persistent Memory inspector

Accepted in implementation commit `2f951fb`; the full hash and owner acceptance
record are in [the ledger](progress.md). `src/model/memory.{h,c}` owns up to
32 independent locations/settings and bounded byte/validity snapshots; `app.c`
owns per-pane inline drafts/cursors and viewport scrolling. The toolbar stays
above the body while body scrollbars use the adjusted body geometry. Enter and
Go share navigation; invalid input stays in the pane. Switching tabs retains
drafts and data. New sessions/split-slot reuse clear old instance state.

Memory has no Linux `/proc`/PID dependency. `tb_memory_order` and
`tb_memory_regions` take adapter metadata; region entries designate established
ordinary memory only. `tb_memory_browse` validates the next complete bounded
range, read completeness, overflow and current context before enqueueing. No
region metadata grants nothing implicitly; explicit address/Refresh remains
available. `session/native.c` reads ELF byte order and a bounded process-map
catalog after stops, excluding device/special-file mappings from ordinary-memory
authorization. Missing metadata is an honest unknown state.

Snapshots never exceed 1024 bytes; one backend request is active at a time.
Addresses are at most 64 bits and units are 8-bit bytes for the initial supported
targets. The metadata catalog holds at most 128 regions; omitted or unavailable
regions require explicit Go. Unsupported wider/non-octet targets need M15
capability diagnostics, not fabricated values. GDB memory reads may use heuristic
partial reads; the frontend preserves the exact reported bytes/validity rather
than reconstructing inaccessible memory. Scalar address conversion suppresses
character-pointer string printing. User calls/writes/casts remain rejected.

Focused peer/native/TUI, full regression, real tmux and Clang ASan/UBSan checks
passed; commands and logs are in [the ledger](progress.md#m10-completion-verification).
Owner acceptance is recorded separately from automated checks; no physical
embedded result or release acceptance is implied.

## M11 scoped Locals and Watches

Accepted implementation: `77446434d8e6e1e1a356abaa4d94e028a1098036`.
`src/model/variables.{h,c}` owns separate shared Locals/Watches trees; panes
retain their own selection and scroll, but do not create independent watch lists.
`session/native.c` routes events and drives the model without blocking the TUI.
Local names come from `-stack-list-variables --no-values`; value inspection uses
owned MI variable objects, not parsed CLI output or demo data.

Names and expression syntax are not evidence that a value is safe to read.
Creation, evaluation/formatting and child expansion pass authorization before
dispatch. The optional adapter callback must establish the complete operation's
read footprint without reading target memory to discover it. Automatic reads
require every span to fit established ordinary regions, with unknown or
side-effecting overlays taking precedence. Bounds are eight spans and 1024 total
bytes for this proof. Readability alone, an apparent base address, or absence of
`volatile` is insufficient. No complete proof means unknown, not necessarily unsafe.

The native adapter currently supplies no complete expression-footprint proof.
Values therefore require deliberate Read, expansion, page or format actions.
Adding a watch or opening a tab grants no read. Explicit inspection applies to
pointer children just as to roots; it grants only the current operation/context,
not ongoing polling or nearby reads. Calls, mutations and unsupported expression
forms remain rejected even on explicit Read. Explicit operations can access MMIO;
bounded rows and strings do not guarantee a hardware transaction width or a
complete byte limit for GDB's own value evaluation. This is not a sandbox around
GDB or manual Console commands.

Each tree holds at most 64 roots and 256 nodes, with 32 children per page and
depth limited to 12. Names/types/values have fixed display bounds. Expansion is
lazy, including C++ access-group children; specialized pretty-printer/STL objects
are unsupported. Root Read discards its old child page. Numeric formats preserve
canonical change comparisons across stops; scope changes reset comparisons and
same-stop refresh does not erase the previous-stop baseline. Optimized-out,
unavailable, failed and stale states remain explicit.

One variable request is active at a time. Session/stop/context generations reject
obsolete replies. Resume/context changes retire owned roots, and collapse/paging
prunes children; cleanup uses session scope while value requests use context
scope. No automatic idle/running/hidden value polling is introduced.

Remaining integration work belongs to existing gates: M14 audits other value
producers (including Call Stack's `--simple-values` arguments, which are not
read-free); M15 carries authorization/unknown-region policy into remote adapters
before embedded automatic inspection. Target descriptions must not be treated
as complete MMIO safety evidence. Without sufficient proof, explicit inspection
remains the fallback. M17 validates real hardware; synthetic policy tests do not
establish peripheral safety on a board.

## M12 hardware data watchpoints

Accepted implementation: `f1dbb8a70033821e33e9a5506e23758d0723587d`.
The existing breakpoint model issues `-break-watch` with optional `-r` or `-a`
and C expression parsing. The shared conservative expression grammar rejects
calls/mutations; creation remains deliberate because evaluation may read storage.
It does not relax M11 authorization or add automatic peripheral inspection.

Creation requires a stopped target and a complete fresh breakpoint table. The
model records existing IDs and generation, then reconciles the returned ID/type
against `-break-list`. Only matching hardware change/read/access types complete
the operation. Software fallback, mismatched mode, changed context or uncertain
creation causes owned-entry removal; ambiguous ownership blocks execution rather
than deleting an unrelated entry. Failed cleanup stays blocked until explicit
Manage → Refresh or session closure. Requests correlate tokens, and reconciliation
uses bounded tables rather than idle polling. Native Continue/Step/Console honor
the existing breakpoint busy interlock; closing the session remains available.

Re-enable repeats hardware verification. Managed software downgrades discovered
in later complete tables are removed before subsequent frontend execution.
This is backend reconciliation, not a promise of hardware insertion: slots,
alignment or width can still fail when GDB resumes, and backend errors remain
visible. No fixed slot count or architecture capability is inferred from Linux.
Explicit Console operations are not a sandboxed frontend execution path.

Memory's action reviews the address of the selected cached row's first group
and a 1/2/4/8-byte width. It requires current data and never watches an entire
displayed block implicitly. Address forms generate fixed unsigned C lvalues;
the initial target data-model assumptions require compatible C widths and octet
addressing. M15's existing address-unit/capability audit must retain that limit.
Variable actions prefill root expressions; child display labels are not guessed
into C paths, so qualified child expressions can be entered explicitly.
The form cancels submission if its stopped context changed.

Watchpoints share Breakpoints enable/disable/delete/condition/count controls.
MI stop records provide hit values and scope-expiry messages; deleted scoped
watchpoints disappear on reconciliation. They are excluded from code gutter
matching. Native fixtures and TUI tests are available; recorded execution evidence
is in [the ledger](progress.md#m12-completion-verification). Physical backend
support remains M17, and software watchpoints remain deferred beyond v0.1.0.

Protocol references: GDB [MI breakpoint commands, `-break-watch` and `-break-list`](https://www.sourceware.org/gdb/current/onlinedocs/gdb.html/GDB_002fMI-Breakpoint-Commands.html)
and [Setting Watchpoints, hardware limits and scope](https://www.sourceware.org/gdb/current/onlinedocs/gdb.html/Set-Watchpoints.html).

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
Authorize value-producing variable requests before dispatch; expression filtering
alone does not prevent side-effecting memory reads. Manual GDB
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
