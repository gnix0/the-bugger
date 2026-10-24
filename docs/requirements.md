# Product requirements

This specification describes the v0.1.0 target, not the availability of every
feature today. [Roadmap status](roadmap.md#implementation-status) distinguishes
completed implementation from pending integration and hardware validation.
The [interface specification](requirements.md#interface-constraints) defines presentation and
interaction; [architecture](architecture.md) defines the implemented boundaries.

## Product direction

The Bugger is primarily a terminal-native GDB frontend for embedded development:
microcontrollers and other embedded targets across GDB-supported architectures.
Native Linux debugging is a supported product capability as well as the practical
development/validation focus through M14. This sequencing does not make Linux
the product's primary target or permit weakening existing native functionality.
ARM Cortex-M/OpenOCD is the initial embedded workflow; STM32 is the first physical
acceptance target, followed by the specified ESP32 release matrix. Physical
RISC-V acceptance is deferred for v0.1.0 as specified below.
Shared inspectors remain architecture-neutral; target/server capabilities must
be explicit. No single probe, server or target operating system is universal.

After v0.1.0, active feature development focuses overwhelmingly on embedded
workflows. Native Linux remains supported: preserve regression coverage, fix
defects, maintain compatibility and share UI/infrastructure improvements. Its
early implementation supplies useful functionality and a repeatable foundation
before probes/servers/hardware; it does not imply equal feature effort or parity.
Substantial native-specific additions require a correctness need, important
defect or explicitly approved requirement. An excellent native experience
remains a v1.0.0 intention. M14–M18 and v0.1.0 FR/NFR acceptance retain their
scope except for the explicit physical RISC-V deferral below.
The post-release roadmap places F22 flashing first in v0.4.x independently
of text-channel readiness. Version targets express workflow objectives and
dependencies, not dates.

The interface is a **fast, responsive, mouse-operable, terminal-native debugger** with the inspection density of IAR Embedded Workbench's C-SPY debugger, rather than another text editor or a full IDE. It must run comfortably as one pane/window in **tmux** and provide a consistent workflow for:

- **Native Linux C/C++ processes on x86-64** (GDB controls locally launched programs; attaching to permitted processes is also in scope).
- **Embedded ARM firmware** (GDB connects to an OpenOCD GDB server; initial reference board is the STM32 NUCLEO-F446RE with onboard ST-LINK).
- **ESP32 Xtensa and RISC-V firmware** through appropriate editable GDB/server choices, included in recorded release regression. Do not claim untested boards/backends are supported. QEMU ARM/RISC-V guest/native tests are supplementary test infrastructure, not a simulator product feature.
- General firmware and system-level C/C++ across GDB-supported architectures without replacing the TUI or assuming ARM registers. Validation targets do not limit the product architecture.

This is **a GDB frontend, not a replacement debugger engine**. Use structured **GDB/MI** for debugger communication. OpenOCD is an **optional remote server backend**, not the universal backend for every architecture or a prerequisite for native Linux debugging.

C17, Make, ncursesw, POSIX APIs and C test harnesses form the implementation
foundation. C++ is permitted for debug-target fixtures, not the frontend.
No simulator, browser runtime, new macro language, mandatory IDE/build integration
or public plugin SDK is part of the approved scope.

The intended session workflow is:

1. Run `the-bugger`.
2. Choose a recent profile or the kind of debugging session from a **clickable terminal menu**.
3. Fill in a concise, readable configuration (with advanced details hidden until requested).
4. Confirm what will be launched/connected, then enter the debugging workspace.
5. Debug normally with **function keys, mouse clicks, and visible toolbar actions**.
6. Exit cleanly; return later to a saved session profile.

Do not add Vim modes, Vim-motion-first interaction, editor emulation, floating windows, or a mandatory IDE/project generation step. Always-on lowercase `h`/`j`/`k`/`l` are additional Left/Down/Up/Right aliases outside text fields, not a mode or a prerequisite for navigation. Text fields keep these letters literal; standard arrows, mouse and visible actions remain available.


## Functional requirements — v0.1.0

Use these identifiers in tests and validation records. `MUST` requirements are release blockers.
All v0.1.0 requirements below remain unchanged by the post-release priority shift.
Post-release scope/acceptance lives in [the roadmap](roadmap.md); shared features must preserve
native behavior, and hardware-specific claims require corresponding evidence.

| ID | Priority | Requirement | Objective acceptance evidence |
|----|----------|-------------|-------------------------------|
| FR-01 | MUST | Startup screen: Linux / Embedded OpenOCD / Existing server, mouse & keyboard selectable | Launch `the-bugger` without args and select each flow |
| FR-02 | MUST | Native x86-64: launch debug-built ELF with arguments and working directory | Run provided fixture and step through `main` |
| FR-03 | MUST | Native Linux: attach to a permitted existing process | Attach to owned fixture PID; handle denied attach gracefully |
| FR-04 | MUST | OpenOCD board-script flow, plus advanced adapter + target / custom cfg | Configure `st_nucleo_f4.cfg`, preview, launch server and connect |
| FR-05 | MUST | Existing remote GDB-server connection | Connect to externally started server without owning/killing it |
| FR-06 | MUST | F5/F9/F10/F11/Shift+F11 actions with clickable equivalents and actionable status | Exercise actions locally; confirm keyboard fallback for unsupported key sequences |
| FR-07 | MUST | Source and disassembly simultaneously, synchronized to selected stopped PC | Step through fixture in both views, verify PC and source changes |
| FR-08 | MUST | Registers and change indicators, discovered from actual target | Compare Linux vs Cortex-M register names, values, PC, SP |
| FR-09 | MUST | Persistent Memory pane with inline address/expression and Go, bounded bidirectional browsing, hex/ASCII and explicit errors | Open without extra page; inspect real buffer/RAM/Flash, scroll both ways, reject invalid/partial reads honestly |
| FR-10 | MUST | Locals, watches and call stack with frame selection | Inspect defined variable, step into function and change selected frame |
| FR-11 | MUST | Breakpoint toggle from source gutter and list management | Set, hit, disable, remove breakpoint; breakpoint state stays in sync |
| FR-12 | MUST | Debugger console & error log (safe command input), state indicator | Inspect MI/GDB diagnostics and recover from a nonfatal bad command |
| FR-13 | MUST | Six-pane default, view selection/tabs, mouse resizing, **dynamic tiling split** | Create seventh visible view, resize, close it; no floating windows |
| FR-14 | MUST | Basic saved connection/source profiles and recent sessions; no preset picker | Save/reopen/edit/delete native/OpenOCD settings; open approved default workspace. Rich workspace/watch/breakpoint restoration is F01, not v0.1.0 |
| FR-15 | MUST | Real STM32 NUCLEO-F446RE firmware debug through OpenOCD and appropriate ARM GDB | Real hardware: halt/step, inspect ARM registers, memory, ELF source/disassembly |
| FR-16 | MUST | Process cleanup, handling missing tools/symbols/device/disconnect | Terminal restored and errors visible; no stray owned subprocesses |
| FR-17 | MUST | Legitimate non-Monokai fallback where terminal lacks truecolor; minimum layout survives resize | Exercise default Linux terminal & tmux, resize window while stopped/running |
| FR-18 | SHOULD | Search/filter OpenOCD script choices and simple friendly labels | Find `board/st_nucleo_f4.cfg` without typing full path |
| FR-19 | SHOULD | Basic inferior stdout/stderr monitoring without corrupting TUI | Debug fixture producing console output |
| FR-20 | SHOULD | Basic register formats and memory grouping controls | Change hex/decimal or byte grouping and see real data interpreted |
| FR-21 | MUST | Focus-driven source/instruction stepping, retained toolbar mode, optimized-context correctness | Step both modes and verify GDB PC/frame/source mapping over multiple instructions per line |
| FR-22 | MUST | Optional mixed source/assembly and AT&T x86-64 assembly | Toggle mixed view, verify source-absent fallback and actual x86 AT&T operands |
| FR-23 | MUST | Run to selected source line/address | Stop at selected location or report intervening stop/error; preserve user breakpoints |
| FR-24 | MUST | Conditions, ignore counts and Default / Require hardware code breakpoints | Real GDB conditions/counts, unavailable slots and explicit failure behavior |
| FR-25 | MUST | Hardware-assisted value-change/read/access data watchpoints | Actual supported stops; unsupported/width/resource errors, reject software fallback |
| FR-26 | MUST | Independent Memory navigation, drafts, grouping, byte count and scroll | Inspect two ranges; edit/navigate/switch tabs without changing another instance |
| FR-27 | MUST | Lazy read-only Locals/Watches, bounded arrays and pre-dispatch read authorization; explicit inspection for unknown storage, automatic reads only with complete safe-footprint evidence | C/C++ aggregates/pointers/arrays, optimized/unavailable values, policy denial before MI dispatch and context cleanup |
| FR-28 | MUST | Source-file/text/line and assembly address/symbol navigation, return to stopped context, source-path recovery | Relocated tree, duplicate basenames/spaces; correct source and breakpoint resolution |
| FR-29 | MUST | Basic Save Log action | Export retained readable log, verify content and useful file-write errors |
| FR-30 | MUST | Explicit embedded disconnect target-state choice where supported | Leave halted/resume and failure/capability diagnostics; external server survives |
| FR-31 | MUST | Every-release native x86-64, STM32/ARM, ESP32/Xtensa and ESP32/RISC-V regression, with physical RISC-V acceptance deferred for v0.1.0 | Recorded real-board/core workflow evidence for the release matrix below; QEMU supplementary where available; no unrun PASS |

An existing view choice is not evidence of functionality unless its **data path is live**. Never declare a requirement complete on the basis of visual mock data alone.

## Non-functional requirements — v0.1.0

| ID | Requirement / target | Verification |
|----|----------------------|--------------|
| NFR-01 | **C17**, builds on up-to-date Linux using documented, minimal dependencies | `make`/equivalent clean build; compiler errors absent |
| NFR-02 | **Performance**: fast interactive redraw; aim for under 100 ms UI response for local events on normal laptop under non-blocking backend operation; no busy-spin at idle | Measure/observe with instrumented fixtures; report hardware and method, not invented benchmark numbers |
| NFR-03 | **Robustness**: no terminal corruption, hangs or process leaks across repeated launch/quit and disconnect/error paths | Repeated smoke tests; sanitizer run; validate TTY restoration |
| NFR-04 | **Protocol correctness**: structured MI parser with nested/escaped strings, async events, chunk boundaries, malformed inputs | Deterministic parser corpus + integration fixtures |
| NFR-05 | **Usability**: entirely usable by mouse plus visible actions; key-only basic navigation also works | Manual terminal/tmux acceptance checklist |
| NFR-06 | **Safety**: no implicit flash/erase/reset/write; no shell injection; no killing external servers | Manual tests / code review / test of malicious path/argv strings |
| NFR-07 | **Architecture-neutral core**: discovered registers, explicit target capabilities and session-supplied byte order/ordinary-memory regions; no host/target confusion | Synthetic ARM/big-endian/unknown-region cases plus real target acceptance; native success is not embedded evidence |
| NFR-08 | **Diagnosability**: retain readable errors and diagnostic logs without flooding user interface | Reproduce missing GDB, missing OpenOCD and permission failures |
| NFR-09 | **Maintainability**: clear ownership/module boundaries, small testable APIs, no undocumented global state | PR/diff inspection, targeted unit tests |
| NFR-10 | **Portability within Linux terminal ecosystem**: xterm-compatible terminal and tmux, graceful fallbacks for mouse/Fn/truecolor | Manual matrix including inside tmux with mouse enabled |
| NFR-11 | **Accessibility**: color must not be the sole signal of breakpoint/run-state/error/change | Monochrome/minimal color review; labels and symbols remain |
| NFR-12 | **Reproducibility**: exact dependency/build/test instructions; scripted fixture creation; traceable acceptance gates | Clean checkout on same Linux environment; follow README without IDE |

Performance numbers are targets to measure, not grounds for hiding failures. The UI should never freeze while waiting for a remote target.


## Release acceptance

`v0.1.0` may be proposed **only when**:

1. All **FR-MUST** items have explicit `PASS` evidence, either automated or recorded hardware/manual evidence as appropriate.
2. All **NFR** items have been assessed; no known critical bug, crash, corrupted terminal, unmanaged subprocess, unsafe default or unusable mouse/keyboard workflow remains.
3. The same debugger workspace operates successfully for **native Linux x86-64** and **STM32 ARM via OpenOCD**, showing **real** source, disassembly, registers, memory and locals with coherent step/stop behavior.
4. The startup menu and profiles are usable without writing raw OpenOCD/GDB CLI commands for the tested happy paths; all underlying choices remain inspectable/editable.
5. `README.md` covers dependencies, build, invocation, native fixture test, OpenOCD setup for the F446RE, tmux mouse instructions, troubleshooting, limitations and a screenshot/terminal capture where feasible.
6. [validation evidence](validation.md), `CHANGELOG.md`, tests and provenance notes are up to date.
   The v0.1.0 validated matrix is native Linux x86-64, NUCLEO-F446RE/ARM and
   ESP32-S3/Xtensa. Physical RISC-V acceptance is explicitly deferred for this
   release, remains UNVERIFIED, and supplies no hardware compatibility claim.
   Later releases retain the full matrix unless separately reviewed. QEMU adds
   coverage without substituting for boards. Target-specific features list
   their actual supported setups.
7. Final end-to-end validation and release authorization follow the [maintainer release procedure](maintaining.md#release-procedure). The release version is **`v0.1.0`**, without a prerelease suffix.


## Release exclusions

Do **not** allow these features to delay the release:

- Windows/macOS support; core-dump debugging, reverse/time-travel debugging; custom debug probes beyond what GDB/OpenOCD already supports; simulator features/dedicated simulator integration. Xtensa/RISC-V real-board regression remains required.
- CMSIS-SVD peripheral bitfield UI, RTOS awareness, advanced instruction trace, timeline, profiling, cycle-accurate performance analysis, coverage, fault decoding.
- A full IDE, build system integration, editor replacement, LSP, Vim modes, floating/detachable panes, SSH remote session administration, a theme marketplace or a plugin marketplace.
- Silent auto-flashing, automatic reset-to-main and device probing that changes target state.

## Interface constraints

### Visual identity

- The original v0.1.0 visual reference uses **Monokai Pro**. F19 adds selectable
  themes and makes **Catppuccin Macchiato** the development-build default when no
  valid preference exists. Preserve the tiled interface and semantic roles.
- Retained Monokai Pro palette: background `#221f22`, panel `#2d2a2e`, raised surface `#353236`, border `#49454b`, foreground `#fcfcfa`, muted `#939293`, pink `#ff6188`, green `#a9dc76`, orange `#fc9867`, yellow `#ffd866`, purple `#ab9df2`, cyan `#78dce8`.
- Use colors semantically and consistently: current execution line, breakpoints, selected row, changed registers/variables, running/halted/error statuses, assembly mnemonics, addresses, and C/C++ source syntax.
- Provide a **256-color fallback** if truecolor is unavailable, with legible monochrome/minimal-color behavior rather than invisible text. No need to vendor proprietary Monokai assets.
- Compact borders, understated tabs, a persistent status bar, and visible toolbar/action hints. Keep it fast and restrained rather than decorative.
- F19 provides seven built-in themes and 37 variants through centered
  **Config → Themes** controls: a Theme row, a dependent Variant row, Back,
  Preview and Save. Preview uses simulated data and never saves or reads a
  target; Back cancels and restores the saved palette. Save persists the choice
  and returns to Config. Profiles remains available through Config.
- Appearance preferences are global, separate from connection profiles. Preserve
  corrupt/unsupported files and detect stale writers. Colors must retain legible
  fallback and non-color indicators. User-defined themes remain F44; this does
  not change the historical v0.1.0 visual reference or add layout presets.

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
- The retired prototype used simulated example addresses, register contents, log entries and step behavior. The shipped debugger must clearly distinguish live data from demo/mock data.
- A post-v0.1.0 workspace organization **decision gate** is planned for v0.2.1, before the v0.3.x view expansion. Evaluate pages and other navigation then; pages are an idea, not an approved layout or permission to redesign the current workspace now.

- Source supports a temporary fullscreen rendering toggle while retaining the
  application toolbar/session header and status/help footer. Restore must retain
  the tiled split tree, pane proportions and tabs across terminal resizing.
  Hidden panes cannot receive focus or divider hit tests. Existing visibility,
  stale-state and read-authorization rules still apply. This is not a layout preset.

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

Locals/Watches, memory and CPU-register editing are targeted for **v0.2.0**. Dedicated Stack is **v0.1.5**. The complete versioned deferred inventory in docs/roadmap.md is retained through v1.0.0; deferral never means forgetting a feature. No simulator feature or dedicated simulator integration is planned.

### Interaction conventions

- Mouse-first **and** keyboard-accessible. The visible controls are authoritative; key bindings are accelerators, not the only mechanism.
- Overflow scrollbars appear only when needed. Vertical bars are one terminal cell wide, with a vertical `|` track and `#` thumb, retaining the horizontal bar's colors/weight rather than its sideways glyphs. Live Source/Disassembly, Console and context views support independent horizontal scrolling with thumb dragging, track paging and Left/Right keys, preserving pane sizes and execution context. Source line numbers stay fixed.
- Disassembly retains surrounding instructions across steps. Initially show about four valid preceding instructions where GDB metadata permits; keep the viewport still through its middle, scrolling only to preserve roughly five visible rows above and below the PC (smaller margins in short panes, clamped to available context). Manual browsing stays put until stepping or Return to Stopped. Bounded earlier/later windows remain browsable without moving PC; never guess x86 instruction boundaries or treat this as execution history.
- `q` closes/backs out of the current screen outside text fields; in the workspace it leaves the session for the launcher after confirmation. At the launcher it does not exit. `Ctrl+Q` quits from any screen, retaining active-session confirmation. Text fields accept a literal `q`; Escape backs out while editing. Footers distinguish these actions.
- Standard desired shortcuts: `F5` continue, `F9` breakpoint, `F10` next/step over, `F11` step into, `Shift+F11` step out **where the terminal reports it**. Provide clickable alternatives and allow remapping if tmux/terminal intercepts or collapses special keys.
- Startup-menu selection, text fields, dialogs, confirmation prompts and view changes must all be operable by clicks plus straightforward arrow/Tab/Enter/Escape navigation.
- Workspace arrow navigation follows visible control geometry: Left/Right stay
  in the visual row, Up/Down prefer aligned controls, and directions do not wrap.
  Tab/Shift+Tab traverse controls only. Enter activates focused tabs/buttons.
  Content/scrollbar clicks explicitly acquire pane navigation; Escape/Tab or a
  control/outside-pane click returns to controls. Content focus must be visible
  in color and monochrome, and hidden fullscreen controls are excluded.
  While content is focused, Ctrl+arrows or Ctrl+h/j/k/l transfer that focus to a
  spatially neighboring visible pane, preserving viewports and stopping at edges.
- Lowercase `h`/`j`/`k`/`l` act as Left/Down/Up/Right by default on navigation controls, including tabs, scrolling and divider resizing. Editable fields retain ordinary character entry; no enable switch, modal editing, counts or other Vim commands are introduced.
- Terminal/multiplexer constraints are real: test mouse click, scroll, drag/resize and function keys in tmux with mouse enabled; avoid assuming a browser-level right click or perfect function-key support in every terminal. Use explicit `...` menus if context clicks aren't reliably conveyed.
- Do not overwrite user bindings for tmux. If tmux requires `set -g mouse on`, document it as an opt-in prerequisite.
- Source focus selects source F10/F11; Disassembly focus selects instruction F10/F11. Clicking toolbar/inspection panes preserves the last code-pane mode, visibly indicated. Both execution highlights use GDB PC/frame/source mappings; one source line can remain highlighted over several instructions. Browsing never moves PC. Optimized/inlined/tail-call behavior and unavailable values follow GDB; do not reconstruct missing semantics or fabricate precision.
- Distinguish actual stopped PC from an inspected caller frame; refresh context coherently. Run to Selected is not Set Next Statement/change PC. Force Return and timed Autostep are deferred, distinct from ordinary Step Out.

## Session configuration requirements

### Launcher

The launcher uses a larger card
centered horizontally and vertically, theme-aware title/version and subtle rules,
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

### Native Linux configuration

Ask for:

- Mode: **Launch** or **Attach**.
- Launch: executable/ELF path, optional arguments, optional working directory, optional environment variables, optional GDB path (default `gdb`).
- Attach: PID/process selection and optional symbols path. Show actionable Linux permission/`ptrace` diagnostics when the user is not permitted to attach.
- Validate the executable or PID and tools *before* claiming connection; retain input on failure for correction.
- Source search directories and ordered build-prefix → local-prefix mappings are available as source settings, with a recovery action for missing source. Save these in basic profiles; test correct file and breakpoint resolution after relocation.
- Keep inferior stdout/stderr observable at least in a basic form; do not let target output destroy the debugger TUI. Full interactive TTY integration may follow later.

### OpenOCD configuration

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

### Existing server configuration

Inputs: host, port, GDB executable, optional symbol-bearing ELF and remote protocol choice when needed. Support a locally running GDB server/`gdbserver`/OpenOCD remote endpoint without taking ownership of server lifecycle. Default to safe local host values; don't launch OpenOCD in this mode.

### Profile persistence

- Profiles are explicit named data (Linux native / OpenOCD / existing server); support create, edit, select, delete with confirmation, and **recent sessions**.
- Persist user files according to XDG (`$XDG_CONFIG_HOME/the-bugger`, falling back to `~/.config/the-bugger`) and document the format. Use a format and parser appropriate for C with limited dependencies.
- v0.1.0 stores tool names/paths, structured argument arrays, ordered scripts, endpoint, source lookup/mapping settings and remapped actions, with paths including spaces. Use json-c, a versioned JSON format and atomic writes.
- Rich workspace/tab/Memory/watch/code-breakpoint restoration is **F01, v0.1.4**, ahead of Stack/editing. Do not persist runtime values, hit counts, temporary run-to breakpoints or old hardware allocations as live state. Shareable-profile import/export is **v0.3.x**, separating shared connection definitions and personal workspace/history, with relative paths and prelaunch review; no mandatory project format.
- Never save passwords/tokens or guess hidden credentials. Avoid executing untrusted session fields as shell snippets. CLI overrides can come later; usability from bare `the-bugger` is the priority.
