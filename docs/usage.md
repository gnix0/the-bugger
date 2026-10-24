# Using The Bugger

The current development build supports native Linux and existing remote
GDB-server sessions, plus reviewed OpenOCD startup and target actions. Physical
validation covers NUCLEO-F446RE and ESP32-S3 with the
[recorded configurations](validation.md#m17--physical-board-validation).
v0.1.0 is the first public release; the theme configuration below is a post-release
development feature, not a claim about the v0.1.0 binary.
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
unresolved relative paths. Save applied settings through the source/profile
action to reuse them in a named profile; unsaved changes remain session-only.
Code browsing is bounded to 256 catalog files, 1 MiB / 32,768 source lines per
pane and a 128-instruction cache with bounded, normally 256-byte fetches.
Backward scans from known anchors have a work limit and retain the prior listing
on failure. Mixed groups outside the cached
file show their location with unavailable text rather than loading every file.
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
Lowercase `h`/`j`/`k`/`l` are always-on aliases for
Left/Down/Up/Right outside text fields. They follow the focused control: menu
selection, tab focus, body scrolling, theme/variant cycling and divider
resizing behave just like arrows. No setting or editing mode is required.
In fields, these letters type normally; use actual Left/Right, Home/End,
Backspace/Delete and Ctrl+U for text editing.
Click a pane's content or scrollbar to give it arrow/hjkl scrolling or row
navigation. A `*` in its header and **CONTENT FOCUS** in the help bar identify
this state, including in monochrome. Escape (or Tab) returns to the last visible
control, falling back to Continue if necessary. Clicking a control or empty space
outside the pane also leaves content focus. Wheel scrolling does not acquire
keyboard focus. In fullscreen, the first Escape leaves content; the next restores
the tiles. Field editing and divider resizing retain their own key behavior.

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

All built-in themes use consistent panel, header, tab and footer roles. Direct-color
terminfo enables the adapted RGB palette; otherwise 256/8-color or monochrome is used.
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

## Profiles and action keys

Use **Save profile** in configuration and **Profile...** for source directories,
ordered path mappings and action keys. Replacing an existing name and deleting
a profile require confirmation. Launcher **Config → Profiles** loads a configuration for
review; selection alone does not launch tools. Successful saved native sessions
appear in recents. Source settings restore before execution; errors remain visible.

Profiles use version-1 JSON at `$XDG_CONFIG_HOME/the-bugger/profiles.json`, or
`$HOME/.config/the-bugger/profiles.json` when XDG configuration is absent/relative.
Storage is bounded to 16 named profiles and 8 recent entries with private atomic
writes. Corrupt files are preserved; repair/reload instead of silently overwriting
them. Concurrent stale writers must reload. Paths and ordered argument/script
arrays are literal, with no shell evaluation.

Profiles exclude PID, environment overrides and runtime values; Attach requires
a current PID. Workspace/tabs, Memory locations, watches and breakpoints are not
restored yet (F01). Remote definitions retain endpoint/tool/protocol choices for reviewed connection.
OpenOCD profiles retain script choices and require explicit startup review. Key fields accept function keys, Ctrl letters or `None`;
reserved/conflicting bindings are rejected and toolbar hints follow the profile.
Terminal/tmux delivery still varies; mouse alternatives remain available.
See the [format](architecture.md#profiles-and-source-restoration) and
[walkthrough](testing.md#profiles-and-action-keys).

## Troubleshooting and known limits

| Symptom | Meaning or next step |
|---|---|
| Loaded / PID not started / Awaiting stop | The executable is loaded but has not stopped. Console `start` asks GDB to stop at main; Continue alone runs normally. |
| Attach permission error | Check the selected process and system ptrace policy. The frontend does not change security settings; the provided fixture has an explicit `--allow-attach` mode. |
| No source mapping | GDB may be stopped in a library or code without source information. Use assembly or configure source directories/path substitutions; do not infer a frontend failure from missing debug information. |
| STALE memory or Read required variables | Execution/context changed or safety could not be established for automatic reads. Use deliberate Refresh/Read at a fresh stop. |
| Missing tool, bad ELF or profile write error | Correct the retained configuration/input. Profile corruption is preserved rather than silently overwritten. |
| Step Out keeps running | Step Out returns from a function, not a loop. An infinite-looping function needs Pause. |
| Function key is intercepted | Use the visible action or a profile remapping; terminal/tmux key delivery varies. |

Flash programming, value editing, specialized peripheral/
RTOS inspectors, trace and runtime analysis remain planned. Profile definitions
do not make those features operational. Register groups currently have an honest
ungrouped fallback. Log export contains retained history only; native program input
and a full terminal emulator are not implemented. Physical board support must be
established by the [release validation matrix](testing.md#every-release-regression-matrix).
