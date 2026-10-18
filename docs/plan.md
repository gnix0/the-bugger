# Development roadmap — v0.1.0 through v1.0.0

Approved roadmap. Version targets are expected sequencing, not deadlines.
M00–M09 and the reversible pane-tab correction are accepted. M08 is `55ecbd4`;
the pane-tab correction is `0023150`. M09's SHA was not supplied or checked.
M10 is accepted at `2f951fb8dbaa10f490e5abd6eba31d1ced1dc6af`. Its completed
scope includes the inline Memory correction and embedded-first architecture review.
M11 Locals/Watches is accepted at `77446434d8e6e1e1a356abaa4d94e028a1098036`.
This separate documentation handoff records its implementation and validation.
M12 remains planned; no implementation is included in this handoff.
Native Linux remains the implementation/validation focus through M14; embedded
development is the primary product direction, with real hardware gates preserved.
Simple positive feedback without issues implies acceptance and next-milestone
authorization unless specified otherwise. Questions/instructions do not. The
owner handles commits/pushes. "Approved" means continue directly without Git
checks or repeated commit/push confirmation. No agent commits/pushes are allowed.

## Product and preserved work

Quit interaction: `q` closes/backs out of the current screen outside text
fields; the workspace confirms session closure and returns to the launcher.
At the launcher `q` stays open. `Ctrl+Q` exits the application from any screen,
retaining active-session confirmation. Input fields accept literal `q` and use
Escape to cancel. Footers explicitly distinguish close/back from quit app.

The Bugger is an embedded-first terminal-native GDB frontend written in C for
microcontrollers, firmware and system-level C/C++ across GDB-supported architectures.
Native Linux application debugging remains a fully supported capability.
GDB supplies debugging; OpenOCD is an optional server, not a universal dependency.
C17, Make, ncursesw, POSIX APIs, and C test harnesses remain the implementation
foundation. C++ is permitted for debug-target fixtures. No simulator, browser
runtime, new macro language, IDE/build integration, or public plugin SDK is added.

M00, M01, M02 and the post-M02 navigation corrections are accepted and preserved.
Their commits are respectively 0f5d05b, 60df655, 094fa71 and a832eef. The former
unimplemented M03–M07 grouping is replaced by M03–M18 below; Git history is not
rewritten. Existing demo data proves visual behavior only. The MI parser and
controller have independent diagnostic tools. Native launch/attach, Continue/Pause,
console/output and Save Log integrate with the workspace. M06 adds selected
context, Call Stack and Threads. M07 adds live Source/Disassembly, mixed/AT&T,
focus-driven stepping, read-only code navigation and source recovery; the owner
accepted it. M08 adds bounded code-aware browsing, live code breakpoints and
Run to Selected. M09 adds read-only target-discovered CPU registers, formats
and change indicators. Other inspection and profile persistence remain in their
later gates. Explicit --demo preserves
the visual-reference sample and its tests independently of live sessions.

AGENTS.md governs requirements and workflow. The approved visual/interaction
reference is [the HTML prototype](design/prototype.html), not a backend. Preserve
Monokai Pro, toolbar/status, six panes (Source / Disassembly / Registers above
Memory / Locals / Console), tabs, view selection, scrollbars, resizing and dynamic
tiled splits. Six panes are the default, not a limit. **No layout presets,
layout selector, floating windows, editor/Vim modes, or workspace pages now.**
Terminal cells require compact surfaces, separators and explicit menus in place
of browser typography, rounded corners, hover behavior and pixel spacing.

## Gate contract, deliverables and documentation

Each milestone includes only its named functional scope, focused C tests/fixtures,
documentation and a reproducible demonstration. Adjacent features are excluded
until their own gate. Technical prerequisites appear below; execution additionally
requires acceptance, owner commit/push, and authorization after the preceding gate.
Do not combine gates without owner approval.

Every implementation gate inherits NFR-01/03/05/06/08/09/10/11/12: strict C builds,
cleanup/ownership, responsive mouse and keyboard operation, readable errors,
small testable interfaces, terminal/tmux compatibility, non-color indicators and
reproducible evidence. Protocol gates particularly cover NFR-04; inspection gates
NFR-07; transport/integration gates measure NFR-02 instead of inventing timings.

Historical M10-specific sequence: update requirements/instructions first, implement and
validate, and prepare the documentation before manual acceptance (README last).
Keep implementation and documentation staging paths separate. Stop for explicit
acceptance; only then provide the implementation commit message. After the owner
commits/pushes code, read its real SHA and finalize documentation/ledger references
and confirmed acceptance results. Present the documentation commit message and
stop. This M10-specific sequence is historical; subsequent gates follow the
lifecycle below. Map tests to FR/NFR IDs.
Record exclusions, failed/blocked checks, and compatibility/hardware limitations.

Lifecycle: implement → focused checks → provide staging commands and a proposed
Conventional Commit message (title at most 50 characters, detailed body) →
**stop for manual review** → fix feedback and revise the message as needed →
acceptance → owner commits/pushes code → separate documentation handoff and
owner commit/push → continuation. An owner-created milestone commit establishes
acceptance; do not leave that gate awaiting approval. Simple positive feedback
without issues also authorizes continuation unless specified otherwise;
questions/instructions do not. Never bypass the commit/push gate.
Normally include a proposed message at delivery. The accepted M10 completion instead
waits for explicit manual approval before presenting the implementation message,
as requested by the owner. Neither a message nor tests authorize advancement.
No autonomous commit, push, tag, merge, rebase, or milestone advancement. Neither
automated success nor silence is acceptance. Every release requires its own owner
acceptance; no prerelease suffixes are used.

Agents run only quick, focused checks. Supply exact commands for lengthy test,
stress, sanitizer or compiler-matrix runs and wait for the owner's results.
Record agent-run and owner-run evidence separately; pending checks are unverified.

Historical accepted exception: M08 documentation and the reversible pane-tab
correction were combined in `0023150`; subsequent gates use separate code/docs commits.
Pane `...` choices toggle local membership: included views alone have a filled
circle at the right (ASCII star fallback); defaults start marked. Add/activate
absent views or remove included ones, retaining a survivor and at least one tab.
Other panes are independent; promotion and per-view positions remain unchanged.
This is current UI behavior, not the later persistence/templates feature.

## P00 — approved planning handoff and tab correction

Update the specification and existing roadmap/architecture/testing/progress notes,
record all versioned decisions and research references, remove
**iar_debug_ui_example.jpg**, and fix the inactive-tab highlight. Preserve active
promotion to the left; rebind focus after tab reordering; mouse activation must
not leave reverse-video on the inactive tab. Keyboard focus remains visible.
Add C regression coverage for repeated switching without an intervening click,
keyboard activation and monochrome/direct/256-color behavior. Update README last
as product documentation, distinguishing implemented demo features from plans.

Checks: make test; make CC=clang BUILD_DIR=build/p00-clang test; make sanitize;
make analyze; make test-tmux; git diff --check. Environment failures are not passes.
Manual pass: switch Disassembly/Call Stack repeatedly, confirm promotion and
immediate inactive styling, then use keyboard focus/activation. Repeat in tmux
and monochrome. No backend feature is included. Stop at awaiting-user.

## Architecture and v0.1.0 behavior

Keep TUI/layout, session, debugger model, MI transport/parser and OpenOCD lifecycle
separate. Use one nonblocking poll event loop with bounded processing and owned
argv-based subprocesses. Start MI3; fall back to MI2 only on interpreter startup
failure before connecting. Use all-stop semantics with asynchronous transport.
Parse nested records/streams, tokens and incremental input; do not derive state
from human CLI text. Commands lacking an MI equivalent may use GDB console
commands through MI with explicit error/capability handling.

Introduce small internal interfaces for owned MI trees, token-correlated requests,
session/stop/context generations, coherent snapshots, view-instance settings and
versioned profiles. No public SDK. Invalidate obsolete replies on resume,
reconnection and frame/thread changes. Show stale/unavailable/optimized-out/read
errors honestly. Fetch visible data/expanded children in bounded requests.
Reject mutating expressions and inferior function calls conservatively during
automatic inspection; the deliberately operated GDB console remains available.

Source/Disassembly use GDB PC, frame and source mappings, including optimized,
inlined and tail-call behavior. Source focus selects source stepping; Disassembly
focus selects instruction stepping. Toolbar/inspection clicks preserve the last
code-pane mode, shown visibly. Execution highlights stay synchronized; browsing
never moves PC. Distinguish an inspected caller frame from the actual stopped PC.
x86-64 disassembly uses **AT&T syntax**. Mixed source/assembly is optional and
falls back to assembly when source information is absent.

Include Run to Selected; conditional breakpoints/ignore counts; Default / Require
hardware code-breakpoint properties; hardware-assisted value-change/read/access
watchpoints with explicit resource errors and no software fallback. Independent
Memory views keep inline navigation, address/expression, grouping and scroll.
Go/Refresh are deliberate bounded reads; stops mark old data stale. Wheel/keys
can fetch adjacent blocks only within established ordinary-memory regions;
unknown/MMIO boundaries use explicit Go, with no speculative prefetch or polling.
Registers are target-discovered, grouped only where metadata supports it;
otherwise show an honest ungrouped display. Expand Locals/Watches lazily through
MI variable objects and bounded arrays; no specialized STL display yet.

Include source-file choice, text find, line/address/symbol navigation, return to
stopped context, source search directories and ordered build-prefix → local-prefix
mapping using GDB. Include captured native output, readable debugger/server logs,
manual console and basic Save Log with visible file errors. Basic named profiles,
recent sessions and remapped keys use versioned json-c data and atomic XDG writes;
structured argv/scripts and source settings are saved, environment overrides are
session-only. Rich workspace/watch/breakpoint restoration is F01, not v0.1.0.

Native Launch may Restart with confirmation; native Attach has no Reset action.
OpenOCD Reset/Halt is confirmed and explicit. Embedded disconnect asks leave halted
or resume where supported, disclosing backend/script limits. Never silently
rewrite external server scripts, kill an external server, flash, erase, reset or
resume on connection. Catalog installed scripts, preserve explicit order/paths,
and never automatically append interface/target files to board configurations.

ncursesw retains truecolor/256-color/monochrome fallbacks, matching terminfo,
explicit mouse menus and visible key alternatives/remapping. tmux mouse is opt-in;
do not rewrite user configuration. Current GDB 18.1 and the development OpenOCD
build are environment evidence, not a compatibility guarantee for other versions.
Probe support and report incompatible ARM GDB/server behavior.

## v0.1.0 milestones

The following table supplies objective, technical prerequisites, principal scope
and exclusions, automated verification and objective manual acceptance. The gate
contract above supplies per-milestone NFRs, documentation and completion rules.
All new v0.1.0 capabilities are release evidence requirements, not just visible UI.

| Gate / prerequisite | Objective and bounded deliverable | Automated verification | Manual acceptance criterion |
|---|---|---|---|
| M03 / accepted M02 and P00 | Incremental owned MI parser; no subprocess/live session. High protocol/bounds risk. | Nested/escaped/result/async/stream/token corpus; fragmented/combined, malformed, oversized and deeply nested records; sanitizers. | Run corpus demonstration, inspect decoded records and deterministic failures; no crash/leak. FR-16 foundations; NFR-04. |
| M04 / M03 | Nonblocking transport/controller, queued writes, tokens, deadlines, async state and owned GDB lifecycle; no live inspection. | C fake-MI emitter with delay/reordering, partial writes, floods, EOF, timeout, startup/signal/reaping failures and obsolete generations. | Drive explicitly fake sessions, observe responsive input, useful errors and no orphan processes. NFR-02/03/04. |
| M05 / M04 | Real native launch/attach, Continue/Pause, state, isolated output, console and Save Log; inspection unavailable until its gates. | Real GDB fixtures: argv/cwd/env, spaces, attach allowed/denied, control-character isolation, bad command, file-write failure, exit/cleanup. | Launch/pause/continue, inspect output, use console, save log, attach/detach owned fixture and recover configuration. FR-02/03/12/19/29. |
| M06 / M05 | Selected thread/frame, stopped-context generations, Call Stack and Threads; no RTOS inspectors. | Recursive/multithread fixtures; thread exit, inline/tail frames as GDB supplies, missing unwind, rapid switches/stale replies. | Switch context and verify real names/arguments and honest unavailable frames. FR-10, NFR-07. |
| M07 / M06 | Live Source/Disassembly, mixed display, source/instruction stepping, Step Out, navigation and source recovery; no PC-changing commands. | GDB comparisons at -O0/-O2/stripped; multi-instruction lines, retained mode, moved sources, spaces/duplicate basenames/path failures. | Step from each pane/toolbar; verify synchronization, AT&T, caller-frame label, navigation and moved-tree recovery. FR-06/07/21/22/28. |
| M08 / M07 | First research/review disassembly browsing, then implement owner-approved findings, then complete code breakpoints, conditions, ignore counts, pending/multiple locations, hardware requirement and Run to Selected. See the staged scope below; no logpoints/catchpoint UI. | Research decisions define focused disassembly tests; original breakpoint tests retain set/hit/disable/remove/events, invalid conditions, unresolved locations, exhausted hardware, temporary cleanup and intervening stops/signals. | Review/approve browsing decisions and validate them, then exercise gutter/keys/list/properties and run to line/address without damaging user breakpoints. FR-07/22/28 and FR-11/23/24. |
| M09 / M06 | Discovered CPU registers, formats, changes and available groups; no editing/custom groups/bitfield decoder. | Native GDB and x86/ARM MI fixtures; unusual/large sets, unavailable values, frame/thread changes and change-baseline reset. | Compare GDB values, change format, step and inspect context-correct changes. FR-08/20, NFR-07. |
| M10 / M06 and view-instance state | Independent Memory, bounded expression/address reads, hex/ASCII, grouping/target byte order; no editing/search/export/comparison. | Buffers, partial/unreadable ranges, invalid expressions, overflow/endian, separate instances/paging and no unsolicited MMIO reads. | Inspect two buffers independently, scroll/group, and verify invalid-address and deliberate-refresh behavior. FR-09/20/26. |
| M11 / M06 (accepted) | Scoped read-only Locals/Watches, types/formats/changes, lazy children/array pages; pre-dispatch read authorization and explicit unknown-region inspection. No editing/Auto/Statics/STL-specific UI. | C/C++ aggregates/pointers, optimized/unavailable, invalid/mutating expressions, complete-footprint/region denial before requests, bounds, baselines and varobject cleanup. | Add/read/remove/expand watches, page arrays and change frames; no fabricated values or automatic unknown-region reads. FR-10/27. |
| M12 / M08, M10, M11 | Hardware-assisted data watchpoints and Watches/Memory actions; no software fallback. | Value-change/read/access where supported; scope expiry, expression/width/alignment/resource failures, fallback rejection and backend reconciliation. | Trigger supported watch modes on known data, inspect reason, disable/remove and see unsupported errors. FR-25. |
| M13 / session model and M07 | Basic XDG connection/source profiles, recent sessions and key remapping; no rich setup restoration. | Schema/round-trip/corrupt/truncated/atomic-write errors, argv/spaces, source rules, CRUD and key validation. | Save/reopen/edit/delete profile, recover bad data, remap intercepted key and retain mouse alternatives. FR-01/06/14/28. |
| M14 / M05–M13 | Native integration/acceptance and failure hardening; no new feature family. | Complete C/C++ native suite, optimized/stripped/attach, repeated sessions/output floods, cleanup, sanitizers and measured UI/idle behavior. | Complete native live workflow directly/in tmux, splits, watchpoints, source recovery, profiles and Save Log. FR-02/03 and native release evidence. |
| M15 / M14 | Generic remote/extended-remote with optional symbols; no OpenOCD ownership in existing-server mode. | Real external gdbserver, disconnect/reconnect/refused/timeout, missing symbols, architecture mismatch and external survival. | Debug through external endpoint and disconnect without terminating its server. FR-05/16. |
| M16 / M15 | OpenOCD catalog/filter, board/advanced ordered argv, managed server, Reset/Halt and disconnect choices; no Load/Flash UI. | Discovery/order/no duplicated dependencies, paths, readiness/timeout/busy ports, fake-server failures, profiles and ownership. | Review exact scripts/argv; recover missing/bad setup; inspect actions/logs. No hardware claim from offline tests. FR-04/18/30. |
| M17 / M14–M16 | Real release-target acceptance, starting STM32, then owner's Xtensa and RISC-V ESP32 boards. Reproducible fixtures; no automatic programming or unspecified-board claim. | Firmware fixture builds, architecture MI regressions and applicable native suites; QEMU ARM/RISC-V runs as supplementary coverage when available. | Owner programs fixtures externally; validates halt/step/break/watch, source/assembly/registers/RAM/locals, cleanup and disconnect on real boards. FR-15/31. |
| M18 / M14 and owner-confirmed M17 | Release evidence, compatibility, docs and remaining defects; no deferred features. | Clean GCC/Clang, suites/sanitizers/analysis/tmux, stress and FR/NFR evidence audit. | README clean-checkout workflow, per-release matrix below, explicit owner release acceptance and existing owner license/provenance finalization. No autonomous tag. |

## M10 completion contract and embedded prerequisites

M10 owns FR-09/20/26 and relevant NFR-02/03/04/05/06/07/10/11. It is accepted
at `2f951fb` (see the ledger). Its objective is a usable persistent Memory inspector within the
existing six-pane workspace, preserving all accepted native debugging.

- **Scope/deliverables:** inline Address/expression + Go above hex/ASCII, retained
  drafts/settings across tabs, independent instances, byte grouping/count,
  bounded forward/backward wheel and keyboard browsing, explicit Refresh,
  partial/unavailable/error states, and session/context invalidation. No address
  page, mandatory SP default, arbitrary initial target address, editing, search,
  export, comparison or new layout. Empty viewer controls stay usable.
- **Architecture:** keep navigation separate from request publication. Session
  adapters supply byte order and established ordinary-memory regions. Generic
  Memory never reads `/proc` or assumes a target OS/architecture. Native Linux
  metadata is adapter-local; unknown metadata permits explicit Go and byte display.
  Adjacent fetches require established bounds, never a blanket assumption that
  unlisted addresses are RAM. Grouping does not promise a peripheral bus width.
- **Automated acceptance:** C peer tests for endian/unknown maps, bounds,
  partial/overlap/error replies, stale/hidden/rapid navigation and no unsolicited
  reads; native fixture buffers and an unmapped boundary; PTY inline entry/Go,
  keyboard/wheel browsing, ASCII/hex, independent panes, invalid input, resize
  and terminal cleanup. Existing suites must remain green. Run focused sanitizer
  coverage and tmux forwarding checks; record blocked tools without claiming PASS.
- **Manual acceptance:** open the viewer directly, type a symbol/address inline,
  verify actual hex/ASCII, navigate both directions, refresh after stepping,
  compare two independently configured views, switch tabs, resize, recover a
  bad address, and check existing source/assembly/register/breakpoint controls.
  Real ARM/Flash/MMIO/probe acceptance remains M17, never inferred from PTYs.
- **Documentation/completion:** this specification, architecture review,
  tests/limitations and README are prepared before review. Acceptance remains
  explicit; real implementation SHA and confirmed manual results are added only
  after the owner's implementation commit, before the documentation commit.

The [M10 architecture review](architecture.md#m10-embedded-readiness-review)
assigns the following prerequisites to existing gates; no new feature family or
milestone renumbering is needed:

| Gate | Added prerequisite and deliverable | Objective acceptance |
|---|---|---|
| M11 (accepted) | Gate variable creation/evaluation/children before dispatch; complete safe-region proof for automatic reads, explicit inspection otherwise. Bounded context-scoped trees. | Calls/mutations rejected; no proof means no automatic request; explicit pointer/page actions, C/C++ fixtures and cleanup verified. |
| M12 | Preserve hardware-resource/support errors as target capabilities; do not infer hardware slots from Linux results. | Synthetic ARM/Xtensa/RISC-V responses and actual native supported modes; no silent software fallback. |
| M13 | Persist connection definitions/source rules/action mappings without runtime PID/region/value assumptions. | Existing-server definitions round-trip without running a native launch or claiming target connection. |
| M14 | Complete native workflow; resolve outstanding full-project analyzer findings and audit adapter boundaries/value-producing requests, including Call Stack simple arguments, before remote integration. | Existing native regressions stay green; full analyzer passes or findings have explicitly reviewed dispositions; inventory automatic reads/policy, test denial before unsafe dispatch; measure responsiveness and terminal/tmux behavior. |
| M15 | Extract necessary shared session ownership/actions from `TbNative`; remote PIDs never become local kill/proc targets. Populate explicit capabilities, byte order/address units, image/region metadata and conservative read authorization. Do not equate target-description readability with side-effect safety. | Real external gdbserver plus no-local-PID ARM/big-endian/unknown-metadata MI fixtures; no automatic value requests without complete safe-access evidence, explicit inspection fallback, RAM/Flash browsing, reconnect/external-server survival; no unconditional x86 settings. |
| M16 | Backend-specific OpenOCD setup, capability-dependent Reset/Halt and disconnect target-state policy; installed script discovery and argv review. | No symbol load flashes/resets/resumes; duplicate-script/USB/port/timeout failure tests; only owned server cleanup; unsupported state transitions diagnosed. |
| M17 | Validate target metadata/actions on STM32 first, then owner ESP32 Xtensa/RISC-V. | Real source/assembly/registers/RAM/Flash/locals/break/watch evidence, reset/disconnect choices and no speculative peripheral reads; native regression preserved. |
| M18 | Audit actual capability matrix and host-vs-target limits. | All release-blocking evidence and owner acceptance; no unrun board marked supported. |

## M08 staged disassembly review and original scope

Completed and accepted in code commit `55ecbd4`. The three approved stages were
research/comparison, the resulting browsing corrections, then the original
code-breakpoint/Run-to scope. Their scope and acceptance criteria are retained
below as the gate record, not outstanding work.

1. **Research and product review first.** Thoroughly examine official IAR/C-SPY,
   GDB/MI and relevant debugger documentation/implementations. Report sources and
   section references in the discussion, distinguish observed implementation
   from assumptions, and ask the owner concrete questions where needed. Investigate:
   - Browsing instructions before/after PC beyond the current four-line initial
     context, across functions/images, with PC/breakpoint overlays and return to PC.
   - Whether and where assembly is actually available at session startup versus
     obtained/decoded on demand; native and embedded/remote trade-offs.
   - The reported repeated `00 00` / `add %al,(%rax)` rows after scrolling far
     down: establish whether displayed bytes belong to code, padding/data or
     another region; do not assume the screenshot proves either valid code or
     fabricated data. Decide how unavailable/unmapped/non-code boundaries should
     be represented and navigated.
   - Suitable address/function/section boundaries, lazy fetching/cache limits,
     correct x86 variable-length boundaries, target reads and performance. Do
     not assume that eagerly decoding an entire target is necessary or desirable.
   Compare terminal-native alternatives while preserving the approved workspace,
   architecture-neutral C model, AT&T x86-64 syntax and nonblocking behavior.
   The owner decides the resulting product behavior and bounded implementation
   scope before Stage 2; do not preselect a solution in this note.
2. **Implement the approved browsing decisions.** Deliver the agreed disassembly
   changes, focused C tests and a reproducible native demonstration before the
   original M08 feature work. Research decisions must define limits/errors,
   non-functional requirements, regression fixtures and manual acceptance cases.
3. **Complete original M08.** Code breakpoints, conditions/ignore counts,
   pending/multiple locations, Default / Require hardware and Run to Selected,
   with the existing tests/exclusions above. This scope is preserved, not replaced.

Approved and delivered behavior:

- GDB decodes bounded ranges on demand; no eager whole-target disassembly.
  libelf supplies ELF32/64 section/function/object metadata, relocation bounds
  and ARM mapping symbols. Native mapped-image discovery uses `/proc/PID/maps`.
- Browse earlier/later code from trusted anchors, retaining at most 128 sorted
  instructions. Backward scanning is limited to 4096 chunks of 256 bytes.
  Known gaps/data are skipped and labeled as bytes not decoded; explicit
  Decode bytes permits raw inspection. Repeated instructions are not filtered
  by their appearance. Unknown visible ranges are labeled honestly.
- PC/breakpoint overlays remain independent of browsing. Preserve the viewport
  through menu roundtrips and five-row follow margins; align assembly columns
  and retain full text through horizontal scrolling. Warnings follow visible
  rows, not the last fetch request.
- Structured live breakpoint management covers conditions, ignore counts,
  pending/multiple locations and Default / Require hardware. Run to Selected
  owns separate temporary state, preserves user breakpoints and handles
  intervening stops, timeouts and cleanup errors conservatively.
- C tests cover section gaps, menu/viewport preservation, visible warnings,
  malformed ELF metadata, long-function browsing, quoted source identities,
  breakpoint lifecycle and hardware-resource errors. Acceptance/evidence are
  recorded in progress.md; repeatable procedures are in testing.md.

No register/Memory/variable inspector, data-watchpoint UI, remote connection,
OpenOCD workflow or persistence work was folded into M08. These remain M09–M16.

Decision sources: IAR [Disassembly window, Toolbar/Display/Context menu](https://docs.iar.com/ewarm/10.1x/en/c-spy-debugging/executing-your-application/reference-information-on-application-execution/disassembly-window.html),
GDB [MI data](https://sourceware.org/gdb/current/onlinedocs/gdb.html/GDB_002fMI-Data-Manipulation.html)
and [breakpoint commands](https://sourceware.org/gdb/current/onlinedocs/gdb.html/GDB_002fMI-Breakpoint-Commands.html),
[VS Code DisassemblyView](https://github.com/microsoft/vscode/blob/main/src/vs/workbench/contrib/debug/browser/disassemblyView.ts),
[MIEngine disassembly](https://github.com/microsoft/MIEngine/blob/main/src/MIDebugEngine/Engine.Impl/Disassembly.cs)
and [Arm AAELF32 §5.5.5 mapping symbols](https://github.com/ARM-software/abi-aa/blob/main/aaelf32/aaelf32.rst).

## Per-release architecture regression gate

**Before every release**, including patch releases, test at least the owner's
available environments: native Linux x86-64; STM32/ARM (NUCLEO-F446RE reference);
ESP32/Xtensa; and ESP32/RISC-V. This later owner instruction expands the original
Linux/STM32-only first-release acceptance matrix. Exact ESP board/probe/toolchain
configuration is not required during P00; record it with actual validation later.
Use the appropriate editable GDB and GDB-server backend; OpenOCD is optional.

QEMU-based ARM and RISC-V native/guest runs are additional coverage where a
reproducible setup is available, not a simulator product feature and not substitutes
for physical-board evidence. Do not label an unrun row passed. A missing setup is
UNVERIFIED and requires owner resolution before release acceptance, not an
invented support claim. Test core workflows on every row and new features where
capable; clearly record capability-specific exclusions. Architecture support is
claimed only for the actual tested setups. This gate applies to every F release.

## Versioned future gates

Each row is an independent milestone with code/tests, a documented demonstration,
relevant NFRs and documentation under the gate contract. All require the released
core plus the named prerequisites. Only named scope is included: no adjacent
capability silently rides along. Version targets can change only through owner
review. Hardware/backend acquisition must be proved before dependent analysis;
sparse PC samples cannot establish full instruction trace or complete coverage.
A failed feasibility prerequisite prompts a revised plan, not simulated evidence.

| Gate / expected target | Objective, scope and prerequisites | Automated tests | Objective manual acceptance |
|---|---|---|---|
| F01 / v0.1.4 | Rich setup restoration: workspace/tabs/Memory settings/watches/code-breakpoint definitions; basic profiles first; exclude runtime values/temp breakpoints/old hardware allocations. | Round-trip/migration/corruption/missing files/unresolved breakpoints/terminal size. | Reopen setup without presenting saved values as live. |
| F02 / v0.1.5 | Dedicated Stack: raw contents, SP offsets, available variable/frame annotations and bounds/usage when known; context/Memory first; no invented high-water measurement. | Native/ARM stacks, unknown bounds, unreadable data/optimized locations. | Follow known SP and distinguish available annotations/bounds from unknown information. |
| F03 / v0.2.0 | Locals/Watches editing through GDB; no implicit automatic writes. | Conversion/scope/optimized/read-only/error/refresh. | Deliberately edit a target value and verify it. |
| F04 / v0.2.0 | Memory editing with explicit width/endian; isolated RAM first. | Bounds/partial writes/encoding/failures/refresh. | Verify exact bytes and visible failed writes. |
| F05 / v0.2.0 | CPU-register editing where supported; no peripheral editor. | Writable/read-only/unavailable, generic IDs and refresh. | Change an appropriate fixture register and verify backend result. |
| F06 / v0.2.1 | Workspace information-organization **decision gate**, before new views. Evaluate pages/tab discovery/mouse/keys/small terminals; existing workspace baseline, no production redesign yet. | Geometry/input/state-preservation prototypes as needed. | Owner approves organization and implementation stages; pages remain a candidate, not a preset or preapproved design. |
| F07 / v0.3.x | Searchable Symbols with code/Watch/Memory actions; bounded GDB queries. | Filters/duplicate names/no debug info/result limits/capabilities. | Find and open intended symbol/code/data. |
| F08 / v0.3.x | Globals/Statics selection; symbol discovery first. | Qualified/duplicate statics/scope/unavailable. | Monitor selected static-storage variables. |
| F09 / v0.3.x | Explicit Quick Watch, tiled/inline; no floating window. | Requested timing/errors/context/deliberate-side-effect policy. | Evaluate on request without unwanted persistent watch. |
| F10 / v0.3.x | Specialized STL using GDB facilities; no frontend Python dependency. | Printers available/absent, limits/errors/toolchains. | Inspect containers with usable raw fallback. |
| F11 / v0.3.x | Custom CPU-register groups and status-bit display. | Persistence/missing names/metadata/formats/context. | Build useful group and inspect flags without hardcoding canonical register sets. |
| F12 / v0.3.x | Shareable profile import/export after F01; relative paths, shared/personal separation, explicit prelaunch review; no mandatory project or shell snippets. | Relocation/validation/conflicts/excluded personal/session-only data. | Share between checkouts without personal workspace/history. |
| F13 / v0.4.0 | UART/serial text integration. | Fragmentation/disconnect/configuration/bounds. | Real endpoint I/O without TUI corruption. |
| F14 / v0.4.0 | RTT text through supported backend. | Channels/configuration/reconnect/overflow/malformed streams. | Read/write supported real-firmware channels. |
| F15 / v0.4.0 | Explicit semihosting setup/text. | Setup failures/routing/cleanup. | Known firmware output with documented intrusion/timing. |
| F16 / v0.4.0 | SWO/ITM text acquisition/decoder; no full instruction-trace claim. | Corpus/fragmentation/overflow/clock errors. | Known text captured on verified hardware. |
| F17 / v0.4.x | Basic native PTY line input; no full terminal emulator. | Echo/lines/EOF/exit/input focus. | Answer fixture prompts without sending input to GDB. |
| F18 / v0.4.x | Advanced log find/filter/continuous recording. | Retention/filter/file failures/flood/lifecycle. | Find/filter/record responsively. |
| F19 / v0.5.0 or earlier | Choose 6–8 tested community-favorite themes; Monokai Pro default. | Role/color fallbacks/contrast/non-color signals/persistence. | Inspect every theme in terminal/tmux and restore selection. |
| F20 / v0.6.x | Bounded memory search. | Patterns/limits/gaps/cancel/latency. | Find fixture bytes and cancel large search responsively. |
| F21 / v0.6.x | Memory export/snapshot comparison. | File failure/identity/gaps/diffs/storage bounds. | Save range and correctly identify changed bytes. |
| F22 / v0.6.x | Deliberate firmware Load/Flash, confirmation/progress/recovery/reset policy. | Download events/interruption/failures/sequencing. | Program/verify real fixture, preserving independent attach workflow. |
| F23 / v0.7.x | Thread-specific breakpoint UI. | Lifetime/IDs/errors/reconciliation. | Stop for intended thread only. |
| F24 / v0.7.x | Exception/event catchpoint UI. | Supported/unsupported ABI/runtime and stop reasons. | Catch known supported event/context. |
| F25 / v0.7.x | Signal policies. | Stop/print/pass combinations/errors. | Verify deliberate native signal behavior. |
| F26 / v0.7.x | GDB-based logpoint UI. | Format/condition/error/overhead. | Expected messages with honest stop/performance behavior. |
| F27 / v0.7.x | Explicit software watchpoints; never silent fallback. | Selection/cancel/thread limits/overhead. | Opt in and see correct results and limits. |
| F28 / v0.7.x | Set Next/change PC and Force Return; distinct explicit actions, not Run to Selected/Step Out. | Support/confirmation/context invalidation/result state. | Verify on disposable fixtures. |
| F29 / v0.8.x | Capability-dependent Live Watch; proven non-halting acquisition first. | Rate/loss/staleness/disconnect/no hidden stop-resume. | Monitor running supported target; unsupported remains honest. |
| F30 / v0.8.x | Auto with bounded safe discovery; no invented compiler-level statement precision. | Relevance/unavailable/side-effect rejection/performance. | Useful relevant expressions evaluated safely. |
| F31 / v0.8.x | Dedicated RTOS inspectors, documented RTOS/version first. | Kernel layouts/missing symbols/version errors/corrupt lists. | Real tasks/objects and selected task context agree. |
| F32 / v0.8.x | Tiled terminal-cell value plots after suitable F13–F16 or F29 acquisition. | Bounds/time/sample indices/gaps/scales/resize. | Known changing data plotted with timing/loss visible, no oscilloscope accuracy claim. |
| F33 / v0.9.0 | Multi-image controls and explicit mappings. | Relocation/overlap/duplicate symbols/removal/breakpoints. | Correct bootloader/application image inspection. |
| F34 / v0.9.1 | Peripheral/SVD, validated descriptions and conservative access. | XML inheritance/arrays/fields/access/malformed/no unsolicited reads. | Real peripherals inspected, deliberate side-effect-prone reads. |
| F35 / v0.9.1 | Cortex-M fault decoder/recoverable exception context. | Combinations/bad frames/unavailable/variants. | Inject faults; no fabricated exception PC/frame. |
| F36 / v0.9.2–v0.9.5 | Interrupt/data-event timelines after trace acquisition. | Decoder/order/timestamps/overflow/history. | Known sequence and incomplete capture shown honestly. |
| F37 / v0.9.2–v0.9.5 | Full instruction trace on supported real setup. | Decoder/synchronization/gaps/buffers. | Reconstruct known execution segment. |
| F38 / v0.9.2–v0.9.5 | Profiling from defined trace/sampling source. | Distributions/attribution/gaps/statistical limits. | Identify known hotspots without false exact counts. |
| F39 / v0.9.2–v0.9.5 | Coverage with sufficient source; sampling alone insufficient. | Known executed/unexecuted paths/incomplete capture. | Correct coverage with completeness limits. |
| F40 / v0.9.2–v0.9.5 | Power analysis using measurement hardware. | Alignment/units/loss/bounds. | Correlate workload and real measurements with stated accuracy. |
| F41 / v0.9.2–v0.9.5 | Performance counters where available; no universal M4F PMU claim. | Availability/overflow/units/unsupported. | Known workload on capable real target. |
| F42 / v0.9.6 | GDB command-file/session hooks; no new macro language. | Ordering/failure/cancel/persistence. | Explicit scripts/hooks with visible results. |
| F43 / v0.9.6 | Timed Autostep with cancellation. | Timers/intervening stops/disconnect/cancel. | Repeated steps without runaway queue. |
| F44 / v0.9.6 | User-defined role-based themes after F19. | Parse/validation/missing roles/fallback/persistence. | Load/edit and recover invalid definitions. |
| F45 / v0.9.7–v0.9.9 | Dedicated multicore context/coordinated execution on supported backend. | Core IDs/isolation/stop policies/failures. | Real multicore execution scope matches intent. |
| F46 / v0.9.7–v0.9.9 | Multiple inferiors/process coordination after context isolation. | Create/exit/address spaces/focus/ownership. | Multiple supported inferiors without mixed data. |
| F47 / v1.0.0 | Stability/compatibility/documentation acceptance; no last-minute feature family. | Full regression/support matrix/critical-defect audit. | Owner accepts supported workflows and release quality. |

The theme set, future workspace design and acquisition backends receive scoped
review at their gates. Those future design decisions are intentional; no agent
may choose product behavior silently. Core dumps/reverse debugging, a full native
terminal emulator, IDE/toolchain integration and marketplaces are not added by
this roadmap. Additional architecture/platform claims need actual validation.

## Documentation inputs and decision provenance

Official IAR pages were examined for capability breadth, not visual copying.
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

The final research pass also consulted first-hand debugger-community reports.
Historical/closed requests and extension limitations are not claims of present
IAR/C-SPY defects. These findings directly informed accepted requirements:

- [Source mapping request](https://github.com/Marus/cortex-debug/issues/611) plus GDB source-path documentation → source-path recovery in v0.1.0.
- [Graphing request](https://github.com/microsoft/vscode-cpptools/issues/8770) and [Live Watch implementation](https://github.com/Marus/cortex-debug/issues/810) → bounded plots at v0.8.x with real acquisition limits. The latter feature was subsequently implemented in that project, not still missing.
- [Embedded tooling discussion](https://www.reddit.com/r/embedded/comments/r57z2j/what_would_you_change_in_embedded_programming/) → shareable profiles v0.3.x, separating personal state and supporting relocation.
- [Slow symbol queries](https://github.com/Marus/cortex-debug/issues/539), [setup frustration](https://www.reddit.com/r/embedded/comments/1k39h8h/) and [IAR extension context request](https://github.com/IARSystems/iar-vsc-debug/issues/19) → lazy reads, recoverable setup and correct frame/thread context, already required. The IAR issue is extension-specific and closed; full C-SPY task switching is not alleged absent.

## Current authorization

M00–M11 and the pane-tab correction are accepted. M11's implementation is
`77446434d8e6e1e1a356abaa4d94e028a1098036`; the owner confirmed acceptance and
commit/push. Complete this separate documentation handoff and stop for the
owner's documentation commit. M12 remains the next planned implementation gate;
this handoff does not begin it. Automated success alone is not acceptance.

M09 implementation uses structured register names/values, selected thread/frame,
32-value batches, a 512-ID catalog and 1024-byte bounded display values. Formats
follow GDB's Hex/Decimal/Octal/Binary/Raw/Natural modes. Canonical comparisons
survive format changes; context changes reset baselines. Read only visible rows
after stops/explicit refresh; retain honest stale/unavailable/bounded errors.
Current GDB/MI lacks structured group discovery, so display All (ungrouped)
without parsed CLI text or guessed target groups. Register editing and SVD remain
deferred. See progress.md for acceptance, the corrected caller test and evidence.

Historical M07 manual-review decisions retained in the accepted interface: provide one-cell vertical
scrollbars matching horizontal styling and overflow-only horizontal bars in live code/Console/
context views. Keep independent per-view horizontal position, mouse thumb/track
and keyboard navigation, fixed source line gutters, clamping across resize/data
changes, and no bars on a fitting axis. Preserve the accepted workspace geometry
and action footers. These corrections are accepted; they are not pending work.

The owner additionally authorized the supplied launcher image's hierarchy, using
Monokai Pro, a larger card centered on both axes, full-row choices/descriptions,
icons/chevrons and compact terminal adaptation. Keep configuration behavior and
the workspace unchanged, with no heavy rendering dependency or prerelease suffix.
Focused launcher/input/resize regressions accompany the accepted launcher.

Later M07 review refined assembly navigation: retain surrounding decoded code and
bounded window anchors across stops; allow backward/forward browsing without PC
changes. Show roughly four preceding instructions initially where valid metadata
permits; preserve the viewport until the PC reaches a five-row top or bottom
margin, scaled down for short panes and clamped to available context. Manual
browsing remains until stepping/Stopped. The default upper row uses 62% of the
usable row height rather than 56%; all user-adjusted divider ratios remain intact.
Use GDB function-offset metadata and bounded reads, never guessed x86 boundaries.
Label Shift+F11 explicitly. Step Out means function return, not leaving a loop.
