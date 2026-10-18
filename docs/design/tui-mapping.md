# Interface specification and terminal mapping

The [HTML prototype](prototype.html) is the visual and interaction reference;
it contains simulated data, not proof of backend functionality. This document
preserves the product's interface constraints and target session workflows.
Remote/OpenOCD execution remains planned (M15/M16); future views remain subject
to their roadmap gates. For actual controls today, see [usage](../usage.md).

## Interface constraints

### Visual identity

- **Monokai Pro standard** as the visual starting point; prioritize contrast and readability.
- Suggested palette: background `#221f22`, panel `#2d2a2e`, raised surface `#353236`, border `#49454b`, foreground `#fcfcfa`, muted `#939293`, pink `#ff6188`, green `#a9dc76`, orange `#fc9867`, yellow `#ffd866`, purple `#ab9df2`, cyan `#78dce8`.
- Use colors semantically and consistently: current execution line, breakpoints, selected row, changed registers/variables, running/halted/error statuses, assembly mnemonics, addresses, and C/C++ source syntax.
- Provide a **256-color fallback** if truecolor is unavailable, with legible monochrome/minimal-color behavior rather than invisible text. No need to vendor proprietary Monokai assets.
- Compact borders, understated tabs, a persistent status bar, and visible toolbar/action hints. Keep it fast and restrained rather than decorative.
- Monokai Pro remains the default. Selection among 6–8 tested community-favorite themes is targeted around v0.5.0 or earlier; user-defined themes near v1.0.0. Neither changes the v0.1.0 visual reference.

### Default workspace

The initial **IAR-inspired** workspace is a 3-column by 2-row arrangement:

|                 | Left              | Middle                    | Right                       |
|-----------------|-------------------|---------------------------|-----------------------------|
| **Upper row**   | Source            | Disassembly               | CPU Registers               |
| **Lower row**   | Memory            | Locals                    | Console / Debug log         |

Other characteristics already accepted:

- Toolbar: **Continue F5**, **Pause**, **Step over F10**, **Step into F11**, **Step out Shift+F11**, **Toggle breakpoint F9**, and **Reset** (reset is capability-dependent and requires explicit confirmation where impactful).
- Mouse interaction for buttons, tabs, view selector, row selection, source gutter breakpoint toggling, scrolling, and **resizable split dividers**.
- Each pane has a title/tab bar and a menu (e.g. `...`) to switch views. The user may keep several views as tabs in one pane.
- Activating a tab promotes it to the left. Inactive tabs immediately return to their normal styling; mouse activation must not leave a gray/reverse-video highlight on the old tab. Keyboard focus remains visibly associated with the intended tab after reordering.
- **No layout presets or layout selection UI.** The six-pane arrangement shown below is the single approved default workspace. "IAR-inspired" is an internal design description, **not** a user-visible preset name or menu item. In particular, do not implement "Source focus", "Memory focus", or alternate named layouts.
- **Dynamic split** of any pane horizontally or vertically, so more than six views can be shown; six is the **default**, not a limit. Resizing, tabs, and splitting modify the current workspace; they do **not** select predefined layout modes.
- **Strictly tiled** workspace. No floating windows. Tabs, resize, focus, splitting, and closing views must maintain valid layout geometry.
- When the terminal becomes too small, maintain a usable focused view with accessible tabs or a controlled compact mode. Never write outside the terminal, crash, or silently discard pane state.
- The original prototype is **a visual/interaction specification only**: its example addresses, register contents, log entries and step behavior were simulated. The shipped debugger must clearly distinguish live data from demo/mock data.
- A post-v0.1.0 workspace organization **decision gate** is planned for v0.2.1, before the v0.3.x view expansion. Evaluate pages and other navigation then; pages are an idea, not an approved layout or permission to redesign the current workspace now.

### View inventory

**Functional by v0.1.0** (underlying data must be live, not fabricated):

- **Source**: current file/line, breakpoint gutter, scroll, syntax-aware coloring, GDB-known file selection, current-file text find, line navigation and missing-source recovery via GDB search directories/path substitutions.
- **Disassembly**: addresses, bytes if available, mnemonics/operands, PC highlighting, optional mixed source/assembly and source connection where symbols permit. **AT&T syntax for x86-64**, not Intel. Address/symbol navigation and return to stopped context do not move execution.
- **Registers**: discover register names from active GDB target; per-group display where supported, selectable numeric format, highlights for values changed since last stop; CPU architecture-neutral model.
- **Memory**: persistent pane with an inline address/expression field and Go above hex/ASCII data; no initial address-entry page. Independent instances retain address, grouping and scroll. Wheel/keyboard browsing fetches bounded adjacent blocks within established ordinary readable regions, without speculative prefetch. Preserve a prior location; otherwise keep controls visible with a concise empty state. No mandatory SP initialization, fixed target addresses or broad scans. Explicit Go permits RAM/Flash inspection even without region metadata; unknown/MMIO boundaries require deliberate exact-range navigation. Show target byte order and partial/read errors. No editing/search/export/comparison yet.
- **Locals**: scoped variables for selected frame, values/types and lazy expandable structs/unions/arrays/pointers/C++ fields with bounded array paging; honest optimized-out/unavailable states.
- **Console**: readable debugger/transport/target events/errors, captured native output, clearly separate manual GDB command input through MI and basic Save Log with visible file-write errors. No dedicated program-input/serial/RTT/SWO controls yet.
- **Watches**: add/remove/refresh expressions, the same bounded expansion, formats/types/change indicators as Locals; GDB-resolvable global/static expressions work without a dedicated Statics view. No specialized STL display or value editing yet.
- **Call Stack**: show frames and select a frame, updating source and locals.
- **Breakpoints**: gutter/F9 and list management, conditions, GDB ignore counts, pending/multiple locations and Default / Require hardware. Run to Selected uses temporary backend state without damaging user breakpoints. Hardware-assisted value-change/read/access watchpoints are capability-dependent, with clear slot/width/support errors and no silent software fallback.
- **Threads**: list and switch when GDB reports multiple threads; simple/single-core embedded targets can show one.

**Recognized but not required fully functional in v0.1.0**: peripheral/SFR inspector and custom register groups from CMSIS-SVD; stack memory with symbol annotations; Auto, Live Watch (while running), Statics, Symbols, Terminal I/O beyond basic output, RTOS views, Faults, trace, timeline, code coverage, profiling, advanced performance monitoring, image inspection and complex debug macros. These must not masquerade as implemented features. Either omit them from production selectors or show clearly marked `not implemented` descriptions; no deceptive interactive placeholders.

**Design allowance:** device descriptions such as **CMSIS-SVD** should later enrich peripheral register naming and fields, but CPU register inspection must work without a device file. Register/memory reads of MMIO can have side effects: no aggressive automatic peripheral polling.

Locals/Watches, memory and CPU-register editing are targeted for **v0.2.0**. Dedicated Stack is **v0.1.5**. The complete versioned deferred inventory in [the roadmap](../roadmap.md) is retained through v1.0.0; deferral never means forgetting a feature. No simulator feature or dedicated simulator integration is planned.

### Interaction conventions

- Mouse-first **and** keyboard-accessible. The visible controls are authoritative; key bindings are accelerators, not the only mechanism.
- Overflow scrollbars appear only when needed. Vertical bars are one terminal cell wide, with a vertical `|` track and `#` thumb, retaining the horizontal bar's colors/weight rather than its sideways glyphs. Live Source/Disassembly, Console and context views support independent horizontal scrolling with thumb dragging, track paging and Left/Right keys, preserving pane sizes and execution context. Source line numbers stay fixed.
- Disassembly retains surrounding instructions across steps. Initially show about four valid preceding instructions where GDB metadata permits; keep the viewport still through its middle, scrolling only to preserve roughly five visible rows above and below the PC (smaller margins in short panes, clamped to available context). Manual browsing stays put until stepping or Return to Stopped. Bounded earlier/later windows remain browsable without moving PC; never guess x86 instruction boundaries or treat this as execution history.
- `q` closes/backs out of the current screen outside text fields; in the workspace it leaves the session for the launcher after confirmation. At the launcher it does not exit. `Ctrl+Q` quits from any screen, retaining active-session confirmation. Text fields accept a literal `q`; Escape backs out while editing. Footers distinguish these actions.
- Standard desired shortcuts: `F5` continue, `F9` breakpoint, `F10` next/step over, `F11` step into, `Shift+F11` step out **where the terminal reports it**. Provide clickable alternatives and allow remapping if tmux/terminal intercepts or collapses special keys.
- Startup-menu selection, text fields, dialogs, confirmation prompts and view changes must all be operable by clicks plus straightforward arrow/Tab/Enter/Escape navigation.
- Terminal/multiplexer constraints are real: test mouse click, scroll, drag/resize and function keys in tmux with mouse enabled; avoid assuming a browser-level right click or perfect function-key support in every terminal. Use explicit `...` menus if context clicks aren't reliably conveyed.
- Do not overwrite user bindings for tmux. If tmux requires `set -g mouse on`, document it as an opt-in prerequisite.
- Source focus selects source F10/F11; Disassembly focus selects instruction F10/F11. Clicking toolbar/inspection panes preserves the last code-pane mode, visibly indicated. Both execution highlights use GDB PC/frame/source mappings; one source line can remain highlighted over several instructions. Browsing never moves PC. Optimized/inlined/tail-call behavior and unavailable values follow GDB; do not reconstruct missing semantics or fabricate precision.
- Distinguish actual stopped PC from an inspected caller frame; refresh context coherently. Run to Selected is not Set Next Statement/change PC. Force Return and timed Autostep are deferred, distinct from ordinary Step Out.

## Session configuration specification

### Launcher

The launcher presentation uses: use a larger card
centered horizontally and vertically, Monokai Pro title/version and subtle rules,
three full-row choices with terminal-friendly icons, descriptions and chevrons,
and recent-session information below. The selected row has a clear surface and
non-color indicator. Adapt spacing for small terminals; preserve all mouse and
keyboard actions, honest preview/profile availability and v0.1.0 without suffixes.
This launcher refinement does not change the approved debugging workspace.

The startup screen should read approximately:

```text
 THE BUGGER                                            v0.1.0
 ────────────────────────────────────────────────────────────
 Start a debugging session

   > Linux application             Launch or attach locally
     Embedded target · OpenOCD     Board or adapter + target
     Connect to GDB server         Existing remote endpoint

 Recent sessions: [saved named profiles]

 [Enter / click to select]                     [Configure...]
```

This is a TUI with clickable items, not an actual GUI/dialog wrapper. A compact wizard is preferable to a pile of flags on the command line.

### Native Linux

Ask for:

- Mode: **Launch** or **Attach**.
- Launch: executable/ELF path, optional arguments, optional working directory, optional environment variables, optional GDB path (default `gdb`).
- Attach: PID/process selection and optional symbols path. Show actionable Linux permission/`ptrace` diagnostics when the user is not permitted to attach.
- Validate the executable or PID and tools *before* claiming connection; retain input on failure for correction.
- Source search directories and ordered build-prefix → local-prefix mappings are available as source settings, with a recovery action for missing source. Save these in basic profiles; test correct file and breakpoint resolution after relocation.
- Keep inferior stdout/stderr observable at least in a basic form; do not let target output destroy the debugger TUI. Full interactive TTY integration may follow later.

### OpenOCD targets

Default **simple path**:

- Firmware ELF **with symbols** (may already be flashed to hardware).
- **Board** selection from OpenOCD's actual installed `board/*.cfg` scripts; show a friendly display label plus the exact underlying path. Search/filter list; don't claim a perfect human-readable identity can be derived from every file name.
- GDB executable, default preference `arm-none-eabi-gdb` for ARM, but always editable.
- OpenOCD executable (default `openocd`), editable host/port (default GDB port 3333 when applicable), and **Start OpenOCD for me** toggle.
- A summary/confirmation showing the selected paths and **argument-vector preview**, e.g. `openocd -f board/st_nucleo_f4.cfg`. Show the resolved GDB endpoint and whether the tool will start a process or connect to an existing one.
- Connection should normally be **non-destructive**: attach and halt/read where supported, but **do not automatically flash firmware, erase flash, force reset or resume** without the user's deliberate action/configuration.

**Important:** OpenOCD board scripts can already include adapter and target scripts. **Do not append an additional `interface/*.cfg` or `target/*.cfg` automatically** to a board configuration; doing so may duplicate or conflict with its setup. For the reference NUCLEO-F446RE workflow, prefer `board/st_nucleo_f4.cfg` if present in the installed OpenOCD distribution; it sources `interface/stlink.cfg` and `target/stm32f4x.cfg` in the upstream script referenced below. Always verify the installed version and actual target behavior.

**Advanced configuration** (accessible, but not forced on novice users):

- Choose a manually selected **interface** script plus **target** script when no appropriate board script exists.
- Choose one or more explicit config files in user-specified order, a custom `.cfg` file, OpenOCD search paths (`-s`), an existing OpenOCD process, or a different GDB server host/port.
- Display the paths and order plainly. Do **not** pretend that mechanically parsing OpenOCD's Tcl scripts yields a fully reliable semantic board/adapter/device model. Use script discovery as a catalog, with an optional small curated friendly-label mapping, and execute through OpenOCD itself.
- Friendly names must never hide the true config file or make unsupported hardware compatibility promises. Preserve nonstandard scripts and manual override choices.
- Firmware **flash/load UI is deferred beyond v0.1.0** (v0.4.x, ahead of independent text-channel integrations). It will be a deliberate confirmed action with progress/errors and a defined reset policy. v0.1.0 loads ELF symbols and debugs firmware already programmed through existing tools; symbol loading must not program the target.

Handle failed board discovery, missing `.cfg`, busy ST-LINK, USB permission error, GDB port collision, absent device, GDB incompatible architecture, and server startup timeout with clear recoverable diagnostics.

Embedded disconnect/exit asks whether to leave halted or resume where supported. Disclose backend/script limitations and state-transition failures. Do not silently rewrite external server scripts or kill external servers. Reset/Halt is explicit and confirmed; native Launch can Restart with confirmation, while native Attach has no Reset action.

### Existing GDB server

Inputs: host, port, GDB executable, optional symbol-bearing ELF and remote protocol choice when needed. Support a locally running GDB server/`gdbserver`/OpenOCD remote endpoint without taking ownership of server lifecycle. Default to safe local host values; don't launch OpenOCD in this mode.

### Profile persistence

- Profiles are explicit named data (Linux native / OpenOCD / existing server); support create, edit, select, delete with confirmation, and **recent sessions**.
- Persist user files according to XDG (`$XDG_CONFIG_HOME/the-bugger`, falling back to `~/.config/the-bugger`) and document the format. Use a format and parser appropriate for C with limited dependencies.
- v0.1.0 stores tool names/paths, structured argument arrays, ordered scripts, endpoint, source lookup/mapping settings and remapped actions, with paths including spaces. Use json-c, a versioned JSON format and atomic writes.
- Rich workspace/tab/Memory/watch/code-breakpoint restoration is **F01, v0.1.4**, ahead of Stack/editing. Do not persist runtime values, hit counts, temporary run-to breakpoints or old hardware allocations as live state. Shareable-profile import/export is **v0.3.x**, separating shared connection definitions and personal workspace/history, with relative paths and prelaunch review; no mandatory project format.
- Never save passwords/tokens or guess hidden credentials. Avoid executing untrusted session fields as shell snippets. CLI overrides can come later; usability from bare `the-bugger` is the priority.


## Terminal implementation mapping

| Component | Design consideration | Terminal implementation |
|---|---|---|
| Workspace | One background and prominent ASCII boxes | `#221f22` workspace gutters, `#2d2a2e` panels, raised headers and distinct footers; no ASCII boxes |
| Toolbar | Separate branding, buttons and extra header rows | Identity and action strip share a row at 124+ columns; yellow key hints, raised buttons, right-aligned Session/Quit; wraps on narrow screens |
| Tabs | Leading `*` alone, little visual hierarchy | Active tab has selected background, pink text, bold underline; inactive tabs are muted; focused pane has `>` marker |
| Panels | Blank rows around headers and large border overhead | One-row tab strip, one-row inspector/table heading, body and one-row contextual footer; one-cell gutters |
| Source | Whole-line coloring | Separate gutter, line numbers, keywords, types, functions and literals; current line has full-width selection background |
| Inspection | Whole tables colored uniformly | Purple addresses, muted instruction bytes/types, cyan names, green values, yellow changes; selected rows preserve semantic foreground colors |
| Status | Identity, target and execution shared a single style | Pink identity, cyan session label, green labeled simulated run state, explicit disconnected demo ribbon, muted diagnostics/footer |
| Startup | Full-width, loosely grouped controls | Launcher card centered on both axes, pink title/right-aligned version, thin rules, full-row choices with icons/descriptions/chevrons; compact fallback and unchanged configuration forms |

## Terminal adaptations and limitations

Memory uses an inline Address/expression field and Go above the data, as in
the HTML inspector toolbar. It remains visible while the hex/ASCII body scrolls;
no initial address form page. Each pane retains its draft and field scroll/cursor.
Grouping/count and deliberate Refresh remain accessible in compact panes.
An unknown initial address leaves an empty data area with usable controls.
Native/Linux staging does not alter the embedded-first product or Monokai design.

The launcher hierarchy refines the prototype's startup screen only. Its hierarchy is implemented with the existing Monokai roles,
at most a 100-column card, 23 rows normally or 15 on compact terminals. Small
ASCII icons avoid font/icon dependencies; cells replace pixel spacing/rounding.
The six-pane workspace and its approved visual identity remain unchanged.

- Fixed character cells cannot reproduce CSS pixel padding, font sizes, rounded
  corners, shadows or one-pixel borders. Background surfaces, cell gutters,
  bold text and underline provide the corresponding hierarchy. Font rendering
  remains the user's terminal's responsibility.
- Toolbar text labels replace browser icons/tooltips, so actions remain
  discoverable without hover support or font-dependent symbols. Narrow screens
  wrap the action strip and retain the existing focused-pane fallback.
- Exact 24-bit palette values use ncurses direct-color terminfo. For standalone
  Alacritty advertising `COLORTERM=truecolor` or `24bit`, the application tries
  its matching `alacritty-direct` entry and falls back to the original entry if
  unavailable. It does not substitute unrelated terminal key/mouse definitions.
- Inside tmux, direct color requires a matching direct-color terminal description
  and RGB forwarding configured by the user. The application does not infer
  tmux RGB support from an inherited `COLORTERM` or modify tmux settings.
  Otherwise ncurses uses 256/8 colors; `NO_COLOR` requests monochrome. Finite
  palettes may merge nearby gray shades. Labels, underline, focus and change
  markers remain meaningful without color.
- The browser's overlay menus are represented by full-page session configuration
  and pane-local view selection, preserving the strictly tiled workspace.
- Live Source/Disassembly, Console, Call Stack and Threads can scroll clipped
  rows horizontally without resizing panes. Horizontal overflow adds a one-row
  track above the existing footer; source line numbers and execution gutters
  remain fixed. The track/thumb support click paging, dragging and Left/Right
  keys. View offsets persist independently and clamp when content or geometry
  changes; neither bar appears when its content fits. M02 adds dynamic splits, nested pane closing,
  selected-tab closing and divider dragging through the existing pane menus.
  Idle gutters retain the workspace background; only an actively dragged or
  keyboard-selected divider gains a visible line and selected background.
- Explicit demo/previews remain simulated Cortex-M data; native code/console/
  context views use GDB. Live x86-64 disassembly uses AT&T syntax.
- Pane scrolling moves the viewport rather than a highlighted navigation row.
  Source/disassembly reserve row highlighting for the simulated execution
  position (actual stopped/inspected context in live sessions). Overflowing
  bodies have a one-cell vertical scrollbar with a cyan `#` thumb and muted `|`
  track; horizontal `=`/`-` glyphs remain horizontal only. Click its
  track to page or drag its thumb. Initial demo sessions have no breakpoints.
  Live assembly retains preceding instructions across steps. Initial context can
  include preceding decoded instructions where metadata permits; subsequent stepping preserves the
  viewport until PC enters a five-row top or bottom margin. Smaller panes scale
  the margin; available context limits it at listing edges. The default upper
  row receives 62% of the usable height, with divider resizing unchanged.

## Inspection

The live Registers pane uses actual target names/values, green normal
values, yellow changed values and `*` as a non-color indicator. Existing pane
scrollbars handle overflow; the footer offers Refresh, a cycling numeric format
and All (ungrouped). Stale/unavailable/bounded/error states are explicit. This
adds no register editing, new workspace mode or simulated live target data.

The pane-local view menu toggles membership. Included views alone show a
right-aligned filled circle (ASCII star fallback); unselected entries are blank
there. Defaults start marked. Toggle to add/activate or remove a tab; other panes
are independent and at least one tab survives. This behavior adds neither presets nor saved templates.

Compare at 140x44 or larger first, then at 112x28, 80x24 and below 48x18.
Inspect the actual terminal directly and inside tmux with mouse enabled.
Compare active/inactive tabs, current source/disassembly rows, stepped values,
panel gutters, toolbar alignment, startup fields and monochrome readability.
Run `make test` for C interaction and renderer checks. The renderer assertions
verify palette and cell styling, not subjective visual acceptance.

## Design references and rationale

IAR documentation informs capability breadth, not visual styling.
Device/driver/edition/probe requirements are not universal debugger promises:
C-RUN needs its add-on; device files supply register/peripheral descriptions;
RTOS views use specific plugins; trace/profiling/power depend on hardware/driver.
The full IDE supports window arrangements we deliberately do not adopt.

- [C-SPY execution: stepping, highlighting, terminal I/O](https://docs.iar.com/ewarm/10.1x/en/c-spy-debugging/executing-your-application/introduction-to-application-execution.html), sections Source and disassembly mode debugging, Highlighting, Terminal input and output.
- [Disassembly](https://docs.iar.com/ewarm/10.1x/en/c-spy-debugging/executing-your-application/reference-information-on-application-execution/disassembly-window.html), Toolbar and Context menu: mixed display, Go to and Move to PC.
- [Memory](https://docs.iar.com/ewarm/10.1x/en/c-spy-debugging/memory-and-registers/reference-information-on-memory-and-registers/memory-window.html), Toolbar/Context menu: independent windows, driver-dependent live updates, search/save.
- [Registers](https://docs.iar.com/ewarm/10.1x/en/c-spy-debugging/memory-and-registers/reference-information-on-memory-and-registers/registers-window.html), groups/device files, expandable bits and change indicators.
- [Stack](https://docs.iar.com/ewarm/10.1x/en/c-spy-debugging/memory-and-registers/reference-information-on-memory-and-registers/stack-window.html), bounds from linker configuration and Display area annotations; not generic GDB metadata.
- [Fault viewer](https://docs.iar.com/ewarm/10.1x/en/c-spy-debugging/executing-your-application/reference-information-on-application-execution/fault-exception-viewer-window.html), Cortex-M requirements.
- [Live Watch](https://docs.iar.com/ewarm/9.7x/en/c-spy-debugging/variables-and-expressions/reference-information-on-working-with-variables-and-expressions/live-watch-window.html), device/driver requirements.
- [Quick Watch](https://docs.iar.com/ewarm/10.1x/en/c-spy-debugging/variables-and-expressions/reference-information-on-working-with-variables-and-expressions/quick-watch-window.html) and [Statics](https://docs.iar.com/ewarm/10.1x/en/c-spy-debugging/variables-and-expressions/reference-information-on-working-with-variables-and-expressions/statics-window.html), explicit evaluation versus stopped refresh.
- [Trace](https://docs.iar.com/ewarm/10.1x/en/c-spy-debugging/trace/introduction-to-using-trace.html), ETM/ITM and Requirements; [Profiler](https://docs.iar.com/ewarm/10.1x/en/c-spy-debugging/profiling/introduction-to-the-profiler.html), sources/driver matrix; [Power](https://docs.iar.com/ewarm/10.1x/en/c-spy-debugging/power-debugging/introduction-to-power-debugging.html), measurement requirements.
- [Images](https://docs.iar.com/ewarm/10.1x/en/c-spy-debugging/getting-started-using-c-spy/reference-information-on-starting-c-spy/images-window.html) and [Multicore](https://docs.iar.com/ewarm/10.1x/en/c-spy-debugging/multicore-debugging/introduction-to-multicore-debugging.html), loaded images and SMP/AMP restrictions.
- [Macros](https://docs.iar.com/ewarm/10.1x/en/c-spy-debugging/c-spy-macros/introduction-to-c-spy-macros.html), reserved setup hooks and macro language; reuse GDB instead.
- [IDE introduction](https://docs.iar.com/ewarm/10.1x/en/ide-project-management-and-building/the-development-environment/introduction-to-the-iar-embedded-workbench-ide.html), window persistence, C-STAT/C-RUN; [C-RUN](https://docs.iar.com/ewarm/10.1x/en/c-spy-debugging/c-run-runtime-error-checking.html), add-on product boundary.
- [Debug Log](https://docs.iar.com/ewarm/10.1x/en/c-spy-debugging/executing-your-application/reference-information-on-application-execution/debug-log-window.html), filtering/file recording; [ITM configuration](https://docs.iar.com/ewarm/10.1x/en/c-spy-debugging/trace/reference-information-on-trace/itm-configuration-dialog-box.html), text routing.

Backend references: [MI syntax](https://sourceware.org/gdb/current/onlinedocs/gdb.html/GDB_002fMI-Output-Syntax.html), [breakpoints](https://sourceware.org/gdb/current/onlinedocs/gdb.html/GDB_002fMI-Breakpoint-Commands.html), [variable objects](https://sourceware.org/gdb/current/onlinedocs/gdb.html/GDB_002fMI-Variable-Objects.html), [source paths](https://sourceware.org/gdb/current/onlinedocs/gdb.html/Source-Path.html), [symbol queries](https://sourceware.org/gdb/current/onlinedocs/gdb.html/GDB_002fMI-Symbol-Query.html), [optimized code](https://sourceware.org/gdb/current/onlinedocs/gdb.html/Optimized-Code.html), [OpenOCD GDB/RTOS](https://openocd.org/doc/html/GDB-and-OpenOCD.html) and [target events](https://openocd.org/doc/html/CPU-Configuration.html#Target-Events).

First-hand debugger-community reports also inform the design rationale.
Historical/closed requests and extension limitations are not claims of present
IAR/C-SPY defects. These findings directly informed accepted requirements:

- [Source mapping request](https://github.com/Marus/cortex-debug/issues/611) plus GDB source-path documentation → source-path recovery in v0.1.0.
- [Graphing request](https://github.com/microsoft/vscode-cpptools/issues/8770) and [Live Watch implementation](https://github.com/Marus/cortex-debug/issues/810) → bounded plots with real acquisition limits, originally v0.8.x and now proposed v0.7.x (F32). The latter feature was subsequently implemented in that project, not still missing.
- [Embedded tooling discussion](https://www.reddit.com/r/embedded/comments/r57z2j/what_would_you_change_in_embedded_programming/) → shareable profiles v0.3.x, separating personal state and supporting relocation.
- [Slow symbol queries](https://github.com/Marus/cortex-debug/issues/539), [setup frustration](https://www.reddit.com/r/embedded/comments/1k39h8h/) and [IAR extension context request](https://github.com/IARSystems/iar-vsc-debug/issues/19) → lazy reads, recoverable setup and correct frame/thread context, already required. The IAR issue is extension-specific and closed; full C-SPY task switching is not alleged absent.
