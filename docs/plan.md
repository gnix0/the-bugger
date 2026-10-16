# Development plan — v0.1.0

Approved revision: single configurable workspace, no layout presets.
This replaces the earlier conversational plan's preset-related tasks and criteria.
All other approved architecture choices, requirements and milestone gates remain.

## Specification and visual reference

AGENTS.md governs product requirements and workflow. The approved HTML governs
visual and interaction fidelity alongside it: Monokai Pro palette, six-pane
workspace, toolbar, status, tabs, view selection and startup configuration.
Adapt browser controls to terminal cells and mouse/keyboard events while
preserving their purpose and appearance as closely as terminal capabilities allow.

The approved reference is `docs/design/prototype.html`, read in full during
review and moved there from the repository root with owner authorization.
Its content is a standalone simulated UI, not backend code
or evidence of debugger functionality. No browser runtime will be introduced.
Unimplemented views must be omitted or explicitly labeled, as AGENTS.md requires.

The default arrangement is Source / Disassembly / Registers above Memory /
Locals / Console. There is no layout selector, named preset, preset persistence,
predefined alternate arrangement or preset-switching action. Tabs, view changes,
resizing, split creation and closing operate on the current tiled workspace.
Compact rendering on a small terminal preserves that workspace's state and is
not a user-selectable predefined layout. Restoring a user's own saved geometry
is permitted and does not introduce a preset catalog.

## Architecture and dependencies

Retain C17, Make, ncursesw, POSIX argv-based subprocess APIs and a nonblocking
poll event loop. Use explicit GDB/MI3 with MI2 startup fallback; structured parsing,
token correlation and context generations keep stopped snapshots coherent.
Keep all-stop semantics and explicit stale/unavailable states. Isolate launched
inferior output in a PTY, disable its startup shell and sanitize displayed output.
For x86-64, disassembly uses **AT&T syntax**, not Intel syntax.

Use a split-tree layout independent of ncurses. Keyboard and mouse dispatch the
same semantic actions. Use capability-based truecolor/256-color/monochrome and
visible alternatives/remapping for intercepted keys. Test terminal and tmux mouse
handling without changing user configuration.

OpenOCD remains optional and ownership-aware. Catalog installed scripts, preserve
explicit order, preview argv, and never automatically append adapter/target
scripts to a board script or issue flash/erase/reset/resume commands. Use json-c
for atomic, validated XDG profiles and optional saved current-workspace geometry;
environment overrides remain session-only. Keep the application and tests in C,
with Make and minimal library dependencies; no Python test dependency.

## Milestones

Each milestone depends on acceptance, owner commit/push and explicit authorization
following the preceding milestone. Risk and evidence are scoped to each gate.

| Gate | Deliverable and principal risk | Automated verification | Manual acceptance |
|---|---|---|---|
| M00 | C17 CLI, Make, tests, docs/ledger and CI proposal. Low risk. | Clean GCC/Clang builds, seven CLI tests, ASan/UBSan, static analysis and whitespace checks. | Build, help/version, honest bare-invocation status, invalid-argument exit and unchanged terminal behavior. NFR-01/03/08/09/12 foundations. |
| M01 | Startup menu/configuration demo and Monokai six-pane shell matching the HTML. Toolbar, status, tabs and view chooser; every simulated view marked DEMO. Moderate terminal-capability risk. | Input/action routing, hit regions, bounds, color fallback, PTY startup/exit and terminal restoration. | Compare against HTML within terminal limits; navigate all three session flows, toolbar and tabs by mouse and keyboard, directly and in tmux/monochrome. Verify no layout selector. FR-01 and initial FR-13/17. |
| M02 | Complete tiled workspace engine: default geometry, tabs, focus, view switching, draggable dividers, horizontal/vertical splits, closing splits and compact rendering. High interaction risk. | Deterministic/randomized split/close/resize invariants; bounds, non-overlap, focus, tab/view-state preservation, drag hit tests and PTY resize. No preset-transition tests. | Create a seventh visible view, split right/below, resize, switch/close tabs, close a split, shrink/restore terminal without state loss; repeat clicks/wheel/drag in tmux. Verify no predefined-layout controls. FR-13/17; no FR-14 preset requirement. |
| M03 | Incremental MI parser, token queue, async state, deadlines, owned subprocess lifecycle and explicitly fake MI test server. High protocol risk. | Nested/escaped/malformed/chunked corpus; reordered/delayed replies, partial writes, errors, EOF, floods, timeout and reaping; sanitizers/analysis. | Exercise mock state changes, errors/disconnect, responsiveness and child cleanup. FR-16 foundations; NFR-03/04/06/08/09. |
| M04 | Real native launch/attach, source/disassembly, run controls, breakpoints, safe console input and basic target output. High integration risk. | Real GDB C/C++ fixtures: stepping, breakpoints, literal argv, cwd/env, output isolation, attach allowed/denied, missing source/symbols, exit/cleanup. | Native reference smoke subset: launch, step, pause, breakpoint management, console errors and attach. No demo values in active sessions. FR-02/03/06/07/11/12/19 and native FR-16. |
| M05 | Target-discovered registers/change markers/formats, memory/grouping, locals, watches, frames and threads. High synchronization risk. | Known native values, failed reads/evaluations, frame/thread switches, optimized/stripped programs, stale-reply rejection, x86/ARM MI fixtures and target byte order. | Inspect known values, step/change context, manage watches and formats, see errors and changed values. No background peripheral polling. FR-08/09/10/20; NFR-07. |
| M06 | OpenOCD board/advanced wizard, owned server, existing endpoint, named session profiles/recent sessions and optional restoration of the user's current geometry/key mappings. High environment/hardware risk. | Script catalog/argv/order, profile round trips/corruption/write failures, timeout/port collision/cleanup, real external gdbserver ownership test. | Configure exact board argv, save/edit/delete/reopen sessions, restore user geometry, connect to external server and real board where available. FR-04/05/14/18 and remote FR-16. Hardware unavailable means UNVERIFIED. |
| M07 | Native/STM32 integration, graceful failures, docs/capture, provenance and final release evidence. Hardware/owner evidence required. | Full builds/suites/sanitizers/analysis, repeated lifecycle/PTY stress and measured responsiveness/idle CPU. | Complete native and real NUCLEO-F446RE smoke tests, terminal/tmux input and color matrix; all FR-MUST evidenced, every NFR assessed. Confirm absence of preset UI. FR-15 and release-wide gate. |

M01 establishes visual language; M02 completes layout interactions. Neither
implements named layouts. M04/M05 validate native debugging before M06/M07
validate the reference board. ARM protocol fixtures never replace real hardware.
Flash UI, SVD/RTOS/trace/profiling, other architecture validation and IDE/editor
features remain outside v0.1.0. License/contribution/security setup stays deferred
to owner-controlled release finalization.

## Acceptance and version control

Implement only one authorized milestone, run and inspect relevant checks, update
the ledger, then stop with reproducible manual steps and limitations. After
explicit acceptance, provide only the requested Conventional Commit message:
title at most 50 characters, followed by a detailed body. The owner commits and
pushes; wait for confirmation and explicit authorization before advancing.

M00 was manually accepted and committed as `0f5d05b`. The owner confirmed the
documentation update (`86820c4`) and explicitly authorized the next milestone.
M01 was manually accepted and committed/pushed as `60df655`. The owner then
authorized M02. Current scope is M02 only, followed by manual acceptance and
the same owner-controlled commit/push gate. M03 remains planned.

M01 visual correction: the initial presentation was not accepted. Apply the
component comparison in `docs/design/tui-mapping.md` and inspect the actual
rendered interface against the HTML before acceptance. Renderer/color tests
supplement that review; panel count and interaction tests cannot replace it.
Milestone boundaries and the no-presets requirement remain unchanged.
