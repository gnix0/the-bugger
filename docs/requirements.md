# Product requirements

This specification describes the v0.1.0 target, not the availability of every
feature today. [Roadmap status](roadmap.md#implementation-status) distinguishes
completed implementation from pending integration and hardware validation.
The [interface specification](design/tui-mapping.md) defines presentation and
interaction; [architecture](architecture.md) defines the implemented boundaries.

## Product direction

The Bugger is primarily a terminal-native GDB frontend for embedded development:
microcontrollers and other embedded targets across GDB-supported architectures.
Native Linux debugging is a supported product capability as well as the practical
development/validation focus through M14. This sequencing does not make Linux
the product's primary target or permit weakening existing native functionality.
ARM Cortex-M/OpenOCD is the initial embedded workflow; STM32 is the first physical
acceptance target, followed by the specified ESP32 Xtensa/RISC-V release matrix.
Shared inspectors remain architecture-neutral; target/server capabilities must
be explicit. No single probe, server or target operating system is universal.

After v0.1.0, active feature development focuses overwhelmingly on embedded
workflows. Native Linux remains supported: preserve regression coverage, fix
defects, maintain compatibility and share UI/infrastructure improvements. Its
early implementation supplies useful functionality and a repeatable foundation
before probes/servers/hardware; it does not imply equal feature effort or parity.
Substantial native-specific additions require a correctness need, important
defect or explicitly approved requirement. An excellent native experience
remains a v1.0.0 intention. M14–M18 and all v0.1.0 FR/NFR acceptance stay intact.
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

Do not add Vim modes, Vim-motion-first interaction, editor emulation, floating windows, or a mandatory IDE/project generation step. Standard terminal text editing for input fields is fine; navigation must not require Vim knowledge.


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
| FR-31 | MUST | Every-release native x86-64, STM32/ARM, ESP32/Xtensa and ESP32/RISC-V regression | Recorded real-board/core workflow evidence per release; QEMU ARM/RISC-V supplementary where available; no unrun PASS |

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
6. [historical evidence](history/milestones.md), `CHANGELOG.md`, tests and provenance notes are up to date.
   The every-release architecture matrix includes native x86-64 and real STM32/ARM, ESP32/Xtensa and ESP32/RISC-V; QEMU adds coverage without substituting for boards. Target-specific features list their actual supported setups.
7. Final end-to-end validation and release authorization follow the [maintainer release procedure](maintaining.md#release-procedure). The release version is **`v0.1.0`**, without a prerelease suffix.


## Release exclusions

Do **not** allow these features to delay the release:

- Windows/macOS support; core-dump debugging, reverse/time-travel debugging; custom debug probes beyond what GDB/OpenOCD already supports; simulator features/dedicated simulator integration. Xtensa/RISC-V real-board regression remains required.
- CMSIS-SVD peripheral bitfield UI, RTOS awareness, advanced instruction trace, timeline, profiling, cycle-accurate performance analysis, coverage, fault decoding.
- A full IDE, build system integration, editor replacement, LSP, Vim modes, floating/detachable panes, SSH remote session administration, a theme marketplace or a plugin marketplace.
- Silent auto-flashing, automatic reset-to-main and device probing that changes target state.
