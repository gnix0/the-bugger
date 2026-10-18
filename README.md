# The Bugger

A terminal-native GDB frontend implemented in C, primarily for embedded firmware
and system-level C/C++ development across ARM, Xtensa, RISC-V, x86 and other
GDB-supported targets. Native Linux application debugging is also a supported
use case. GDB provides the debugger
engine through its structured MI interface; OpenOCD is an optional server for
targets that use it.

Release acceptance requires regression testing on native Linux x86-64,
STM32/ARM (initially the NUCLEO-F446RE through OpenOCD), ESP32/Xtensa, and
ESP32/RISC-V. QEMU-based ARM and RISC-V runs provide supplementary coverage
where available. Compatibility claims identify the setups actually tested;
these acceptance targets do not limit the product architecture.

**Current development build:** native Linux launch/attach, Continue/Pause,
program output, GDB console commands, Save Log, Call Stack, Threads, live
Source/Disassembly, source/instruction stepping, code breakpoints and Run to
Selected, read-only CPU Registers, Memory with inline address navigation and
bounded hex/ASCII browsing, scoped Locals/Watches, and hardware-required data
watchpoints are functional where the backend supports them.
Other inspection panes report unavailable; profiles and
remote/OpenOCD workflows are not implemented yet. `--demo` opens the
separate, explicitly simulated visual reference.
`0.1.0` is the target version, not a declaration of release readiness.
Native Linux is the development and acceptance focus through M14. Remote and
OpenOCD workflows follow before physical embedded acceptance; their current
preview screens do not establish target support. See the [roadmap](docs/plan.md)
and [architecture review](docs/architecture.md#m10-embedded-readiness-review).

## Build and run

Build on Linux with a C17 compiler (GCC or Clang), GNU Make, pkg-config and the
ncursesw and libelf development headers/libraries. libelf supplies ELF metadata;
GDB performs instruction decoding. Native sessions require a compatible GDB
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
The launcher centers its session card horizontally and vertically, with readable
descriptions and whole-row click targets. Arrow/Tab/Enter navigation and a compact
presentation keep all three choices accessible on smaller terminals. Monokai Pro
colors, simple terminal icons and text indicators need no additional assets.

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
the session usable. Source/Disassembly and Registers show actual stopped context;
Memory uses the inline inspector described below; Locals/Watches expose scoped
values through deliberate reads and lazy expansion, without substituting samples.

A local fixture is available for a first run:

```sh
make fixtures
./build/the-bugger
```

Choose `./build/debug-fixture` as the executable. Continue prints its PID,
directory, arguments, environment marker and ticks; Pause stops it. Console
commands such as `info program` and `echo console-ok\n` exercise debugger output.
The [native checklist](docs/testing.md#m05-manual-acceptance--real-native-linux)
covers configuration errors, output, logs, cleanup and attach. Terminal-control
test output requires explicit `--control-bytes` under frontend capture; ordinary
fixture runs retain their printed PID in the terminal.

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
workspace implements Source, Disassembly, Console, Call Stack and Threads; other live inspection
views are marked unavailable. With `--demo`, views display the simulated Cortex-M4F reference,
including sample source/assembly, values and execution actions. Embedded/remote
configuration still opens an explicit preview and launches no server.

For a live context example, launch `./build/context-fixture`. It deliberately
stops with two worker threads waiting while main is inside recursive calls.
Open **Call Stack**, then choose **Threads** in another pane's `...` menu.
Click a row to select its frame/thread, or browse rows with Up/Down and press
Enter. Selecting a caller changes inspection context without moving execution;
the **inspected caller** label distinguishes it from the original stop.
`>` marks selection, and `*` marks the stopped thread's innermost frame.

**Refresh** reloads stopped context. **Frames >** and **< Frames** page through
32 frames at a time; resize a narrow pane to expose its paging controls.
Add `--deep` as the fixture's Launch argument to exercise multiple pages.
Continuing the example joins the workers and stops again with main alone.
While running, context is explicitly stale. Missing symbols, optimized-out
arguments and unwind errors remain visible rather than becoming sample data.
Thread display is bounded to the first 128 entries and stack paging to 4096
levels. See the [context checklist](docs/testing.md#m06-manual-acceptance--call-stack-and-threads)
for optimized/stripped examples and manual validation.

For a code-stepping example, run `./build/the-bugger`, choose Linux Launch and
enter `./build/code-fixture` in **Executable / ELF**. Review and Start native.
The Loaded / PID not started status and **Awaiting stop** code panes mean the
program has not run yet. In the bottom-right Console click **Command**, type
`start`, then click **Send** (Enter also submits). Do not run the fixture directly
in the shell or pass `start` as its argument. This deliberately asks GDB to stop at main; ordinary Launch
does not insert an automatic breakpoint. Source and Disassembly then follow
GDB's real frame, source mapping and PC. Click Source for source-level F10/F11,
or Disassembly for instruction-level stepping. The visible mode persists when
using the toolbar or inspecting other panes. **Step out** / Shift+F11 returns
from the execution frame, not from a loop within it. A function that loops forever
may therefore keep running until Pause. One source line can stay highlighted over several
instructions; optimized mappings are reported as GDB supplies them.

Code views use `>` for actual stopped execution and `~` for inspected caller
context. Browsing never changes PC. Source **Actions** offers GDB-known file
filtering, current-file find, line navigation and source lookup settings.
Disassembly offers hexadecimal address/simple-symbol navigation and **Mixed**
source/assembly, with an assembly fallback when source is unavailable. x86-64
assembly uses AT&T syntax. **Stopped** returns to the original stopped thread
and frame. Wheel/scrollbars and arrow/Page keys move viewports; Left/Right pans
long code lines. Disassembly's `<` / `>` footer controls page instruction windows.
Assembly retains surrounding instructions across stops; scrolling at a window
edge can load earlier/next code without moving execution. ELF metadata and GDB
provide trusted decoding anchors and preceding context where available.
Stepping leaves the viewport still until PC reaches a five-row
top or bottom margin (smaller in short panes and limited by available context).
Manual browsing stays put until stepping
or **Stopped**. Reads/history are bounded; missing metadata is never replaced by
guessed x86 instruction boundaries. Browsing skips known data/section gaps and
labels the bytes not decoded. **Actions → Decode bytes** explicitly inspects raw
data; repeated instructions are not hidden merely because they look unusual.
Unknown ranges are labeled for the instructions currently visible. Opening and
closing the view selector preserves the listing and viewport. Addresses, bytes,
mnemonics and operands use aligned columns, with full lines available through
horizontal scrolling.
The default upper row gets 62% of the available pane height, giving Source,
Disassembly and Registers more room. Dragging dividers adjusts this as usual.
Live code, Console, Call Stack and Threads show a horizontal scrollbar only when
rows exceed the pane width: drag its thumb, click the track to page, or focus the
body and use Left/Right. Each view retains its own position; Source keeps its line
numbers fixed. Vertical scrollbars are one terminal cell wide and match the
horizontal bar's colors, with a vertical `|` track and `#` thumb. Each bar
disappears when its content fits, and scrolling preserves pane sizes and PC.

Source lookup settings apply GDB search directories and ordered build-prefix →
local-prefix substitutions, preserving literal spaces and quoted paths. GDB must
resolve source identities; the frontend does not guess duplicate basenames or
unresolved relative paths. Settings are session-only until profiles are available.
Code browsing is bounded to 256 catalog files, 1 MiB / 32,768 source lines per
pane and a 128-instruction cache with bounded, normally 256-byte fetches.
Backward scans from known anchors have a work limit and retain the prior listing
on failure. Mixed groups outside the cached
file show their location with unavailable text rather than loading every file.
See the [code checklist](docs/testing.md#m07-manual-acceptance--live-source-and-disassembly)
for optimized, stripped and relocated-source examples.

## CPU registers

Registers discovers names and numeric IDs from the active GDB target and reads
the selected thread/frame. There is no hardcoded ARM or x86 register list.
Changed values use yellow and `*`; changing display format does not itself
create a change. Selecting another frame/thread resets the comparison baseline.

Click the format control to cycle Hex, Decimal, Octal, Binary, Raw and Natural.
**Refresh** rereads stopped values. Running values remain visibly stale; hidden
or idle panes do not poll. Register editing is not implemented. GDB/MI does not
provide standard structured register groups here, so the pane says **All
(ungrouped)** rather than guessing groups from register names.

Visible rows are fetched in batches of 32, with a 512-ID catalog and bounded
1024-byte value buffers. Unavailable values, bounded output and errors are
explicit. Existing horizontal/vertical scrollbars handle overflow. To try it,
launch `build/code-fixture`, enter `start` in Console, focus Disassembly and step
F11. Compare `rip` with the stopped instruction address, then change formats.
See the [register checklist](docs/testing.md#m09-acceptance--live-cpu-registers)
for frame selection and further checks.

## Memory inspection

The Memory pane has an inline Address/expression field and **Go** above hex/ASCII
data. Enter `&memory_buffer`, a pointer expression or a numeric address, then
press Enter or click Go. Without a prior location it stays empty with its controls
visible; no fixed target address or automatic stack-pointer read is assumed.

Wheel and arrow/Page keys browse cached rows and bounded adjacent blocks within
known ordinary-memory regions. Unknown/device boundaries give a navigation notice;
enter an exact address and use Go to deliberately inspect it. No background scans
or speculative peripheral prefetch run. **Options** controls byte count (1–1024)
and grouping (1/2/4/8 bytes when byte order is known); grouping changes display,
not a target bus-access width. Horizontal scrolling exposes long hex/ASCII rows.

Each Memory pane keeps its own address draft, location, grouping and scroll.
Stops/context changes mark retained data stale; **Refresh** reads deliberately.
Partial bytes appear as `??`; failed reads retain the previous snapshot as stale.
Calls, writes and user casts are rejected by the conservative expression policy.
Memory editing/search/export/comparison remain deferred. See
[manual acceptance](docs/testing.md#m10-inline-memory-acceptance).

## Code breakpoints and Run to Selected

Click the Source gutter or select a source/instruction row and press **F9**
(or toolbar **BP**) to toggle a code breakpoint. The **Breakpoints** view shows
live enabled/pending/multiple-location state. **Manage** adds, removes, enables
or disables entries, edits conditions and GDB ignore counts, and selects
**Default / Require hardware** for newly entered locations. Hardware resources
are target-dependent; errors never silently substitute a software breakpoint.

From Source or Disassembly **Actions**, **Run to Selected** resumes toward the
selected line/address without changing PC directly. It preserves user
breakpoints; an intervening stop cancels the operation and temporary state is
cleaned up. Failed cleanup blocks execution and offers **Manage → Refresh**;
ambiguous ownership is reported rather than guessing which entry to delete.
Data watchpoints use the same list and management controls. **Add hardware data
watchpoint…** accepts a conservative expression and Value change, Read or
Read/write access mode. Creation may read watched storage. Hardware type is
verified before frontend execution; software fallback is removed. Width/slot
insertion errors can still occur on resume and remain visible.

Locals/Watches **Actions** can prefill a root expression; qualified child paths
can be entered explicitly. Memory **Options** offers a watchpoint at the selected
row's first group, with editable address and 1/2/4/8-byte width review. It does
not implicitly watch the entire visible range. Mode, width and available slots
depend on the target and GDB/server. See the
[watchpoint walkthrough](docs/testing.md#m12-hardware-data-watchpoint-acceptance).

For a reproducible example, build with `make fixtures`, launch
`build/break-fixture` through the startup menu, then enter `start` in Console
**Command**. This fixture is silent. Add `m08_tick` in **Breakpoints → Manage**,
set condition `value == 3`, and Continue to stop inside the function. Browse
`m08_long` to inspect a long instruction range or `m08_zeros` for explicit raw
decoding. See the [M08 checklist](docs/testing.md#m08-acceptance--browsing-code-breakpoints-and-run-to-selected).

## Workspace controls

The active tab moves to the left. Mouse activation leaves inactive tabs in their
normal style; keyboard focus follows the active tab. Use clicks or Tab/Shift+Tab,
arrows and Enter. In fields use Left/Right, Home/End, Backspace/Delete and Ctrl+U.
Escape goes back; `q` closes/backs out outside text fields and keeps the launcher
open. **Quit** or `Ctrl+Q` exits the application, confirming active-session closure.
Function keys have visible alternatives. Reset remains unavailable in live
sessions; demo actions are explicitly simulated.

Click `...` to customize that pane's tabs or open pane actions. A filled circle
at the right marks each included view; unselected options have no marker.
Choose an absent view to add/activate it, or choose an included view to remove
its tab, including defaults. Each pane retains at least one tab. Membership is
independent between panes; choosing Threads here does not mark it elsewhere.
Minimal terminals use `*` when a single-cell filled circle is unavailable.
Wheel or a focused body with
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

## Locals and Watches

Locals lists names for the selected thread/frame. Watches accepts names, members,
constant indices and simple offsets, such as `counter`, `sample.count` or
`buffer[3]`. Add expressions with **+ Watch**, select a row and choose **Read**.
Calls and mutations are rejected; accepting an expression does not itself read it.

Expand aggregates, arrays and pointers through their disclosure control or
**Actions**. Actions also offers numeric formats, bounded child pages and watch
removal. C++ access groups may require explicit expansion. Types and unavailable/
optimized-out states follow GDB; specialized STL presentation and editing are
not implemented.

Values become stale when execution/context changes. Read again to inspect the
new stop; changed-value indicators compare matching scope across stops. The
current adapter requires explicit value reads because it cannot prove the
complete memory access of arbitrary expressions safe for automatic inspection.
Explicit pointer expansion remains available, but does not grant background
polling. Take care when deliberately inspecting memory-mapped peripherals.

See the [Locals/Watches walkthrough](docs/testing.md#m11-localswatches-acceptance)
for a reproducible fixture and expected observations.

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
launch/attach, output, console/log errors, frame/thread context, code/AT&T/mixed
display, both stepping modes, optimized/stripped targets, relocated source,
crash recovery and cleanup. Sanitizer
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

Independent Memory views and scoped Locals/Watches complement code, breakpoint
and CPU-register inspection. Hardware data watchpoints add capability-dependent
stops on data access. Basic profiles and key remapping are planned. Firmware
sessions will use an appropriate
GDB/server, optionally OpenOCD, without assuming ARM registers or a universal
probe backend.

The [roadmap](docs/plan.md) defines v0.1.0 scope and the expected path to v1.0.0,
including editing, embedded text channels, additional inspection views and themes.
See the [architecture](docs/architecture.md) and
[visual reference](docs/design/prototype.html) for implementation boundaries and
the workspace design.
