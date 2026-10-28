# Development roadmap

Embedded-first development from v0.1.0 toward v1.0.0. Planned capabilities are not
implemented features or hardware support claims. See [architecture](architecture.md),
[requirements](requirements.md) and [validation procedures](testing.md).

## Current development sequence

v0.1.3 is released. RISC-V hardware validation is accepted; F01 is in progress.

| Task | Scope | Current state |
|---|---|---|
| 1.1 | Correct rendering corruption and failed Disassembly navigation | Implemented, host/stress verified and accepted |
| 1.2 | Initially minimized lower panes with independently resizable overlays | Implemented, host/stress verified and accepted |
| 1.3 | Disassembly/Memory browsing and explicit MMIO access | Implemented, host verified and accepted |
| 1.5 | Prepare v0.1.3 | Published; uploaded artifacts verified |
| 2.1 | Validate ESP RISC-V hardware for v0.1.4 | Accepted; recorded RISC-V RV32 hardware checks use ESP32-C6FH4 |
| F01 | Setup restoration for v0.1.4 | Storage, capture/save controls and breakpoint intent implemented; restoration pending |

Current controls are in [usage](usage.md); the accepted browsing scope is in
[requirements](requirements.md#approved-v013-browsing-work).

Remaining v0.1.4 work includes F01, real-world evaluation, documentation,
formatting and owner-led maintenance. F02–F05 retain separate planning and
acceptance gates and their version targets below. Final stabilization,
owner-exclusive build-system review and testing-strategy work precede v0.2.0
publication.

Physical RISC-V validation was deferred for v0.1.3. Task2.1 now records real
RISC-V RV32 checks using ESP32-C6FH4 as the test device. Acceptance adds no new
physical results or waiver of later release regression.

## Implementation status

M00–M18 form the accepted v0.1.0 baseline. Physical ARM/Cortex-M and Xtensa
validation used NUCLEO-F446RE and ESP32-S3; Task2.1 adds RISC-V RV32 using
ESP32-C6FH4. RISC-V was deferred for v0.1.0 and v0.1.3.

Accepted post-release work includes themes, Source fullscreen, spatial navigation
(`53b1e5a`), multi-file Source tabs, inline Console, personal keybinds, Vim-like
input, tmux RGB, rendering fixes, lower overlays and browsing history. Release
contents are in [CHANGELOG](../CHANGELOG.md); results are in
[validation records](validation.md). F01 is in progress; the other future gates
remain planned.

| Milestone | Deliverable | State | Commit / release reference |
|---|---|---|---|
| M00 | Repository foundation | accepted | `0f5d05b` |
| M01 | Launcher and DEMO workspace | accepted | `60df655` |
| M02 | Tiled layout engine | accepted | `094fa71` |
| Post-M02 | Navigation/highlight/scrollbar correction | accepted | `a832eef` |
| P00 | Expanded roadmap/specification and tab-focus correction | accepted | `36d5ad6` |
| M03 | Incremental MI parser | accepted | `ee1cfc5` |
| M04 | Nonblocking transport/controller and owned lifecycle | accepted | `f29ce3c` |
| M05 | Native sessions, console/output and Save Log | accepted | `0278b61` |
| M06 | Selected context, Call Stack and Threads | accepted | `1b21504` |
| M07 | Live code, stepping modes, navigation/source recovery | accepted | `8d69475` |
| M08 | Disassembly research/approved changes, then code breakpoints and Run to Selected | accepted | `55ecbd4` |
| Post-M08 | Reversible pane-local tabs | accepted | `0023150` |
| M09 | CPU registers | accepted | Not recorded |
| M10 | Persistent inline Memory and embedded-readiness review | accepted | `2f951fb8dbaa10f490e5abd6eba31d1ced1dc6af` |
| M11 | Scoped read-only Locals/Watches | accepted | `77446434d8e6e1e1a356abaa4d94e028a1098036` |
| M12 | Hardware data watchpoints and cross-view actions | accepted | `f1dbb8a70033821e33e9a5506e23758d0723587d` |
| M13 | Basic profiles and key mappings | accepted | `4030df3fa41da5805cedf1310b8c8ee7694b3112` |
| M14 | Native integration acceptance | accepted | `1dee59f33323d37af9739ce846f1a676ea56d2d5` |
| M15 | Existing remote server | accepted | `75435199e498cc087941cd8a75d7232b65d4c720` |
| M16 | OpenOCD wizard/lifecycle/actions | accepted | `18e460ddd4cbc897409e2879c03ed0fc983212df` |
| M17 | NUCLEO-F446RE and ESP32-S3 validation | accepted | `f568bd2482b2309e3534ebc8e197854ada37bf77` |
| M18 | v0.1.0 release evidence/acceptance | accepted; published | v0.1.0; `ad606dc6d7af7a3384401fb97e7c328ea1d8b62b` (preparation) |
| F19 | Config, built-in themes and variants | accepted; shipped in v0.1.1 | [Recorded checks](validation.md#f19--themes-and-configuration) |
| F01 | Setup restoration | in progress; capture/save implemented, restoration pending | — |
| F02–F18, F20–F47 | Independent versioned future gates below | planned | — |

## Development priorities

The Bugger is embedded-first, not embedded-only. Native Linux remains supported
through maintenance, regression prevention, compatibility and shared UI/debugger
improvements. After v0.1.0, embedded workflows drive feature development;
substantial native-specific additions require a correctness need, important defect
or explicitly approved requirement. Native usability remains important without
requiring equal development effort or feature parity.

The released v0.1.0 baseline retains M14–M18 native acceptance, remote connections,
OpenOCD lifecycle, physical-board acceptance and release evidence.
No post-release capability expands that baseline. Version targets express intended
ordering, not dates or compatibility guarantees. F22 leads v0.4.x independently
of text-channel readiness.

## Milestone specification conventions

Each gate is a bounded deliverable with the scope, exclusions, prerequisites,
automated checks and manual conditions below. Technical prerequisites and release
family ordering are separate from completion status. The [requirements](requirements.md)
define FR/NFR obligations; [testing](testing.md) supplies reproducible procedures
and hardware evidence boundaries. The [maintainer process](maintaining.md) defines
review, acceptance, combined routine commits and release-level revision records.

Every implementation gate retains NFR-01/03/05/06/08/09/10/11/12: strict C builds,
ownership/cleanup, responsiveness, mouse/keyboard usability, readable diagnostics,
small testable interfaces, terminal/tmux compatibility, non-color indicators and
reproducibility. Protocol gates emphasize NFR-04, inspection NFR-07 and integration
NFR-02 measurements. Evidence must distinguish failures, unavailable tools and
unverified hardware; milestone completion is not release acceptance.

## v0.1.0 milestones

The following table supplies objective, technical prerequisites, principal scope
and exclusions, automated verification and objective manual acceptance. The gate
contract above supplies per-milestone NFRs, documentation and completion rules.
All new v0.1.0 capabilities are release evidence requirements, not just visible UI.
Completed rows describe their original bounded deliverables; they do not limit
functionality added by later completed milestones. Current availability is
summarized above and in the [usage guide](usage.md).

| Gate / prerequisite | Objective and bounded deliverable | Automated verification | Manual acceptance criterion |
|---|---|---|---|
| M00 / foundation | Repository foundation: C project skeleton, build, test harness, README, validation ledger, CI proposal/config if practical | Clean build, test command, launchable `the-bugger --version` or equivalent; documented environment | Completion evidence is recorded in the validation register; [testing](testing.md) provides current procedures. |
| M01 / foundation | Startup menu + Monokai Pro six-pane **static/demo shell**, tab/view chooser and toolbar visual language | Mouse/keyboard navigation and layout paint work locally; all simulated views clearly marked `DEMO` | Completion evidence is recorded in the validation register; [testing](testing.md) provides current procedures. |
| M02 / foundation | Tiled layout engine: approved six-pane default, tabs, resize, click targets, **dynamic splits**, robust terminal/tmux resizing; **no layout presets** | Layout unit tests and manual click/resize/7th-view checklist pass; no preset selector appears | Completion evidence is recorded in the validation register; [testing](testing.md) provides current procedures. |
| P00 / foundation | Approved documentation/roadmap, reference-JPG removal, narrow tab-focus correction | C visual/input regression, normal/Clang/sanitizer/analyzer/tmux checks, manual tab acceptance; stop before backend | Completion evidence is recorded in the validation register; [testing](testing.md) provides current procedures. |
| M03 / accepted M02 and P00 | Incremental owned MI parser; no subprocess/live session. High protocol/bounds risk. | Nested/escaped/result/async/stream/token corpus; fragmented/combined, malformed, oversized and deeply nested records; sanitizers. | Run corpus demonstration, inspect decoded records and deterministic failures; no crash/leak. FR-16 foundations; NFR-04. |
| M04 / M03 | Nonblocking transport/controller, queued writes, tokens, deadlines, async state and owned GDB lifecycle; no live inspection. | C fake-MI emitter with delay/reordering, partial writes, floods, EOF, timeout, startup/signal/reaping failures and obsolete generations. | Drive explicitly fake sessions, observe responsive input, useful errors and no orphan processes. NFR-02/03/04. |
| M05 / M04 | Real native launch/attach, Continue/Pause, state, isolated output, console and Save Log; inspection unavailable until its gates. | Real GDB fixtures: argv/cwd/env, spaces, attach allowed/denied, control-character isolation, bad command, file-write failure, exit/cleanup. | Launch/pause/continue, inspect output, use console, save log, attach/detach owned fixture and recover configuration. FR-02/03/12/19/29. |
| M06 / M05 | Selected thread/frame, stopped-context generations, Call Stack and Threads; no RTOS inspectors. | Recursive/multithread fixtures; thread exit, inline/tail frames as GDB supplies, missing unwind, rapid switches/stale replies. | Switch context and verify real names/arguments and honest unavailable frames. FR-10, NFR-07. |
| M07 / M06 | Live Source/Disassembly, mixed display, source/instruction stepping, Step Out, navigation and source recovery; no PC-changing commands. | GDB comparisons at -O0/-O2/stripped; multi-instruction lines, retained mode, moved sources, spaces/duplicate basenames/path failures. | Step from each pane/toolbar; verify synchronization, AT&T, caller-frame label, navigation and moved-tree recovery. FR-06/07/21/22/28. |
| M08 / M07 | Completed disassembly research and code-aware browsing, code breakpoints, conditions, ignore counts, pending/multiple locations, hardware requirement and Run to Selected. No logpoints/catchpoint UI; [architecture](architecture.md#image-boundaries-and-code-breakpoints) describes the resulting implementation. | Research decisions define focused disassembly tests; original breakpoint tests retain set/hit/disable/remove/events, invalid conditions, unresolved locations, exhausted hardware, temporary cleanup and intervening stops/signals. | Review/approve browsing decisions and validate them, then exercise gutter/keys/list/properties and run to line/address without damaging user breakpoints. FR-07/22/28 and FR-11/23/24. |
| M09 / M06 | Discovered CPU registers, formats, changes and available groups; no editing/custom groups/bitfield decoder. | Native GDB and x86/ARM MI fixtures; unusual/large sets, unavailable values, frame/thread changes and change-baseline reset. | Compare GDB values, change format, step and inspect context-correct changes. FR-08/20, NFR-07. |
| M10 / M06 and view-instance state | Independent Memory, bounded expression/address reads, hex/ASCII, grouping/target byte order; no editing/search/export/comparison. | Buffers, partial/unreadable ranges, invalid expressions, overflow/endian, separate instances/paging and no unsolicited MMIO reads. | Inspect two buffers independently, scroll/group, and verify invalid-address and deliberate-refresh behavior. FR-09/20/26. |
| M11 / M06 (accepted) | Scoped read-only Locals/Watches, types/formats/changes, lazy children/array pages; pre-dispatch read authorization and explicit unknown-region inspection. No editing/Auto/Statics/STL-specific UI. | C/C++ aggregates/pointers, optimized/unavailable, invalid/mutating expressions, complete-footprint/region denial before requests, bounds, baselines and varobject cleanup. | Add/read/remove/expand watches, page arrays and change frames; no fabricated values or automatic unknown-region reads. FR-10/27. |
| M12 / M08, M10, M11 (accepted) | Hardware-required value-change/read/access watchpoints and Memory/variable actions; explicit creation, backend type reconciliation and execution interlock; no software fallback. | Scope expiry, width/overflow/resource errors, fallback/downgrade removal, malformed/late replies, context races and cleanup ownership. Real native/TUI tests remain distinct from model evidence. | Trigger supported modes, review width/address, inspect hit reason, enable/disable/remove and recover unsupported/cleanup errors. FR-25. |
| M13 / session model and M07 (accepted) | Basic XDG/json-c connection/source profiles, recent saved sessions and key remapping; inert OpenOCD/remote definitions, no rich setup restoration or remote execution. | Schema/round-trip/corrupt/atomic-write/stale-writer errors, literal argv, ordered source restore with execution interlock, CRUD/key validation, real relocated-source breakpoint and TUI reopen. | Save/reopen/edit/delete with review, recover preserved bad data, remap key and retain mouse alternatives; PID/environment/runtime state excluded. FR-01/06/14/28. |
| M14 / M05–M13 | Native integration/acceptance and failure hardening; no new feature family. | Complete C/C++ native suite, optimized/stripped/attach, repeated sessions/output floods, cleanup, sanitizers and measured UI/idle behavior. | Complete native live workflow directly/in tmux, splits, watchpoints, source recovery, profiles and Save Log. FR-02/03 and native release evidence. |
| M15 / M14 | Generic remote/extended-remote with optional symbols; no OpenOCD ownership in existing-server mode. | Real external gdbserver, disconnect/reconnect/refused/timeout, missing symbols, architecture mismatch and external survival. | Debug through external endpoint and disconnect without terminating its server. FR-05/16. |
| M16 / M15 | OpenOCD catalog/filter, board/advanced ordered argv, managed server, Reset/Halt and disconnect choices; no Load/Flash UI. | Discovery/order/no duplicated dependencies, paths, readiness/timeout/busy ports, fake-server failures, profiles and ownership. | Review exact scripts/argv; recover missing/bad setup; inspect actions/logs. No hardware claim from offline tests. FR-04/18/30. |
| M17 / M14–M16 | Real ARM/Cortex-M and Xtensa acceptance (NUCLEO-F446RE and ESP32-S3 fixtures) with reproducible fixtures; no automatic programming or unspecified-board claim. The every-release matrix below remains separate. | Firmware fixture builds, architecture MI regressions and applicable native suites; QEMU ARM/RISC-V runs as supplementary coverage when available. | Program fixtures externally; validate halt/step/break/watch, source/assembly/registers/RAM/locals, cleanup and disconnect on real boards. FR-15/31. |
| M18 / M14 and physically validated M17 | Release evidence, compatibility, docs and remaining defects; no deferred features. | Clean GCC/Clang, suites/sanitizers/analysis/tmux, stress and FR/NFR evidence audit. | README clean-checkout workflow, per-release matrix below, final release acceptance and license/provenance finalization under the [release procedure](maintaining.md#release-procedure). |

M13’s row records its original accepted scope. Current personal keybinds have
moved to Config → Keybinds and are excluded from connection profiles; historical
M13 validation remains evidence for that earlier implementation.

## Integration prerequisites and known gaps

The following existing gates own the technical gaps identified during inspection.
No additional feature family or milestone renumbering is introduced.

| Gate | Added prerequisite and deliverable | Objective acceptance |
|---|---|---|
| M11 (accepted) | Gate variable creation/evaluation/children before dispatch; complete safe-region proof for automatic reads, explicit inspection otherwise. Bounded context-scoped trees. | Calls/mutations rejected; no proof means no automatic request; explicit pointer/page actions, C/C++ fixtures and cleanup verified. |
| M12 | Preserve hardware-resource/support errors as target capabilities; do not infer hardware slots from Linux results. | Synthetic ARM/Xtensa/RISC-V responses and actual native supported modes; no silent software fallback. |
| M13 | Persist connection definitions/source rules/action mappings without runtime PID/region/value assumptions. | Existing-server definitions round-trip without running a native launch or claiming target connection. |
| M14 | Complete native workflow; resolve outstanding full-project analyzer findings and audit adapter boundaries/value-producing requests, including Call Stack simple arguments, before remote integration. | Existing native regressions stay green; full analyzer passes or findings have explicitly reviewed dispositions; inventory automatic reads/policy, test denial before unsafe dispatch; measure responsiveness and terminal/tmux behavior. |
| M15 | Extract necessary shared session ownership/actions from `TbNative`; remote PIDs never become local kill/proc targets. Populate explicit capabilities, byte order/address units, image/region metadata and conservative read authorization. Do not equate target-description readability with side-effect safety. | Real external gdbserver plus no-local-PID ARM/big-endian/unknown-metadata MI fixtures; no automatic value requests without complete safe-access evidence, explicit inspection fallback, RAM/Flash browsing, reconnect/external-server survival; no unconditional x86 settings. |
| M16 | Backend-specific OpenOCD setup, capability-dependent Reset/Halt and disconnect target-state policy; installed script discovery and argv review. | No symbol load flashes/resets/resumes; duplicate-script/USB/port/timeout failure tests; only owned server cleanup; unsupported state transitions diagnosed. |
| M17 | Validate target metadata/actions on NUCLEO-F446RE and ESP32-S3. | Real source/assembly/registers/RAM/Flash/locals/break/watch evidence, reset/disconnect choices and no speculative peripheral reads; native regression preserved. |
| M18 | Audit actual capability matrix and host-vs-target limits. | All release-blocking evidence and release acceptance; no unrun board marked supported. |

## Release validation

Except for the explicit [version-specific RISC-V deferrals](requirements.md#release-acceptance),
every release, including patches, requires native Linux x86-64 and real STM32/ARM,
ESP32/Xtensa and ESP32/RISC-V regression. QEMU ARM/RISC-V is supplementary, not
a board substitute or a simulator feature. Capabilities are claimed only for
actually tested setups; missing evidence is UNVERIFIED. See the
[release matrix](testing.md#every-release-regression-matrix).

## Versioned future gates

Every F gate requires the released v0.1.0 core and its named prerequisites.
Each includes its own code/tests, relevant NFRs, documented demonstration and
validation; adjacent capabilities remain excluded until their own gates.
Version assignments do not authorize implementation or replace scope review.

### Release progression and priority changes

F22 precedes text-channel work in the revised embedded-first sequence. All F01–F47 IDs and feature decisions are retained. These
planning targets supersede earlier targets; they group useful workflows rather
than feature quotas or dates. Every F gate still has independent implementation,
tests and acceptance. M14 native acceptance, M15 existing server,
M16 OpenOCD/lifecycle, M17 boards and M18 release evidence remain unchanged.
No post-release feature enters v0.1.0. F22 depends on M16/M17 lifecycle and
hardware evidence, not on F13–F16, F18 or advanced monitoring. External programming
remains available throughout development and after the flashing feature ships.

| Planning target | Useful workflow and execution order | Change from previous roadmap |
|---|---|---|
| v0.1.4 → v0.1.5 | F01 restore a known setup, then F02 stack contents/bounds where established. | Preserve early setup/inspection targets. |
| v0.2.0 → v0.2.1 | F03/F04/F05 deliberate variable/RAM/CPU-register editing, then F06 decide how to organize growing information. | Preserve editing and organization targets; F06 precedes new inspectors. Pages/presets are not approved here. |
| v0.3.x | F11 register metadata/groups, F34 peripherals/SVD, F35 Cortex-M fault diagnosis; F07→F08 symbols/statics, F09 Quick Watch and F12 shareable setups as supporting gates. | Bring F34/F35 forward from v0.9.1; defer specialized STL F10. Supporting gates need not ship together. |
| v0.4.0–v0.4.x | F22 deliberate firmware Load/Flash first; independent F13/F14/F15/F16 UART/RTT/semihosting/SWO text as backends become ready, then F18 shared log tools. | Flashing moves from v0.5.x ahead of text integrations; RTT/SWO readiness cannot delay it. Native PTY input F17 moves later. |
| v0.5.x | F20/F21 memory search/export/comparison. F19 themes was delivered early after v0.1.0 and is accepted and included in v0.1.1 preparation. | Memory tools move from v0.6.x; themes do not block firmware correctness. |
| v0.6.x | F31 version-specific RTOS inspection, F33 bootloader/application images, F23 task/thread stops where IDs are backend-usable; F24 supported event catchpoints and F26 logpoints. | RTOS moves from v0.8.x, images from v0.9.0; stopping tools serve these workflows. Images do not require RTOS. |
| v0.7.x | F29 non-halting values, F41 supported counters, F32 plots from proven F13–F16/F29 sources. F27 software watchpoints and F28 execution overrides remain opt-in gates. | Bring monitoring/counters forward; no universal capability or hidden halt/resume. |
| v0.8.x | F30 safe Auto and F10 backend STL presentation. | Shared convenience follows embedded diagnosis. |
| v0.8.x–v0.9.5 | F37 instruction-trace acquisition, F36 event timelines, F38 profiling, F39 sufficient coverage, F40 measured power, each with a proven source. | Permit ready acquisitions earlier; separate feasibility gates, no mandatory analysis bundle. |
| v0.9.6 | F42 GDB hooks, F43 cancellable Autostep, F44 custom themes. | Preserve late automation/customization. |
| v0.9.7–v0.9.9 | F45 embedded multicore, F46 justified multiple-inferior workflows; late review of native F17/F25. | Embedded coordination first; native-only process expansion is not scheduled by default. |
| v1.0.0 | F47 stabilize documented supported workflows, compatibility, safety and usability. | Quality gate, not feature parity or an obligation to acquire every optional analysis device. |

F17 native line input and F25 signal-policy UI remain documented, deferred to a
v0.9.8-or-later review, possibly after v1.0.0. They are not removed or mandatory
v1 blockers by this proposal. Scheduling requires explicit priority review of need
and embedded opportunity cost. F46 retains its capability but starts from an
embedded use case; native-only expansion needs the same explicit priority review.
Existing native launch, attach, output, signals/cleanup and context correctness
remain maintained. An important defect can be fixed whenever necessary.

### Dependencies and completion conditions

M15–M17 establish capabilities, ordinary-memory evidence, image identity and
backend ownership before future target features consume them. Unknown is not
unsafe, but is not automatic-read authorization. F03–F05 check write capability,
exact footprint/width and stopped state before dispatch; read metadata cannot
silently authorize writes. F34 has two reviewable stages: description/access
policy first, then the peripheral inspector. F35 separates fault-register
interpretation from exception-frame recovery; it need not require SVD. These
are stages inside existing gates, not new features or renumbering.
F34's gate plan must select and justify a bounded C XML parser/library, reject
unsafe external resolution and retain provenance/device matching for descriptions;
no XML dependency or universal SVD compatibility is selected by this revision.

F06 must decide and, where approved, implement organization before F11/F34/F35
or other new views. Preserve primary/group split tiling, the six root groups and no layout presets.
F01 migration precedes later persisted workspace settings; F12 shares reviewed
definitions/paths rather than values, personal history or credentials. Necessary
vendor integration belongs behind the relevant server/probe/target adapter,
without turning one vendor's SDK or protocol into a core requirement.

Acquisition precedes consumers: F36 needs timestamped events from F16 or a
verified event-capable F37 setup; F38 needs a declared sampling/trace source;
F39 needs coverage-sufficient capture, not sparse PC samples; F40 needs a real
measurement device. F41 counters do not imply instruction trace. F32 can plot
existing channel samples without waiting for Live Watch. Sharing a version does
not add dependencies between otherwise independent gates.

Hardware-dependent gates inherit the [future validation matrix](testing.md#post-release-embedded-validation).
At gate planning, name board/core, probe, server/GDB/tool versions, fixture and
capabilities. Corpus success is not hardware acceptance. Missing equipment or
backend support requires technical review and rescoping/deferral of that gate; keep it
UNVERIFIED and retain its ID. A planning version does not establish purchases, proprietary dependencies,
support claims or feature removal. Optional
hardware-dependent candidates can move past v1.0.0 only through explicit scope review,
without blocking unrelated supported workflows. Every shipped release retains
the native/STM32/Xtensa/RISC-V regression gate.


| Gate / expected target | Objective, scope and prerequisites | Automated tests | Objective manual acceptance |
|---|---|---|---|
| F01 / v0.1.4 | Rich setup restoration: workspace/tabs/Memory settings/watches/code-breakpoint definitions; basic profiles first; exclude runtime values/temp breakpoints/old hardware allocations. | Round-trip/migration/corruption/missing files/unresolved breakpoints/terminal size. | Reopen setup without presenting saved values as live. |
| F02 / v0.1.5 | Dedicated Stack: raw contents, SP offsets, available variable/frame annotations and bounds/usage when known; context/Memory first; no invented high-water measurement. | Native/ARM stacks, unknown bounds, unreadable data/optimized locations. | Follow known SP and distinguish available annotations/bounds from unknown information. |
| F03 / v0.2.0 | Locals/Watches editing after M15 capability/read-policy validation; deliberate stopped writes with bounded footprint, no automatic writes or unsafe aggregate rereads. | Conversion/scope/optimized/read-only/error/refresh. | Deliberately edit a target value and verify it. |
| F04 / v0.2.0 | Memory editing after M15/M16 lifecycle/capabilities; exact width/endian and isolated RAM first. No implicit Flash programming or peripheral writes. | Bounds/partial writes/encoding/failures/refresh. | Verify exact bytes and visible failed writes. |
| F05 / v0.2.0 | CPU-register editing after M15 discovery/capabilities; stopped target only, no peripheral editor or guessed writable registers. | Writable/read-only/unavailable, generic IDs and refresh. | Change an appropriate fixture register and verify backend result. |
| F06 / v0.2.1 | Workspace information-organization **decision gate**, before new views. Evaluate pages/tab discovery/mouse/keys/small terminals; existing workspace baseline, no production redesign yet. | Geometry/input/state-preservation prototypes as needed. | Design review approves organization and implementation stages; pages remain a candidate, not a preset or preapproved design. |
| F07 / v0.3.x | Searchable Symbols after F06, with code/Watch/Memory actions and bounded GDB queries; preserve pre-dispatch value-read policy. | Filters/duplicate names/no debug info/result limits/capabilities. | Find and open intended symbol/code/data. |
| F08 / v0.3.x | Globals/Statics selection after F07/F06; symbol discovery first, with the same pre-dispatch region/complete-footprint policy as Watches. | Qualified/duplicate statics/scope/unavailable. | Monitor selected static-storage variables. |
| F09 / v0.3.x | Explicit Quick Watch after F06; tiled/inline, same read-authorization policy, no floating window. | Requested timing/errors/context/deliberate-side-effect policy. | Evaluate on request without unwanted persistent watch. |
| F10 / v0.8.x | Specialized STL using GDB facilities after F06 and core embedded inspectors; no frontend Python dependency or unconditional printer/child reads. | Printers available/absent, limits/errors/toolchains. | Inspect containers with usable raw fallback. |
| F11 / v0.3.x | Custom CPU-register groups/status-bit display after F06; metadata-backed interpretation with raw fallback; shared persistence through F01. | Persistence/missing names/metadata/formats/context. | Build useful group and inspect flags without hardcoding canonical register sets. |
| F12 / v0.3.x | Shareable profile import/export after F01; relative paths, shared/personal separation, explicit prelaunch review; no mandatory project or shell snippets. | Relocation/validation/conflicts/excluded personal/session-only data. | Share between checkouts without personal workspace/history. |
| F13 / v0.4.0 | [Interactive UART/serial console](#f13--uartserial-console-planned) after F06: deliberate RX/TX endpoint configuration and ownership; no inferred universal probe channel. | PTY routing/framing policy, bounded nonblocking I/O, line controls, loss, disconnect and cleanup. | Real firmware output and command/response, debugger coexistence and documented reset-line behavior. |
| F14 / v0.4.0 | RTT text through a verified backend after F06; capability/source validation before channel UI; no universal OpenOCD assumption. | Channels/configuration/reconnect/overflow/malformed streams. | Read/write supported real-firmware channels. |
| F15 / v0.4.0 | Explicit semihosting setup/text after F06; verified server routing and deliberate intrusion/timing policy. | Setup failures/routing/cleanup. | Known firmware output with documented intrusion/timing. |
| F16 / v0.4.0 | SWO/ITM text acquisition/decoder after F06 and verified core/probe/server/clock setup; no full instruction-trace claim. | Corpus/fragmentation/overflow/clock errors. | Known text captured on verified hardware. |
| F17 / v0.9.8+ review | Basic native PTY line input, pending renewed priority approval; no full terminal emulator, not a default pre-v1 commitment. | Echo/lines/EOF/exit/input focus. | Answer fixture prompts without sending input to GDB. |
| F18 / v0.4.x | Shared log find/filter/continuous recording after validated text sources; independent of native input F17. | Retention/filter/file failures/flood/lifecycle. | Find/filter/record responsively. |
| F19 / v0.1.1 preparation; accepted (original target v0.5.0 or earlier) | Seven built-in themes / 37 variants; Catppuccin Macchiato default; centered Config → Themes with dependent variants, Back/Preview/Save and retained Profiles. | Role/color fallbacks, contrast, non-color signals, atomic preferences, all-variant PTY preview and profile regression. | Inspect each variant directly/in tmux and restore selection; see [procedures](testing.md#themes-and-configuration) and [evidence](validation.md#f19--themes-and-configuration). |
| F20 / v0.5.x | Bounded memory search using M15 region policy; explicit ranges, no automatic unknown/MMIO scans. | Patterns/limits/gaps/cancel/latency. | Find fixture bytes and cancel large search responsively. |
| F21 / v0.5.x | Memory export/snapshot comparison after M10/M15 region policy; exact deliberate ranges, no implied full address-space capture. | File failure/identity/gaps/diffs/storage bounds. | Save range and correctly identify changed bytes. |
| F22 / v0.4.x | Deliberate firmware Load/Flash after M16/M17 lifecycle and capability evidence; confirmation/progress/verification/recovery/reset policy, no auto programming on attach. | Download events/interruption/failures/sequencing. | Program/verify real fixture, preserving independent attach workflow. |
| F23 / v0.6.x | Thread/task-specific breakpoint UI after F31 for RTOS tasks; require GDB-usable IDs/lifetimes, not guessed kernel-object addresses. | Lifetime/IDs/errors/reconciliation. | Stop for intended thread only. |
| F24 / v0.6.x | Exception/event catchpoint UI for verified backend/runtime mechanisms; F35 hardware-fault diagnosis is separate, no universal MCU catchpoint. | Supported/unsupported ABI/runtime and stop reasons. | Catch known supported event/context. |
| F25 / v0.9.8+ review | Signal-policy UI for a justified supported workflow, pending renewed priority approval; native-only expansion is not a default pre-v1 commitment. | Stop/print/pass combinations/errors. | Verify deliberate native signal behavior. |
| F26 / v0.6.x | GDB-based logpoint UI on validated native/embedded setups; document stopping/intrusion, never claim non-halting acquisition. | Format/condition/error/overhead. | Expected messages with honest stop/performance behavior. |
| F27 / v0.7.x | Explicit software watchpoints; never silent fallback. | Selection/cancel/thread limits/overhead. | Opt in and see correct results and limits. |
| F28 / v0.7.x | Set Next/change PC and Force Return; distinct explicit actions, not Run to Selected/Step Out. | Support/confirmation/context invalidation/result state. | Verify on disposable fixtures. |
| F29 / v0.7.x | Capability-dependent Live Watch after backend-specific non-halting acquisition and safe-region evidence; unknown/MMIO needs deliberate bounded authorization. | Rate/loss/staleness/disconnect/no hidden stop-resume. | Monitor running supported target; unsupported remains honest. |
| F30 / v0.8.x | Auto after proven safe bounded discovery; M11 complete-footprint authorization before value requests, no invented statement precision. | Relevance/unavailable/side-effect rejection/performance. | Useful relevant expressions evaluated safely. |
| F31 / v0.6.x | Dedicated RTOS inspectors after F06 and M15/M17; one documented kernel/version/port at a time, bounded corruption-safe reads and context capabilities. | Kernel layouts/missing symbols/version errors/corrupt lists. | Real tasks/objects and selected task context agree. |
| F32 / v0.7.x | Tiled terminal-cell value plots after suitable F13–F16 or F29 acquisition. | Bounds/time/sample indices/gaps/scales/resize. | Known changing data plotted with timing/loss visible, no oscilloscope accuracy claim. |
| F33 / v0.6.x | Multi-image controls after M15 image identity/relocation; explicit bootloader/application mappings, separate symbol loading from programming. | Relocation/overlap/duplicate symbols/removal/breakpoints. | Correct bootloader/application image inspection. |
| F34 / v0.3.x | Peripheral/SVD after F06/F11 and M15 region policy: validate description/access metadata, then bounded inspector/bitfields; read-only default, no polling or SVD-as-safety-proof. | XML inheritance/arrays/fields/access/malformed/no unsolicited reads. | Real peripherals inspected, deliberate side-effect-prone reads. |
| F35 / v0.3.x | Cortex-M fault decoder after F06 and validated core/register capabilities: register diagnosis first, guarded exception-frame recovery second; SVD optional, no fabricated PC. | Combinations/bad frames/unavailable/variants. | Inject faults; no fabricated exception PC/frame. |
| F36 / v0.8.x–v0.9.5 | Interrupt/data-event timelines after timestamped F16 or verified event-capable F37 acquisition; gaps/clock/loss are first-class. | Decoder/order/timestamps/overflow/history. | Known sequence and incomplete capture shown honestly. |
| F37 / v0.8.x–v0.9.5 | Full instruction trace: first prove a real core/probe/server capture path, then decoder/navigation; no full trace from sparse PC samples. | Decoder/synchronization/gaps/buffers. | Reconstruct known execution segment. |
| F38 / v0.8.x–v0.9.5 | Profiling after validated trace or sampling acquisition; source/attribution/statistical limits explicit, no dependency on full trace if sampling suffices. | Distributions/attribution/gaps/statistical limits. | Identify known hotspots without false exact counts. |
| F39 / v0.8.x–v0.9.5 | Coverage after F37 or other proven coverage-sufficient acquisition; sampling alone insufficient, missing capture cannot count as unexecuted. | Known executed/unexecuted paths/incomplete capture. | Correct coverage with completeness limits. |
| F40 / v0.8.x–v0.9.5 | Power analysis after actual measurement-device/source feasibility; separate opt-in integration, no power inference from execution time. | Alignment/units/loss/bounds. | Correlate workload and real measurements with stated accuracy. |
| F41 / v0.7.x | Performance counters after verified target availability, access policy and reset/overflow behavior; no universal M4F PMU claim. | Availability/overflow/units/unsupported. | Known workload on capable real target. |
| F42 / v0.9.6 | GDB command-file/session hooks; no new macro language. | Ordering/failure/cancel/persistence. | Explicit scripts/hooks with visible results. |
| F43 / v0.9.6 | Timed Autostep with cancellation. | Timers/intervening stops/disconnect/cancel. | Repeated steps without runaway queue. |
| F44 / v0.9.6 | User-defined role-based themes after F19. | Parse/validation/missing roles/fallback/persistence. | Load/edit and recover invalid definitions. |
| F45 / v0.9.7–v0.9.9 | Dedicated embedded multicore context/coordinated execution after explicit per-core address-space/action/stop ownership; validated backend first. | Core IDs/isolation/stop policies/failures. | Real multicore execution scope matches intent. |
| F46 / v0.9.7–v0.9.9 | Multiple inferiors after context isolation and a justified embedded workflow; native-only expansion needs explicit renewed approval, not a pre-v1 parity goal. | Create/exit/address spaces/focus/ownership. | Multiple supported inferiors without mixed data. |
| F47 / v1.0.0 | Stability/compatibility/documentation acceptance for supported embedded workflows and maintained native debugging; no last-minute feature family or parity quota. | Full regression/support matrix/critical-defect audit. | Supported workflows and release quality satisfy release acceptance. |

### F13 — UART/serial console (planned)

F13 is a bidirectional text console for a deliberately selected local Linux serial
TTY, not just a firmware-output viewer. It remains planned for v0.4.0 after F06's
organization decision; F22 flashing retains its earlier priority. UART requires a
real endpoint and appropriate firmware/wiring, not merely a connected debug probe.
Concrete controls and defaults are reviewed at implementation; acceptance requires:

- **Connection and configuration:** select/review the device path and identity,
  baud rate, data bits, parity, stop bits and flow control. Reject unsupported
  settings visibly rather than silently substituting them. Connect/Disconnect
  are explicit; saved settings, if provided, do not open a port automatically.
  Missing, permission-denied and busy endpoints have actionable diagnostics.
  Request exclusive access where supported, without claiming detection of every
  competing reader or terminating another program.
- **Reset-line behavior:** review DTR/RTS and close/hangup policy before opening;
  these lines can connect to board reset/boot circuitry. No implicit reset pulse,
  BREAK, flashing or startup command. Driver/device transitions on open or close
  may still affect hardware: disclose and physically test the supported setup
  rather than promise universally reset-free connections.
- **Useful input/output:** receive firmware text and deliberately send text with
  selectable none/LF/CR/CRLF endings and explicit local-echo behavior. Label the
  serial source, received text, transmitted text and connection status separately
  from GDB commands/output. Navigation shortcuts never transmit bytes; serial
  input never becomes a GDB command. Distinguish queued/accepted-by-host output
  from a firmware acknowledgement; report interrupted or partial transmission.
- **Responsiveness and display:** bounded nonblocking RX/TX work, queues, line
  sizes and retained history; handle fragmented/no-newline streams, partial
  writes and backpressure without blocking the TUI or debugger. Define encoding,
  CR/LF and control-byte handling, including split multibyte sequences and invalid
  input; received terminal escapes cannot execute terminal controls. Expose
  retention truncation, known drops and unknown loss honestly. Reception is
  independent of the debugger's running/stopped state, although halted firmware
  may naturally stop producing UART output. Allow connection before a deliberate
  target reset to capture boot messages; retain history across target resets and
  identify gaps if a USB serial device disappears during reboot.
- **Lifecycle and recovery:** disconnect, unplug/replug and write failures leave
  clear state and responsive controls. Reconnection revalidates endpoint identity
  with explicit user approval if it changed; never silently attach to another
  device reusing a path. Do not replay pending input after reconnect. Restore
  original TTY settings where possible, report cleanup failures and release only
  owned resources; closing a serial connection must not close an external server.
- **Evidence:** automated PTY tests plus a named physical UART/USB-serial setup
  prove both RX and TX, line-ending behavior, flood handling, disconnect/recovery
  and coexistence with GDB. Record supported framing/flow-control and observed
  reset-line behavior; a PTY alone cannot validate these electrical properties.
  See the [planned acceptance checks](testing.md#f13-serial-console-acceptance-planned).

F13 includes bounded on-screen history; shared search/filter/continuous recording
remains F18. RTT, semihosting and SWO remain independent F14–F16 backends. Native
inferior input remains F17. A full terminal emulator, binary file transfer and
automatic remote serial bridging are outside this console's scope.

The built-in F19 theme set is delivered; custom themes, future workspace design
and acquisition backends receive scoped design review at their gates; unresolved
choices are not implementation requirements. Core dumps/reverse debugging, a full native
terminal emulator, IDE/toolchain integration and marketplaces are not added by
this roadmap. Additional architecture/platform claims need actual validation.
