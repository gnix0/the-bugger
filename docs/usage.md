# Using The Bugger

The current development build supports native Linux and existing remote
GDB-server sessions, plus reviewed OpenOCD startup and target actions. Physical
validation covers NUCLEO-F446RE and ESP32-S3 with the
[recorded configurations](validation.md#m17--physical-board-validation).
This guide describes v0.1.3, including independent lower overlays, retained
Disassembly/Memory browsing and explicit, setup-gated MMIO sampling. Themes,
Source fullscreen and spatial keyboard navigation remain available.
See [build and first session](../README.md#build-and-first-session) for prerequisites.

## Native Linux sessions

Choose **Linux application**, configure **Launch** or **Attach**, then choose
**Review session** and **Start native**. Inputs remain available after errors.
The launcher centers its session card horizontally and vertically, with readable
descriptions and whole-row click targets. Arrow/Tab/Enter navigation and a compact
presentation keep all three choices accessible on smaller terminals. Theme colors,
simple terminal icons and text indicators need no additional assets.

Launch accepts an executable ELF, arguments, working directory, optional
`NAME=value` environment entries and GDB executable. Arguments/environment use
whitespace, quotes and backslashes for grouping, without shell expansion. For
example, arguments `"two words" "" '$HOME;$(false)'` preserve a spaced argument,
an empty argument and literal metacharacters. An environment entry such as
`THE_BUGGER_TEST="hello world"` preserves its value. Paths are entered literally.
Environment overrides are session-only.

The session starts **Loaded**. Click **Continue** or press **F5** to run; click
**Pause** to interrupt. State and PID come from GDB. Console has an inline
**Command** field below its output; **Enter** or **Send** executes through MI. Errors leave
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
The [native checklist](testing.md#native-launch-attach-and-cleanup)
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
Click the Command field or reach it with Tab to type a GDB command without
leaving the workspace. Arrows/Home/End/Delete/Backspace and Ctrl+U edit the draft;
h/j/k/l, `q` and digits remain literal during ordinary or Vim Insert editing.
With Vim-like input enabled, press i in the field before typing. Enter or Send
submits one command.
The field clears only when the session accepts it for dispatch; a rejected
submission retains the draft, while a later GDB error appears in the log.
Before an accepted Console command is transmitted, MMIO setup confirmation and
all MMIO range authorizations expire, even for commands that appear harmless.
Range definitions remain available for inspection. Confirm the setup again and
renew range consent before another MMIO read; a retained range alone grants no
access. This protects against aliases/scripts changing the backend invisibly.
Ordinary stepping and navigation preserve setup confirmation; range consent
still expires independently on resume or context changes.
Escape leaves the field without sending or discarding the draft. Each Console
pane retains its own draft across tab changes, dialogs and terminal resizing;
new sessions and reused split panes start empty. Leave the field before using
execution key bindings, or click the toolbar action directly. Save Log remains a separate
file-destination dialog. Command history and multiline command editing are not
added by this change.
The current Console sends commands to GDB; it is not a UART terminal. A separate
interactive serial console is [planned as F13](roadmap.md#f13--uartserial-console-planned).

## Existing remote GDB servers

Choose **Connect to GDB server**, enter a TCP `host:port` endpoint (IPv6 uses
`[address]:port`), optional symbol ELF and a GDB executable supporting the target.
Use **Review session → Connect**. The protocol is `remote` by default; **Profile... → Remote protocol** can select `extended-remote` before connection. Connection can halt a target,
depending on its server. Loading an ELF loads symbols, not firmware.

An externally started server remains externally owned. The workspace shows
**LIVE REMOTE** and the target's opaque identifier rather than a local PID.
Stopped inspectors and controls are shared with native sessions. An extended
connection without an inferior shows **Connected**; deliberately attach or launch
using appropriate GDB Console commands. Continue does not launch a local process.
No server administration, reset or flash action is implied by connection.

Memory starts as the persistent inline inspector. Enter a known RAM/Flash address
or supported expression and **Go** for a bounded read. Remote region safety is
unknown by default, so wheel/key browsing requires affirmative ordinary-region
evidence. Without it, use **Options → Previous range (read) / Next range (read)** or inline Go for each
deliberate range. Unknown endian permits byte display; choose an order before
multi-byte grouping. Unknown address units/width or unsupported widths block
byte reads rather than using host assumptions. Target changes invalidate evidence.

Close confirms disconnection from the existing server, never server termination.
A running target is interrupted first; unconfirmed stopping is a bounded failure.
The server determines whether the target resumes or remains halted. Unconfirmed
disconnect/server loss is reported explicitly; inspect the server/target before
assuming a safe hardware state. Correct refused endpoints or incompatible symbols
in the retained form, and reconnect. Actual hardware compatibility still requires
[physical validation](testing.md#every-release-regression-matrix).

## OpenOCD sessions

Choose **Embedded target – OpenOCD**. In Board mode, use **Browse boards** and
**Filter** to select an installed configuration, or enter a literal path. The
selected board supplies its own dependencies; the frontend does not append an
adapter/target pair. Advanced mode exposes separate **Browse adapter** and
**Browse target** actions. Script discovery is metadata only, not a device-support
promise. Add custom search directories and ordered configuration files through
**Profile...**; an explicit script list overrides board/adapter fields.

Use scripts matching the selected OpenOCD executable. In particular, an ESP-IDF
shell may export `OPENOCD_SCRIPTS` for Espressif's build. When selecting system
`/usr/bin/openocd`, launch with `env -u OPENOCD_SCRIPTS ./build/the-bugger` to use
its own script installation. Alternatively, add `/usr/share/openocd/scripts`
through **Profile... → OpenOCD search directories** when using that system
installation; explicit search paths take precedence over the environment.
Incompatible scripts can prevent target setup.

Enter the symbol ELF and target-compatible GDB (ARM defaults to
`arm-none-eabi-gdb`), OpenOCD executable and endpoint. **Start OpenOCD → Yes**
creates a managed local server. Managed endpoints use `localhost:port` or
`127.0.0.1:port` and resolve to IPv4 loopback. Choose **No – existing server**
for an external instance or another host/IPv6 endpoint. Existing servers are
never terminated by the frontend.

**Review session** shows the exact indexed argument vector, endpoint and ownership.
Use Previous/Next argv when necessary; paths with spaces remain single arguments.
Inspect the selected Tcl files: they and OpenOCD initialization can affect target
state. The frontend adds no automatic flash, reset or resume command. Choose
**Start session** or **Connect** deliberately. Missing tools/files, busy ports,
probe/permission errors, early exits and startup timeouts retain actionable logs
and configuration. A listener alone is not proof of a connected target.

The live workspace identifies **LIVE OPENOCD** and retains shared inspectors.
**Halt** deliberately stops the target. **Reset** requires confirmation and requests
reset-and-halt; it never programs firmware. Unsupported operations and failure to
verify fresh stopped state leave inspection unavailable rather than presenting
stale target values as current. Tested physical configurations and their limits
are recorded in [M17 evidence](validation.md#m17--physical-board-validation).

On close choose **Leave halted** or **Resume**. Only owned GDB/OpenOCD processes
are cleaned up. Target disposition depends on backend scripts and hardware; an
unconfirmed transition is reported. Signal cleanup uses the conservative halted
policy where possible. Normal Memory/variables access policy still applies:
OpenOCD/SVD readability does not authorize speculative peripheral reads.

### Debugging embedded startup code

Startup code can be debugged with the existing source/instruction stepping and
breakpoint controls when the target/backend can halt early enough. Attaching to
running firmware does not rewind initialization. Loading an ELF supplies symbols;
it does not program that firmware onto the board.

1. Use already-programmed firmware, its matching debug ELF and sources, a
   target-compatible GDB and the correct board/probe configuration. Review any
   server scripts that run during connection or reset.
2. In an OpenOCD session, use **Halt** if running, then deliberately confirm
   **Reset** for reset-and-halt. Wait for a fresh **Stopped** state and inspect
   the actual PC in Registers and Disassembly before assuming where execution
   stopped. For other GDB servers, use that server's documented reset/halt
   procedure through deliberate Console commands; monitor commands are not
   portable between servers.
3. On Cortex-M firmware, the reset entry is commonly `Reset_Handler`. If the PC
   is there, open the matching startup assembly through Source **+ File** and
   select Disassembly for instruction stepping. Step Into/Over use instruction
   stepping there; Source stepping needs usable line information. The included
   STM32 fixture's `startup.S` copies `.data`, clears `.bss`, then calls `main`.
4. To finish initialization, deliberately set a breakpoint at the desired later
   entry, for example Console `hbreak main`, then Continue. Hardware breakpoints
   depend on backend support and available slots. Use the actual firmware's
   symbols: ESP SDK application entry may be `app_main`, and earlier ROM or
   bootloader execution may require other symbols or assembly-only inspection.

Stopping before the first startup instruction depends on the core, reset wiring,
probe and server configuration. A later startup breakpoint is not proof that no
earlier instructions ran. If reset-and-halt has already passed the code of
interest, review backend-specific reset catch or connect-under-reset support;
moving PC backwards does not restore the initial hardware/runtime state.
[OpenOCD's reset configuration guide](https://openocd.org/doc/html/Reset-Configuration.html)
explains these target-specific constraints.

Before runtime initialization completes, globals may not have their initialized
values, and locals or stack unwinding may be unavailable. Normal explicit-read
and peripheral-access rules still apply. The
[startup acceptance procedure](testing.md#embedded-startup-debugging-check)
defines how to verify a particular setup; existing M17 results do not establish
first-instruction startup coverage on every board.

## Code and execution context

The single default tiled workspace shows Source, Disassembly and Registers above
Memory, Locals and Console. Each pane has tabs and a view chooser. The live
workspace also includes Registers, Memory, Locals/Watches and Breakpoints;
specialized unimplemented views are labelled unavailable. With `--demo`, views
display the simulated Cortex-M4F reference,
including sample source/assembly, values and execution actions. OpenOCD configuration starts tools only after explicit review. The `--demo`
flow remains simulated.

For a live context example, launch `./build/context-fixture`. It deliberately
stops with two worker threads waiting while main is inside recursive calls.
Open **Call Stack**, then choose **Threads** in another pane's `...` menu.
Arguments initially show names with `<read required>`. Select a frame and choose
**Read Args** to read that frame's simple argument values deliberately. Values
are invalidated by refresh or context/execution changes; they are not polled.
Widen a very narrow pane to expose its footer actions.
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
levels. See the [context checklist](testing.md#call-stack-and-threads)
for optimized/stripped examples and manual validation.

For a code-stepping example, run `./build/the-bugger`, choose Linux Launch and
enter `./build/code-fixture` in **Executable / ELF**. Review and Start native.
The Loaded / PID not started status and **Awaiting stop** code panes mean the
program has not run yet. Restore Console with `^` in the bottom-right dock,
then click **Command**, type
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
Use **+ File** (or **Actions → Choose source file**) to open a GDB-known file in
the active Source pane. The **Current** file tab follows the selected stopped
frame; opened file tabs stay on their chosen file as execution moves. Click a
file tab to switch, **x** beside the active file to close it, or **<**/**>** to
cycle when the pane is narrow. Each Source pane can hold up to seven opened
files plus Current, with independent scroll and horizontal positions. Selecting
an already open file returns to its tab. The tabs are session-local and only
the active file's bounded local text is loaded. Opening a file does not run the
target or change the selected frame.
Disassembly offers hexadecimal address/simple-symbol navigation and **Mixed**
source/assembly, with an assembly fallback when source is unavailable. x86-64
assembly uses AT&T syntax. Source's **Stopped** returns to the original stopped
thread and frame. Disassembly's **Actions → Return to Stop (this view)** instead
moves only that viewport to the stopped PC, leaving selected thread/frame and
other inspectors unchanged. **Follow selected frame (this view)** is separate.
Wheel/scrollbars and arrow/Page keys move viewports; Left/Right pans
long code lines. Disassembly's `<` / `>` footer controls page instruction windows.
Assembly retains surrounding instructions across stops; scrolling at a window
edge can load earlier/next code without moving execution. ELF metadata and GDB
provide decoding anchors and preceding context where their mapping is trusted.
For remote sessions, **Actions → Review ELF/load address mapping** reviews the
configured ELF and an unsigned additive load bias (zero for linked addresses).
The review shows the complete local path and paged allocated-section addresses.
Confirm only when they describe the current target image. **USER-CONFIRMED** is
your declaration, not automatic verification, proof that live bytes match, or
permission to read Memory. Native file/device/inode and load-segment matches are
labeled **BACKEND-VERIFIED**; unavailable or invalidated mappings are
**UNVERIFIED**. The pane footer uses `ELF:native`, `ELF:user` or `ELF:?` for
visible instruction mappings, preserving the selected-frame/file header. Changed ELF identity, target/image lifecycle and accepted Console
commands invalidate trust; cached listings remain historical. A new confirmation
for another bias supersedes the previous declaration for that ELF. Confirmation
preserves existing rows; an explicit code action obtains fresh decoding when
those rows are marked historical. Nonuniform
relocation and general multi-image configuration are not inferred.

Stepping leaves the viewport still until PC reaches a five-row
top or bottom margin (smaller in short panes and limited by available context).
Manual Disassembly browsing stays put across stepping and frame changes, with
historical samples labeled. Its header uses the sampled source attribution when
available, otherwise `address window`; it does not borrow the current selected
frame's filename or caller label. Cached windows can be browsed while running; an
uncached request leaves the viewport unchanged and is never queued for later.
Disassembly and Memory share a lazily populated 4 MiB history cache, including
entry metadata. Eviction does not move a viewport or authorize reads.
Reads/history are bounded; missing metadata is never replaced by
guessed x86 instruction boundaries. Browsing skips known data/section gaps and
labels the bytes not decoded. **Actions → Decode bytes** explicitly inspects raw
data; repeated instructions are not hidden merely because they look unusual.
Unknown ranges are labeled for the instructions currently visible. Opening and
closing the view selector preserves the listing and viewport. Addresses, bytes,
mnemonics and operands use aligned columns, with full lines available through
horizontal scrolling.
Source, Disassembly and Registers occupy the workspace above the lower dock.
Expanded lower groups cover their lower portions without changing that geometry.
Live code, Console, Call Stack and Threads show a horizontal scrollbar only when
rows exceed the pane width: drag its thumb, click the track to page, or focus the
body and use Left/Right. Each view retains its own position; Source keeps its line
numbers fixed. Vertical scrollbars are one terminal cell wide and match the
horizontal bar's colors, with a vertical `|` track and `#` thumb. Each bar
disappears when its content fits, and scrolling preserves pane sizes and PC.

Source lookup settings apply GDB search directories and ordered build-prefix →
local-prefix substitutions, preserving literal spaces and quoted paths. GDB must
resolve source identities; the frontend does not guess duplicate basenames or
unresolved relative paths. Save applied settings through the source/profile
action to reuse them in a named profile; unsaved changes remain session-only.
Code browsing is bounded to 256 catalog files, 1 MiB / 32,768 source lines per
pane and a 128-instruction active window with bounded, normally 256-byte fetches;
older loaded windows share the 4 MiB browsing cache.
Backward reconstruction starts at a reliable earlier boundary within 1024 bytes
and decodes at most 128 instruction rows per action, including rows omitted from
the displayed prefix. Small retained boundary records share the history budget
and can survive eviction of larger listings; historical records never establish
fresh boundaries. Failure or exhaustion keeps the previous rows and provenance;
there is no automatic retry. Closing a split clears its Code history before its
pane slot can be reused.
Mixed groups use bounded local source annotations across mapped files, loading at
most one file per view per pump. Blank lines and braces remain verbatim; missing
or evicted source text shows its location and an unavailable notice.
See the [code checklist](testing.md#source-and-instruction-stepping)
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
See the [register checklist](testing.md#cpu-registers)
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
[manual acceptance](testing.md#memory-inspector).

For the documented, physically validated STM32F446/ST-LINK V2J48M35/OpenOCD
setup, **Options → Temporary ranges** also supports explicit-width MMIO.
Add a range, review its inclusive begin and exclusive end, choose MMIO and its
8/16/32-bit width, and record known side effects (empty means **UNKNOWN**).
When moving a draft to higher addresses, enlarge its end before moving its begin;
each edit must leave valid bounds and revokes consent.
**Setup → review validated setup** displays the exact target, device, firmware,
OpenOCD configuration, endpoint and supported widths. Confirm only if every
detail matches yours. This is your assertion, never automatic verification;
other setups need separate validation. Setup confirmation and range authorization
are separate operations, and neither reads hardware.

**Browse declared MMIO units** opens a navigation-only list. Each row is one
aligned access-width-sized unit, not a verified register identity. Select a row
and choose **Read unit** to sample just that address at its declared width.
**Options → Refresh selected sample** explicitly rereads a previously sampled
unit. Scrolling, numeric address navigation, cached inspection and ordinary
stepping never cause MMIO reads. There is no whole-window MMIO refresh or polling.
Samples are historical observations; peripherals may change even while halted.
One explicit frontend request may cause multiple physical access attempts in
external software/hardware, depending on driver path and failure conditions.
The Bugger does not retry automatically. An uncertain outcome blocks further
MMIO until the exact outstanding request reconciles or you reconnect; a completed
but untrustworthy frame requires reconnection. Diagnostics remain in Console.
Closing invalidates setup and consent immediately; an already outstanding read
may have reached hardware and must finish or follow the bounded close policy.

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
[watchpoint walkthrough](testing.md#hardware-data-watchpoints).

For a reproducible example, build with `make fixtures`, launch
`build/break-fixture` through the startup menu, then enter `start` in Console
**Command**. This fixture is silent. Add `m08_tick` in **Breakpoints → Manage**,
set condition `value == 3`, and Continue to stop inside the function. Browse
`m08_long` to inspect a long instruction range or `m08_zeros` for explicit raw
decoding. See the [M08 checklist](testing.md#disassembly-browsing-and-code-breakpoints).

## Source fullscreen

In a live Source pane, choose **Actions → Fullscreen Source**. Alternatively,
use that pane's **... → Pane actions → Fullscreen Source**; the pane-actions
entry also works in the demo. The option appears only for an active Source view.

Source fills the workspace area while the top toolbar/session header and bottom
status/help bars stay visible. Other panes, inactive tabs, compact pane selectors
and divider controls are hidden. Source scrolling, search/navigation, breakpoint
controls and source stepping retain their normal behavior. Hidden inspectors
follow the existing visibility policy rather than starting background reads.

Choose **Restore** in the Source header, press Escape or `q` at control focus,
or choose **Actions → Restore tiled workspace** to leave fullscreen. Tab/Enter
can activate Restore without a mouse. The prior split tree, proportions and tab
membership are retained, including nested panes and terminal resizing. Pane-switch
shortcuts are inactive while fullscreen so they cannot select a hidden view.
Menus and text-entry pages keep their existing Back/Escape behavior; return to
the workspace before restoring it. `Ctrl+Q` retains normal session-close handling.

This is temporary per-session presentation state, not a saved layout or a terminal
emulator fullscreen setting. Returning to the launcher clears it.

## Workspace controls

The active tab moves to the left. Mouse activation leaves inactive tabs in their
normal style; keyboard focus follows the active tab. Arrows move focus in the
corresponding visual direction; Enter activates the selected control. Left/Right
move within a visual row, and Up/Down prefer aligned controls above/below.
Directional movement stops at an edge; it does not wrap. Continue → Pause uses
Right, not Down. Tab/Shift+Tab cycle controls in a stable sequence, excluding
content bodies, scrollbars, dividers and hidden panes.
Arrows are available by default. **Config → Keybinds → Vim-like input** optionally
adds lowercase `h`/`j`/`k`/`l` aliases for Left/Down/Up/Right on controls, content
and divider resizing. See [Vim-like input](#vim-like-input) for field behavior.
With this option off, fields use ordinary literal typing and editing.
Click a pane's content or scrollbar to give it arrow/hjkl scrolling or row
navigation. A `*` in its header and **CONTENT FOCUS** in the help bar identify
this state, including in monochrome. Escape (or Tab) returns to the last visible
control, falling back to Continue if necessary. Clicking a control or empty space
outside the pane also leaves content focus. Wheel scrolling does not acquire
keyboard focus. In fullscreen, the first Escape leaves content; the next restores
the tiles. Field editing and divider resizing retain their own key behavior.

During **CONTENT FOCUS**, the optional **Ctrl+h/j/k/l** aliases and fixed **Ctrl+Left/Down/Up/Right**
move content focus to the neighboring pane in that direction. Movement uses the
visible pane geometry, including nested splits, preserves viewports and stops
at layout edges. The destination keeps its active view tab; Source/Disassembly
focus also selects the corresponding stepping mode. Fullscreen and compact
rendering exclude hidden panes. Escape still returns to the previous control.
The terminal must forward Ctrl+arrow sequences. Traditional terminals encode
Ctrl+H as Backspace and Ctrl+J as line feed, so those bytes move between panes
only during content focus; text fields keep their existing editing behavior.
Enter's carriage return remains a separate activation key.

Escape goes back; the default `q` alias closes/backs out outside text fields and keeps the launcher
open. **Quit** or `Ctrl+Q` exits the application, confirming active-session closure.
Function keys have visible alternatives. Reset requires reviewed OpenOCD support
and confirmation; demo actions are explicitly simulated.

Click `...` to customize that pane's tabs or open pane actions. A filled circle
at the right marks each included view; unselected options have no marker.
Choose an absent view to add/activate it, or choose an included view to remove
its tab, including defaults. Each pane retains at least one tab. Membership is
independent between panes; choosing Threads here does not mark it elsewhere.
Minimal terminals use `*` when a single-cell filled circle is unavailable.
Wheel or a focused body with
arrows/PageUp/PageDown scrolls; vertical scrollbar tracks page and thumbs drag.
Keys `1`–`9` focus retained pane IDs; `[`/`]` cycles panes. There are no layout
presets, layout selector, floating windows or full Vim editor emulation.

The Memory/Stack, Locals/Watches and Console/Breakpoints groups start minimized.
Click a dock title or `^` to restore a group over the primary views; click `_`
immediately left of `...` to minimize it. Drag its top edge to adjust its height
independently. Multiple groups can stay open. Where they overlap, the most
recently focused group is on top; click an exposed part or its dock title to
bring another forward. Minimize it to access fully covered content. Heights and
visibility are session-only; resizing the terminal preserves preferred heights.

Drag one-cell gutters to resize columns or nested splits. Pane actions
split right/below, close a tab, close a nested pane or enter keyboard resizing.
In resize mode Tab/Shift+Tab selects a divider, arrows move it, and Enter/Escape
finishes. Closing a nested pane expands its sibling. Each pane retains at least
one tab; each of the six root tiles retains at least one pane.

The workspace supports up to 32 panes with a minimum tiled size of 20x7 cells per
pane. Wide terminals show three primary columns; lower groups cover those
columns when restored. Smaller terminals show a primary pane and restored
groups across the workspace, with controls to reach every retained pane.
Below 48x18 a mode-neutral resize warning shows the current dimensions and the
minimum required size, retaining state without pausing or disconnecting the target.
Tabs, splits and proportions survive
shrinking and return when space permits.

All built-in themes use consistent panel, header, tab and footer roles. Direct-color
terminfo enables the adapted RGB palette; otherwise 256/8-color or monochrome is used.
Standalone Alacritty advertising truecolor uses `alacritty-direct` when installed.
Within tmux, matching tmux/screen terminal names use `tmux-direct` when RGB is
advertised through COLORTERM or terminfo capabilities. Missing direct entries or
absent RGB signals retain the original terminal's color mode; ncurses-term supplies
the direct entries on distributions that package them separately.
`NO_COLOR=1 ./build/the-bugger` forces monochrome. Use a TERM entry supported by
your terminal; the application does not alter terminal palettes or tmux settings.

In tmux, opt into mouse handling with `set -g mouse on`. Function keys depend on
the sequences the terminal/multiplexer reports; use clickable alternatives when
intercepted. Exact colors also require tmux to forward RGB to a capable outer
terminal; its `terminal-features` RGB setting or existing `Tc` override handles
that side. The application now honors the conventional COLORTERM RGB signal,
including within tmux, instead of quantizing an advertised RGB palette to 256
colors. It does not modify your tmux settings. Older tmux versions may need
COLORTERM preserved in the session; current versions advertise it themselves.
See [tmux's RGB guidance](https://github.com/tmux/tmux/wiki/FAQ#how-do-i-use-rgb-colour).

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

See the [Locals/Watches walkthrough](testing.md#locals-and-watches)
for a reproducible fixture and expected observations.

## Configuration and themes

Choose **Config** on the launcher, then **Themes** or **Profiles**. Both the
configuration menu and theme selector are centered horizontally and vertically.
The demo launcher also offers Themes; its Profiles option remains unavailable.

With no valid saved preference, the default is **Catppuccin / macchiato**.
Select the Theme row, then use Left/Right, Enter or clicks to cycle themes.
The Variant row lists only variants belonging to the selected theme; changing
families resets that row to the family's initial variant (Macchiato for Catppuccin).

| Theme | Variants |
|---|---|
| Catppuccin | latte, frappe, macchiato, mocha |
| Everforest | dark/light × medium, hard, soft |
| Tokyo Night | night, storm, moon, day |
| Rose Pine | main, moon, dawn |
| Nightfox | nightfox, dayfox, dawnfox, duskfox, nordfox, terafox, carbonfox |
| Gruvbox Material | dark/light × medium, hard, soft; material foreground |
| Monokai Pro | pro, classic, machine, octagon, ristretto, spectrum, light |

**Preview** temporarily applies the draft to a simulated workspace. It does not
start a debugger, read a target or save preferences. **Back** or Escape returns
from preview to Themes and restores the saved palette; the draft remains available
there. Back from Themes discards it and returns to Config. **Save** applies and
persists the draft, then returns to Config. Restarting restores the saved choice.
In `--demo`, an explicit theme Save also persists appearance preferences.

Preferences use version-1 JSON in
`$XDG_CONFIG_HOME/the-bugger/preferences.json`, or
`$HOME/.config/the-bugger/preferences.json` when XDG configuration is absent or
relative. Saves use private atomic replacement and reject stale concurrent
writers. Invalid, unsupported or inaccessible files fall back to Macchiato with
a visible error and block saving; repair the file and restart. They are not
silently overwritten. A stale writer must restart before saving.

The palettes are adapted to semantic roles, with contrast adjustments where
needed; 8-color/monochrome terminals cannot reproduce all palette differences.
No Neovim or Lua installation is needed. Upstream revisions and licenses are in
[THIRD_PARTY_NOTICES](../THIRD_PARTY_NOTICES). Custom palette files remain planned
under F44.

## Personal keybinds

Open **Config → Keybinds**. Centered **Action / Key** tables show all 30
bindings. A blank line separates the instructions from the table headings.
Compact action columns keep the key cells close to their labels;
additional Quit/control-traversal keys have concise row labels.
At 112 columns and wider, two tables sit side by side; narrower
terminals use a single longer table. Short terminals scroll the rows. Use
arrows/default movement aliases, Tab/Shift+Tab, Home/End, Page Up/Down or the
mouse wheel to reach bindings. The minimum remains 48×18.

Click a Key cell, or focus it and press Enter, then **press the actual key**.
The cell value disappears and only its border is highlighted in orange while
capturing. Escape cancels capture; Delete clears the binding to `None`.
Supported bindings are printable ASCII (case-sensitive), function keys F1–F24
(including terminal-reported Shift+F1–F12) and permitted Ctrl letters. Unsupported,
reserved or conflicting keys show an error and keep capture active. Key capture
takes precedence over navigation and quit bindings. Resize retains capture.

Edited cells retain cyan borders, distinct from capture; emphasis also works
without color. **Undo** reverses the last draft change, including a confirmed
restore of defaults (up to 128 changes). **Restore Defaults** asks for confirmation
before replacing the draft. **Back**, Escape or the configured Back binding asks
before discarding unsaved changes; quitting also asks when capture is inactive.
Cancel preserves the draft. **Save** alone persists all bindings and returns to
Config. Undo without history, Save without differences from saved bindings, and
Restore Defaults when already at factory bindings are dimmed, skipped by keyboard
navigation and inert on click. No file is written by capture, Undo, cancellation,
discard or Restore Defaults.

The actions include Continue, Pause/Halt, stepping, breakpoint and Reset;
left/down/up/right movement; spatial content-pane transfers; previous/next pane;
pane selection 1–9; Back/close session; and additional Quit and control-traversal
keys. The factory bindings retain F5/F9/F10/F11/Shift+F11, brackets, digits and q.
Vim-like input optionally supplies h/j/k/l and Ctrl+H/J/K/L. Pause/Halt, Reset and additional shortcuts
start unbound. Toolbar hints follow the saved debugger bindings. Binding Reset
does not bypass confirmation or make it available on unsupported targets.

Physical arrows, Ctrl+arrows, Tab/Shift+Tab, Escape and Ctrl+Q remain available.
Duplicate bindings are rejected; unbind an existing action before reusing its
key. Ctrl+C/I/M/Q/S/Z and Space cannot be assigned. Ctrl+H/J/K/L are assignable
only to spatial content-pane transfers because terminals can encode them as
editing characters. Spatial transfer applies only in CONTENT FOCUS. Navigation
and debugger aliases do not intercept literal typing or editing in fields,
including Memory addresses and Console commands. Terminal/tmux interception can
still prevent delivery; fixed keys and mouse controls provide alternatives.

Bindings are personal application preferences in the same `preferences.json`
as themes. Saving either preserves the other. They apply across sessions and
are never loaded from, exported with or overridden by connection profiles.
Old theme-only preferences load factory bindings. Invalid preferences preserve
the file, use defaults and block saving until corrected and restarted.

## Profiles

Use **Save profile** in configuration and **Profile...** for source directories,
ordered path mappings. Replacing an existing name and deleting
a profile require confirmation. Launcher **Config → Profiles** loads a configuration for
review; selection alone does not launch tools. Successful saved native sessions
appear in recents. Source settings restore before execution; errors remain visible.

Profiles write version-2 JSON at `$XDG_CONFIG_HOME/the-bugger/profiles.json`, or
`$HOME/.config/the-bugger/profiles.json` when XDG configuration is absent/relative.
Storage is bounded to 16 named profiles and 8 recent entries with private atomic
writes. Corrupt files are preserved; repair/reload instead of silently overwriting
them. Concurrent stale writers must reload. Paths and ordered argument/script
arrays are literal, with no shell evaluation.

Profiles exclude PID, environment overrides and runtime values; Attach requires
a current PID. Workspace/tabs, Memory locations, watches and breakpoints are not
restored yet (F01). Remote definitions retain endpoint/tool/protocol choices for reviewed connection.
OpenOCD profiles retain script choices and require explicit startup review.
Profiles exclude keybinds and themes. Version-1 profiles remain readable; obsolete
`keys` fields are ignored and omitted on the next profile-store write, including
recent-session updates. Newly written version-2 stores require an updated app;
older versions that require version 1 cannot read them.
See the [format](architecture.md#profiles-and-source-restoration) and
[walkthrough](testing.md#profiles-and-source-settings).

## Troubleshooting and known limits

| Symptom | Meaning or next step |
|---|---|
| Loaded / PID not started / Awaiting stop | The executable is loaded but has not stopped. Console `start` asks GDB to stop at main; Continue alone runs normally. |
| Attach permission error | Check the selected process and system ptrace policy. The frontend does not change security settings; the provided fixture has an explicit `--allow-attach` mode. |
| No source mapping | GDB may be stopped in a library or code without source information. Use assembly or configure source directories/path substitutions; do not infer a frontend failure from missing debug information. |
| STALE memory or Read required variables | Execution/context changed or safety could not be established for automatic reads. Use deliberate Refresh/Read at a fresh stop. |
| Missing tool, bad ELF or profile write error | Correct the retained configuration/input. Profile corruption is preserved rather than silently overwritten. |
| Step Out keeps running | Step Out returns from a function, not a loop. An infinite-looping function needs Pause. |
| Function key is intercepted | Use the visible action or Config → Keybinds; terminal/tmux key delivery varies. |

Flash programming, value editing, specialized peripheral/
RTOS inspectors, trace and runtime analysis remain planned. Profile definitions
do not make those features operational. Register groups currently have an honest
ungrouped fallback. Log export contains retained history only; native program input
and a full terminal emulator are not implemented. Physical board support must be
established by the [release validation matrix](testing.md#every-release-regression-matrix).

## Vim-like input

In **Config → Keybinds**, click **Vim-like input [ Off ]** to enable it, then
**Save**. This is a personal preference alongside themes and bindings, never part
of a shared connection profile. It starts off for new users. Older preferences
without this option retain custom mappings and appearance, but stock h/j/k/l and
Ctrl+h/j/k/l aliases are disabled until explicitly enabled. Loading never rewrites
the preference file. Older application builds may reject the added preference
field; keep the updated build when using these settings.

The toggle fills only unassigned direction slots. It preserves custom mappings;
if an alias belongs to another action, clear that assignment before enabling.
Disabling removes only the stock aliases in their direction slots. Undo reverses
the toggle; Restore Defaults also turns it off. Changes apply only on Save;
leaving with unsaved changes asks for confirmation.

Every newly focused text field starts in **VIM NORMAL**. Cyan side borders and a
block cursor (where supported) identify Normal; green borders and a thin cursor
identify **VIM INSERT**. The footer always names the mode, including monochrome.

| Normal key | Action |
|---|---|
| `i` / `a` | Insert before / after the cursor |
| `I` / `A` | Insert at the start / end |
| `h` / `l`, Left / Right | Move within the line |
| `0` / `$`, Home / End | First / last character |
| `w` / `b` / `e` | Next word / previous word / word end |
| `x`, Delete | Delete under the cursor |
| `j` / `k`, Down / Up | Move to another visible control |

In Insert, printable characters are literal, including navigation aliases and
`q`. Backspace/Delete, arrows, Home/End and Ctrl+U keep their ordinary editing
behavior. Escape returns to Normal without discarding text; Escape again leaves
the field or screen. Tab/Shift+Tab changes focus and the next field starts Normal.
Enter still submits through the existing field action in either mode; entering
or leaving a mode never sends a command or reads target memory. Ctrl+Q retains
session-close confirmation. Raw keybind capture is independent of field modes.
These are single-line conveniences, not a full Vim editor: counts, operators,
registers, macros and multiline editing are not implemented.
