# Milestone ledger

Target: **v0.1.0**. No release has been accepted.

States: `planned` → `implemented` → `automated-verified` → `awaiting-user` →
`accepted`. Only explicit owner acceptance permits the last transition.

| Milestone | Deliverable | State | Commit SHA |
|---|---|---|---|
| M00 | Repository foundation | accepted | `0f5d05b` |
| M01 | Launcher and DEMO workspace | accepted | `60df655` |
| M02 | Tiled layout engine | accepted | `094fa71` |
| Post-M02 | Navigation/highlight/scrollbar correction | accepted | `a832eef` |
| P00 | Expanded roadmap/specification and tab-focus correction | accepted | `36d5ad6` |
| M03 | Incremental MI parser | accepted | `ee1cfc5` |
| M04 | Nonblocking transport/controller and owned lifecycle | awaiting-user | — |
| M05 | Native sessions, console/output and Save Log | planned | — |
| M06 | Selected context, Call Stack and Threads | planned | — |
| M07 | Live code, stepping modes, navigation/source recovery | planned | — |
| M08 | Code breakpoints and Run to Selected | planned | — |
| M09 | CPU registers | planned | — |
| M10 | Independent Memory | planned | — |
| M11 | Expandable Locals/Watches | planned | — |
| M12 | Hardware data watchpoints | planned | — |
| M13 | Basic profiles and key mappings | planned | — |
| M14 | Native integration acceptance | planned | — |
| M15 | Existing remote server | planned | — |
| M16 | OpenOCD wizard/lifecycle/actions | planned | — |
| M17 | Real release-board acceptance | planned | — |
| M18 | v0.1.0 release evidence/acceptance | planned | — |
| F01–F47 | Independent versioned future gates in docs/plan.md | planned | — |

The approved expanded roadmap replaces only the old unimplemented M03–M07
grouping. Older references to future milestone numbers in historical entries
below describe their original scopes; [the current roadmap](plan.md) governs
new work. Historical failures and acceptance evidence are retained. M04 is the
only currently authorized gate; no live target workflow or release is accepted.

## M00 — foundation

Implementation authorized by the owner after plan review on 2026-10-16.
Added the CLI, C17 Make build, seven integration checks, sanitizer/static-analysis
targets and foundation documentation. The existing AGENTS.md is preserved.
License and contributor/security guides remain deferred to release finalization.

State history: `implemented` → `automated-verified` → `awaiting-user` → `accepted`.
Manual acceptance: the owner reported "M00 gate tested and approved."
M00 commit `0f5d05b` was reliably observed in local Git history. The owner later
confirmed the commit/push workflow and authorized M01.

Automated verification on 2026-10-16:

| Exact command | Outcome | Local log |
|---|---|---|
| `make clean && make all && make test` | PASS; clean GCC/cc build, seven tests | Initial terminal output; subsequent run below retained |
| `make all test` | PASS; rebuilt after Makefile change, seven tests | `build/logs/m00-gcc.log` |
| `make CC=clang BUILD_DIR=build/clang test` | PASS; strict Clang build, seven tests | `build/logs/m00-clang.log` |
| `make sanitize` (sandbox) | FAILED; LeakSanitizer aborts under ptrace | `build/logs/m00-sanitize.log` |
| `make sanitize` (approved outside sandbox) | PASS; final seven tests, ASan/UBSan with leak detection enabled | `build/logs/m00-sanitize-unsandboxed.log` |
| `make analyze` (initial) | FAILED; machine enabled no checks by default | `build/logs/m00-analyze.log` |
| `make analyze` (explicit analyzer/bugprone checks) | PASS; no project diagnostics; one system-header warning suppressed by clang-tidy | `build/logs/m00-analyze-fixed.log` |
| `git diff --check` and `git diff --no-index --check /dev/null <file>` for each new M00 file | PASS; no whitespace errors | Terminal output |

Direct executable demonstration also verified bare invocation, `--version`,
`--help` (all exit 0) and `--unknown` (exit 1), with expected readable output.

The analysis target now selects checks explicitly. Sanitizer instrumentation
remains enabled; no leak-check suppression was used. The sandbox limitation
and reproduction instructions are documented in `docs/testing.md`.

Coverage: NFR-01/09/12 foundations, with basic CLI diagnostics and PTY attribute
checks supporting NFR-03/08. No FR is complete. Debugger, mouse/tmux, hardware,
performance, process lifecycle and release-wide safety remain unverified.

Environment inspected: Arch Linux x86-64; GCC 16.2.1; Clang 23.1.1;
GNU Make 4.4.1; Python 3.14.8. Planning also found ncursesw, native/ARM GDB,
OpenOCD, ARM GCC, gdbserver and tmux installed. Both GDBs answered MI3 feature
queries. OpenOCD reports a development/dirty build. USB enumeration failed;
NUCLEO-F446RE presence/access and all target behavior are **UNVERIFIED**.

A Conventional Commit message was supplied after acceptance, and the resulting
M00 commit was observed locally. The owner subsequently completed the workflow
and authorized the next milestone.

## UI specification correction — approved

Re-read the updated AGENTS.md and the full approved HTML, now located at
`docs/design/prototype.html` after an owner-authorized move from the root.
The move preserved its contents; the ignore rule now permits this design file.
Recorded the revised plan in `docs/plan.md` and aligned architecture/testing
notes. Removed predefined-layout work from the plan: one six-pane default,
resizable tiled panes, tabs/view switching and dynamic splits remain. Optional
restoration refers only to the user's own geometry. All milestone boundaries,
backend requirements and v0.1.0 release gates remain intact.

This documentation correction was committed by the owner as `86820c4` and
followed by explicit authorization for M01. Existing M00 evidence is unchanged.

## M01 — demo launcher and workspace

Authorized by the owner's "Ok, done. Now for the next." State history:
`planned` → `implemented` → `automated-verified` → `awaiting-user`.
The initial handoff awaited manual acceptance; the corrected presentation was
subsequently accepted by the owner (see the acceptance entry below).

Implemented the ncursesw launcher, editable sample configuration/review pages,
Monokai six-pane workspace, toolbar, tabs, pane-local view selector, row/gutter
selection, scroll and simulated run/step/reset actions. Every sample view and
session is labeled DEMO; future views say NOT IMPLEMENTED. Reset confirmation
applies only to the sample. No target processes/tools are launched and no input
is executed or persisted. Full-page forms and pane-local menus keep the workspace
tiled. Color fallback and compact/tiny rendering preserve view state.

Per owner instruction, replaced the Python CLI tests with C and wrote all new
tests in C. Build remains Make with ncursesw as the sole current application
library. Recorded **AT&T syntax for x86-64 disassembly** in `docs/plan.md` for M04.

Automated evidence on 2026-10-16:

| Exact command/check | Outcome | Local evidence |
|---|---|---|
| `make BUILD_DIR=build/m01-clean test` | PASS; fresh strict GCC build, C unit/CLI/PTY suites | `build/m01-clean-build-test.log` |
| `make test` | PASS; default build and all C suites | `build/m01-test-final.log` |
| `make CC=clang BUILD_DIR=build/clang test` | PASS; strict Clang build and all C suites | `build/m01-clang-test.log` |
| `make sanitize` (approved outside sandbox) | PASS; ASan/UBSan/leak detection, all C suites | `build/m01-sanitize.log` |
| `make analyze` | PASS; no unsuppressed project diagnostics | `build/m01-analyze-final.log` |
| `git diff --check` | PASS; no whitespace errors | Terminal output |
| Isolated `tmux -L <unique-name> -f /dev/null new-session -d -s demo -x 140 -y 44 <binary>`, opt-in mouse on, `send-keys` startup/F5/F10, `capture-pane -p` | PASS; launcher, six panes, running state and stepped sample PC; test server cleaned up | `build/m01-tmux-{launcher,workspace,running,step}.txt` |

The PTY suite exercises all three startup previews, keyboard-only tabs/view
selection, mouse forms/buttons, retained text, simulated toolbar/key actions,
reset cancel/confirm, placeholders, compact/tiny resize and restoration after
normal exit or SIGINT/SIGTERM/SIGHUP. Tests cover xterm-direct truecolor,
xterm-256color, vt100 monochrome and NO_COLOR; the isolated tmux run used
monochrome in its inherited environment. Unit geometry checks cover 76,800
screen/focus combinations. This is automated UI evidence, not real debugging.

During implementation, strict GCC found a potentially truncated status buffer;
the buffer/format were bounded. PTY tests caught Ctrl+Q being swallowed by
XON/XOFF; raw input now delivers it and Ctrl+C is explicitly handled. Test-reader
character deletion and fragmented-refresh handling were corrected, and mouse
events are requested explicitly. The initial analyzer rejected ordinary bounded
C calls in favor of optional Annex K `_s` APIs unavailable in Linux libc; that
single recommendation check is excluded and documented. Other analyzer/security
checks remain enabled. A curses-coordinate warning has a local explanatory
suppression. Final analyzer output also reports suppressed system-header warnings.

The sandbox blocks tmux sockets, so the tmux check required approved execution
outside it. Sanitizers likewise run outside ptrace tracing with leak detection
enabled. No environment failure is counted as a passing check.

Coverage: FR-01 UI automation PASS/manual PENDING; FR-13/17 partial UI scaffold;
NFR-01/03/05/08/09/10/11/12 foundational evidence. No live backend, complete layout,
performance benchmark, hardware or release-wide acceptance is claimed.

Manual acceptance: follow `docs/testing.md` on the owner's terminal and tmux,
including physical mouse/wheel, function-key fallbacks, UTF-8 editing, visual
comparison and clean shell restoration. These remain unverified by the owner.
Dragging, dynamic splits, closing tabs/panes and the persistent layout tree are
M02; tool validation, safe live console and real target inspection are later gates.
Persistent key remapping and profiles remain M06. No preset selector exists.
The README's final revision describes the project, current demo capabilities,
build/test commands, terminal use and limitations; collaboration agreements stay
in these development documents.

Stop here. After owner acceptance, provide a Conventional Commit message with a
title of at most 50 characters and a detailed body. Wait for owner commit/push
and explicit authorization before M02.

### M01 visual correction — owner review rejected initial presentation

The owner explicitly did not accept the initial visual result. At that review,
M01 remained `awaiting-user`. The HTML is the component-level visual reference, not merely
an arrangement diagram. See `docs/design/tui-mapping.md` for the comparison,
implemented corrections and terminal limitations.

Replaced ASCII boxes and uniform panel styling with Monokai surfaces, compact
header actions, styled tabs, contextual footers, semantic source/table colors,
full-row selection and bounded startup forms. Preserved the six-pane workspace,
existing interactions and M01 demo-only boundary. No presets or M02 features
were introduced. Added C renderer checks for palette values, semantic colors,
selection backgrounds and monochrome active-tab markers.

Verification results for this correction are recorded below after execution.
Manual visual acceptance, physical mouse/key behavior and appearance in the
owner's terminal remain pending. No commit or milestone advancement authorized.

Correction checks (all C application/test code):

- `make test`: PASS (demo model, renderer cells/palette, CLI, PTY flows,
  compact toolbar position, tabs, controls, resize, color fallback, cleanup).
- `make CC=clang BUILD_DIR=build/clang test`: PASS.
- `make sanitize`: PASS outside the sandbox, ASan/UBSan/LeakSanitizer. The
  initial sandbox run failed because LeakSanitizer cannot run under ptrace;
  it was rerun with the required execution permission, not disabled.
- `make analyze`: PASS after correcting argument-order warnings. Existing
  system-header warnings and explicitly annotated curses y/x conventions
  remain suppressed. A signedness warning in the new test was fixed before
  the successful compiler/test runs.
- Isolated tmux server, mouse enabled, 140x44, `tmux-direct`: launcher,
  workspace, F10 and clean quit checked; stepped PC was `0x0800042C`.
- `git diff --check`: PASS.

Logs: `build/m01-visual-{test,clang,sanitize,analyze}.log`. Captures:
`build/m01-visual-{launcher,workspace,step}.txt` and workspace `.ansi`.
The `.svg`/`.png` workspace previews were reconstructed from the captured ANSI
cells for agent inspection; they are not screenshots of the owner's terminal
or substitutes for manual approval. All artifacts are local/ignored.

Correction handoff status was **AWAITING USER ACCEPTANCE**, using the corrected
visual review checklist in `docs/testing.md`.

### M01 acceptance

The owner explicitly approved the corrected M01 implementation with
"Approved." M01 is now `accepted`. Automated evidence above remains unchanged.
The owner committed/pushed as `60df6550d0434b2c32977ec8179f5eb4ca92f55b`,
reliably observed at both HEAD and origin/main with a clean working tree.
The owner then authorized M02.

## M02 — tiled workspace engine

State history: `planned` → `implemented`; automated verification and manual
handoff are recorded below. Authorization followed the accepted/pushed M01 gate.

Implemented:

- C-only layout module independent of ncurses and debugger data; six root tiles
  preserve approved default proportions, with nested binary split trees.
- Shared column/row gutters and internal dividers resize by held-button drag;
  minimum sizes apply recursively. Keyboard resize selects the same dividers.
- Pane-menu actions: split right/below, close selected tab, close nested pane,
  resize dividers. Original pane state survives splitting; sibling expands on
  close. Final tabs and unsplit root tiles cannot close, matching the HTML.
- Stable pane IDs, focus, view selection and per-view scroll/selection; compact
  Prev/Next and `[`/`]` reach all retained panes. Terminal shrinking preserves
  the tree and ratios; resizing mid-drag cancels obsolete coordinates.
- Explicit xterm-compatible held-button motion mode 1002 and cleanup. The
  installed ncurses accepts the motion mask but enables only click mode through
  its XM capability; an output assertion caught this before handoff.
- Fixed pool of 32 pane slots / 64 nodes, reused after closing. Tiled leaves
  require at least 20x7 cells; splits that cannot fit report a recoverable error.
  No presets, backend, hardware access or persisted layout were added.

Test additions: pure tree/geometry invariants (76,800 size/focus combinations,
12,000 deterministic operations, capacity/reuse), C PTY workspace interactions,
and optional C real-tmux forwarding tests. The test VT reader now handles line
insertion/deletion and scroll regions emitted by ncurses during horizontal
resizing. This corrected stale test-screen coordinates; it was not counted as
an application pass until the actual terminal operations were interpreted.

Environment: Linux 7.2.9-arch1-1 x86-64, ncursesw 6.6.20251230, tmux 3.8.

Automated verification (all final commands exited 0):

| Command | Result | Evidence |
|---|---|---|
| `make test` | PASS | `build/m02-test.log` |
| `make CC=clang BUILD_DIR=build/m02-clang test` | PASS | `build/m02-clang.log` |
| `make sanitize` | PASS, ASan/UBSan/LeakSanitizer | `build/m02-sanitize.log` |
| `make analyze` | PASS | `build/m02-analyze.log` |
| `make test-tmux` | PASS, real isolated tmux input forwarding | `build/m02-tmux.log`, `build/m02-tmux-workspace.txt` |
| `git diff --check` | PASS | No whitespace diagnostics |

The first sanitizer attempt failed under the sandbox's ptrace restriction;
rerunning outside the sandbox passed without disabling leak checks. The first
tmux attempt failed because the sandbox denied its local socket; the isolated
server test passed with execution permission. Intermediate test failures
included assumptions in capacity/closing tests and missing scroll operations
in the VT reader; these were corrected and all final suites rerun. The motion
mode enable/disable assertion and signal cleanup checks passed.

Requirement coverage: FR-13/17 layout/input paths and NFR-01/03/05/09/10/11/12
have automated evidence within this demo milestone. Full usability, physical
mouse behavior and visual fidelity require owner acceptance. No live debugging
or hardware requirement is claimed. There are no known automated test failures.

Manual test now: follow the eight-step M02 checklist in `docs/testing.md`,
especially seventh/eighth views, both divider axes, close tab/split, keyboard
resize, shrink/restore and tmux. Compare the default presentation with the
accepted M01 styling and HTML; all debugger values remain visibly simulated.

State: `implemented` → `automated-verified` → `awaiting-user` → `accepted`.
The owner explicitly approved M02 after manual validation. Its commit
`094fa714da9400ae7a40c0357a1552ba38a1ffd7` was subsequently observed at both HEAD
and origin/main. The owner requested a
break afterward. M03 remains planned until the owner returns and authorizes it.
The newly added image is outside this milestone and was left untouched.
No commit, push or tag was made by the agent.

## Post-M02 navigation corrections

Authorized by the owner's reports of the four-line disassembly limit and
secondary source navigation highlight, followed by a request for scrollbars.
M03 remains planned. The user's image remains untouched.

The disassembly renderer had nine rows while input handling fell back to a
four-row bound. Rendering/input now share per-view counts, including shorter
Call Stack/Threads and empty Console states. The initial regression test failed
against the pre-fix input code and passed against the corrected implementation
(`build/disassembly-selection-before.log`). The VT test reader was extended
for ncurses cursor save/restore around scrolling regions.

The initial line-30 highlight represents the simulated execution position.
Removed the additional navigation-row highlight and the preloaded breakpoint.
Wheel/arrows/Page keys now move the viewport directly; only source/disassembly
execution rows receive a full-row background. Overflowing panes show a
clickable track and draggable thumb. Rendering and input share scrollbar
geometry; resizing ends a scrollbar drag safely.

Manual validation: follow the post-M02 checklist in `docs/testing.md`.
The original handoff status was **AWAITING USER ACCEPTANCE**. The owner later
reported "nevermind, it worked now" and committed the correction as `a832eef`,
reliably observed in local history. These corrections are recorded as accepted;
M02's separate acceptance/commit remain intact. No agent commit/push/tag occurred.

Final correction verification: `make test`, `make sanitize`, `make analyze`,
`make test-tmux` and `git diff --check` all passed. Sanitizer/tmux runs used the
required execution permission; leak detection stayed enabled. Logs are
`build/navigation-fixes-{test,sanitize,analyze,tmux}.log`. C regressions cover
ninth-instruction access, scrollbar paging/drag, execution-only highlighting
and empty initial breakpoints. No running the-bugger process remained after
checks. The subsequent owner confirmation/commit above resolves that pending
correction handoff; it does not accept P00 or authorize M03.

## P00 — expanded roadmap and tab-focus correction

Authorized by "Implement the plan" after the roadmap review and exit from Plan
mode. The subsequent owner instruction adds an every-release regression gate:
native x86-64, real STM32/ARM, ESP32/Xtensa and ESP32/RISC-V, with QEMU ARM/RISC-V
as supplementary coverage. The owner explicitly said not to collect exact board
or probe details now. Hardware configuration/evidence will be recorded when run.

State history: `planned` → `implemented` → `automated-verified` → `awaiting-user`.
This is documentation and the approved
narrow UI correction only; M03–M18 and F01–F47 remain planned.

Implemented the approved versioned roadmap through v1.0.0, preserved accepted
M00–M02, updated the local AGENTS.md specification and tracked architecture/test
documentation, reconciled navigation-correction acceptance and removed the
untracked supplied reference JPG. Research inputs/section references and all
deferred decisions live in docs/plan.md, not a separate research report.
AGENTS.md remains ignored by existing repository policy; the tracked roadmap
contains the complete actionable gates and release matrix.

Tab activation previously changed active-first paint order while retaining the
clicked control's old numeric focus index. Inactive Disassembly then inherited
inverse styling, and Enter could activate the wrong tab. Activation now rebinds
focus to the promoted tab; mouse input suppresses extra keyboard inverse styling,
and keyboard input restores visible focus. The active theme and monochrome
selection marker are preserved. No backend, new layout mode, theme selector or
deferred debugger feature was implemented.

The C PTY regression failed against the pre-fix app at the inactive tab's inverse
attribute (`build/p00-tab-before.log`). It now checks actual inverse attributes,
promotion, repeated switching, Enter and keyboard focus in direct/256-color and
monochrome. Extended color components are excluded from SGR inverse decoding.
The first full run caught an insufficient refresh settle in the new test; using
the existing bounded refresh-settling helper corrected the test, then it passed.
The older keyboard test was updated to follow the correctly promoted tab rather
than rely on the previous stale index. The real tmux test also covers the fix.

Verification (all final commands exited 0):

| Exact command | Result | Local evidence |
|---|---|---|
| `make test` | PASS; strict GCC and all default C suites | `build/p00-test.log` |
| `make CC=clang BUILD_DIR=build/p00-clang test` | PASS; separate strict Clang build and suites | `build/p00-clang.log` |
| `make sanitize` | PASS; ASan/UBSan/LeakSanitizer, outside ptrace sandbox | `build/p00-sanitize.log` |
| `make analyze` | PASS; no unsuppressed project diagnostics; existing system-header/NOLINT suppressions reported | `build/p00-analyze.log` |
| `make test-tmux` | PASS; real isolated tmux, including tab styling/focus | `build/p00-tmux.log`, `build/m02-tmux-workspace.txt` |
| `git diff --check` | PASS before final README update; repeated at final handoff | No whitespace diagnostics |

The initial sandbox sanitizer run failed because LeakSanitizer cannot run under
ptrace; the tmux run failed because the sandbox denied its local socket. These
were rerun with execution permission and passed, without disabling leak checks
or using/modifying the owner's tmux server. Failed environment attempts remain
in `build/p00-{sanitize,tmux}-sandbox.log` and are not counted as passes.
An additional owner README-title commit, `2239e5b`, was observed during P00;
the final README edit preserves that title. No Git write was performed by the
agent. The supplied JPG was untracked, so removing it produces no Git deletion.

Manual test now: follow **P00 tab correction** in docs/testing.md. Inspect the
rendered tabs repeatedly without clicking away, then keyboard focus and Enter,
in the terminal/tmux and monochrome. Review the roadmap/release matrix and
current-demo README. Automated tests do not accept visual appearance.

Risks/limits: no live backend was implemented or tested; physical board runs,
QEMU runs and all release readiness remain UNVERIFIED. AGENTS.md is updated
locally but remains ignored by existing policy; full tracked requirements and
gates are in docs/plan.md. P00 has no resulting commit SHA yet.

**Status: AWAITING USER ACCEPTANCE.** Stop before M03. After explicit acceptance,
provide only the requested Conventional Commit message (title ≤50 characters,
detailed body), then wait for owner commit/push and explicit continuation.

### P00 acceptance and M03 authorization

The owner requested the commit message, completed the commit/push and explicitly
authorized the next gate. Commit `36d5ad6` is reliably observed at HEAD and
origin/main with the P00 tab fix and documentation. P00 is accepted; its earlier
handoff status above is historical. No Git writes were performed by the agent.

The owner subsequently changed the delivery workflow: always include a proposed
Conventional Commit message with the manual-review handoff (title ≤50 characters
and detailed body). Revise it after any fixes. Explicit acceptance, owner
commit/push and authorization still gate the next milestone.

## M03 — incremental owned MI parser

State: `planned` → `implemented` → `automated-verified` → `awaiting-user`.
M03 only was authorized. No acceptance or resulting commit SHA is claimed.

Implemented:

- Independent C17 parser and owned ordered trees in `src/gdb/mi.{h,c}`:
  result/async/stream/prompt records, exact token text, future class/field names,
  nested tuples/lists, duplicate fields, decoded byte strings with explicit
  lengths, and quoted/raw stream handling.
- Incremental CR/LF/CRLF framing across arbitrary chunks, deterministic
  diagnostics and recovery after malformed/overlong lines, truncated EOF
  reporting and fatal allocation-error handling with safe destroy/reset.
- Explicit limits: 1 MiB encoded record, 16,384 values and 64 nested containers.
  No new library dependency, global parser state or production allocation hooks.
- C semantic/corpus/bounds/mutation tests and linker-wrapped allocation faults;
  standalone `mi-dump` demonstration and synthetic fixture provenance.
- Architecture/testing/roadmap/changelog/README updates and the delivery-message
  workflow. AGENTS.md was updated locally but remains ignored by existing policy;
  the tracked gate contract is in docs/plan.md.

Excluded: subprocess transport/controller, token correlation, run-state model,
command encoding, live TUI integration, real session launch/attach and all hardware
access. The existing workspace remains DEMO. M04 and later gates remain planned.

Automated verification (commands exited 0 unless an expected decoder exit is
specified):

| Exact command | Result | Local evidence |
|---|---|---|
| `make test` | PASS; strict GCC, both MI suites and all existing suites | `build/m03-test.log` |
| `make CC=clang BUILD_DIR=build/m03-clang test` | PASS; separate strict Clang build and all suites | `build/m03-clang.log` |
| `make sanitize` | PASS; ASan/UBSan/LeakSanitizer outside ptrace sandbox | `build/m03-sanitize.log` |
| `make analyze` | PASS; no unsuppressed project diagnostics; existing system-header/NOLINT suppressions reported | `build/m03-analyze.log` |
| `make test-mi` | PASS after final test assertion/fixture edits | `build/m03-mi-final.log` |
| `make CC=clang BUILD_DIR=build/m03-clang test-mi` | PASS after those edits | `build/m03-clang-mi-final.log` |
| `make BUILD_DIR=build/sanitize CFLAGS='-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer' LDFLAGS='-fsanitize=address,undefined' test-mi` | PASS after those edits, leak detection enabled outside ptrace sandbox | `build/m03-sanitize-mi-final.log` |
| `make mi-demo` | PASS; 22 decoded records, exit 0 | `build/m03-demo.log` |
| `./build/mi-dump --chunk 1 tests/fixtures/mi/invalid.mi` | Expected exit 1; six diagnostics then two recovered records | `build/m03-invalid.{out,err}` |
| `printf '7^done' \| ./build/mi-dump` | Expected exit 1, truncated EOF | `build/m03-truncated.{out,err}` |
| `printf '7^done\n' \| ./build/mi-dump` | PASS, exit 0 | `build/m03-terminated.out` |
| `./build/mi-dump --chunk 0` | Expected usage exit 2 | `build/m03-usage.err` |
| `./build/mi-dump /nonexistent/the-bugger-m03.mi` | Expected I/O exit 2 | `build/m03-missing.err` |
| `git diff --check` | PASS, repeated at final handoff | No whitespace diagnostics |

The MI tests assert semantics and independent ownership, replay every two-chunk
corpus split and chunk sizes 1–71, exercise all limits, mutate every seed byte
through all 256 possibilities, and walk representative malloc/calloc/realloc
failure points including growing an already allocated buffer. No automated
failure remains. Full suites passed before the final test-only CHECK conversion
and raw-stream fixture wording cleanup; focused suites then passed again under
GCC, Clang and sanitizers. Production code did not change between these runs.

Actual-output compatibility probe (native GDB and arm-none-eabi-gdb, both 18.1):
created `build/m03-probe.commands` using
`printf '1-list-features\n2-interpreter-exec console "echo parser-check\\n"\n3-not-a-command\n4-gdb-exit\n'`.
For each executable and each `mi3`/`mi2` interpreter, ran
`<gdb> --quiet --nx --interpreter=<mi> < build/m03-probe.commands` and decoded
the output with `./build/mi-dump --chunk 1 <capture>`. All four invocations and
decodes exited 0, including intentional GDB error responses. Captures, stderr
and decoded trees are `build/m03-{gdb,arm-none-eabi-gdb}-{mi3,mi2}.{mi,stderr,decoded}`.
No inferior or target was started; these are parser grammar compatibility checks,
not live-debugger or hardware acceptance. The tracked corpus remains synthetic.

Requirement evidence: NFR-04 parser behavior and NFR-01/03/09/12 foundations;
FR-16 parser error handling only. Physical mouse/tmux behavior was not newly
validated in this parser-only gate; existing PTY suites passed. Hardware and
release readiness remain UNVERIFIED. No OpenOCD/device configuration was needed.

Manual test now: follow **M03 manual acceptance** in docs/testing.md. In
particular inspect the valid decoded tree, malformed-record recovery and EOF/
usage exit codes. No visible TUI change is expected.

**Status: AWAITING USER ACCEPTANCE.** Include the proposed commit message at
delivery. Stop before M04; wait for owner review, acceptance, commit/push and
explicit continuation.

### M03 acceptance and M04 authorization

The owner's explicit instruction to proceed to the next milestone accepts M03
and authorizes M04. Commit `ee1cfc5` is reliably observed at HEAD and origin/main,
with a clean working tree before M04. The earlier M03 awaiting-user status is
historical. No commit, push, tag or other Git write was made by the agent.

## M04 — nonblocking MI controller and owned lifecycle

State: `planned` → `implemented` → `automated-verified` → `awaiting-user`.
M04 only was authorized. No acceptance or resulting commit SHA is claimed.

Implemented:

- Internal C controller with owned argv-based GDB spawning, isolated process
  group, close-on-exec/nonblocking parent pipes and separate stderr. No shell,
  global signal handler, new library dependency or target command on startup.
- MI3 prompt and asynchronous/all-stop setup negotiation; one restricted MI2
  retry for the explicit unsupported-interpreter startup diagnostic, after the
  first child is reaped. No fallback after a prompt, generic failure or timeout.
- Bounded queued writes and per-pipe processing, exact-token correlation,
  independent caller cookies, deadlines from enqueue and one completion per
  accepted public request. Safe MI argument encoding and ordered partial writes.
- Session/stop/context generations, queued/in-flight stale rejection, async
  execution-state foundations and protection against late/repeated running
  responses. STOP/CONTEXT scopes require a stopped target.
- Poll integration hooks, EINTR returns and idle waiting; explicit handling of
  closed command input, EOF/protocol errors, backpressure and output floods.
- Nonblocking close, cooperative EOF, bounded TERM/KILL escalation, owned-PID
  reaping and cancellation. Destroy refuses a live controller. External reaping
  is diagnosed and clears ownership; unrelated children remain untouched.
- C fake peer, lifecycle/pressure tests, interactive diagnostic harness and PTY
  regression. Optional actual-GDB startup/features/MI-exit probe without targets.
- Architecture, testing, progress, roadmap, changelog and README updates.
  AGENTS.md is updated locally and remains ignored by existing policy; the
  tracked contract is in docs/plan.md.

Excluded: native target launch/attach, inferior TTY/output/session model, live
workspace wiring or inspection, source/execution controls in the product, saved
profiles, OpenOCD management and hardware access. The application remains DEMO.
The context-generation hook is infrastructure for M06, not implemented frames.

Automated verification (all final commands exited 0):

| Exact command | Result | Local evidence |
|---|---|---|
| `make test` | PASS; strict GCC, ten C suites including existing TUI regressions | `build/m04-test-final.log` |
| `make CC=clang BUILD_DIR=build/m04-clang test` | PASS; separate Clang build and all suites | `build/m04-clang-final.log` |
| `make sanitize` | PASS; ASan/UBSan/LeakSanitizer outside ptrace sandbox | `build/m04-sanitize-final.log` |
| `make analyze` | PASS; no unsuppressed project diagnostics; existing system-header/NOLINT suppressions reported | `build/m04-analyze-final.log` |
| `make test-controller` | PASS after the final harness-only stderr preview cap | `build/m04-controller-delivery.log` |
| `make CC=clang BUILD_DIR=build/m04-clang test-controller` | PASS after that cap | `build/m04-clang-controller-delivery.log` |
| `make BUILD_DIR=build/sanitize CFLAGS='-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer' LDFLAGS='-fsanitize=address,undefined' test-controller` | PASS after that cap, leak detection outside ptrace sandbox | `build/m04-sanitize-controller-delivery.log` |
| `make test-gdb-startup` | PASS; actual native GDB 18.1 startup/setup/features/MI exit, no target | `build/m04-gdb-startup-final.log` |
| `./build/test-gdb-startup arm-none-eabi-gdb` | PASS; actual ARM GDB 18.1, same target-free probe | `build/m04-arm-gdb-startup-final.log` |
| `git diff --check` | PASS; repeated at final handoff | No whitespace diagnostics |

The deterministic tests exercise reordered/late replies, escaped byte arguments,
queued/context/stop invalidation, queue limits, timeouts, partial-write shutdown,
stdout/stderr floods, idle poll, malformed/truncated input, setup/startup/fallback
failures, signals, ECHILD, partial descriptor exhaustion, closed stdio, literal
paths with spaces/metacharacters, repeated cleanup and unrelated child survival.
PTY tests exercise commands, timeout/stale reporting, responsiveness through
floods and SIGINT/SIGTERM/SIGHUP cleanup with unchanged terminal attributes.

Measured method/environment: test_controller times every pump in its delayed-read
and 2 MiB stdout + 2 MiB stderr scenarios using CLOCK_MONOTONIC millisecond wall
time, while also asserting per-turn byte bounds and actual idle waiting.
Final GCC/Clang/sanitizer full suites each observed a maximum pump of **11 ms**.
Host: Linux 7.2.9-arch1-1 x86-64, Intel Core i5-8265U, 8 logical CPUs;
GCC 16.2.1 and Clang 23.1.1. These are fake-peer transport measurements,
not whole-application latency, hardware performance or release acceptance.

Intermediate failures were inspected and corrected: sandbox socket sends were
denied, so command input uses a POSIX pipe with local SIGPIPE protection; an
idle timing assertion initially measured the immediate queued-write turn;
closed-input handling initially prevented legitimate pre-prompt fallback; and
clang-tidy required an explicit POSIX bitmask cast and a narrowly documented
Linux feature-macro annotation. Regression tests now cover the affected behavior.
Earlier logs remain under `build/m04-*`; final full suites passed after the
production fixes. Only the harness stderr display cap changed afterward, and
the focused GCC/Clang/sanitizer controller suites were repeated.

Requirement evidence: FR-16 lifecycle foundations and NFR-02/03/04/06/08/09/12
within this controller gate. No external server was contacted or managed.
The optional GDB tests establish actual transport startup, not native debugging,
ARM firmware debugging or hardware support. Hardware/release readiness remains
UNVERIFIED. No mouse/visual behavior changed; tmux was not newly tested in this
backend-only gate, and existing workspace PTY regressions passed.

Manual test now: follow **M04 manual acceptance** in docs/testing.md. Use the
explicit fake harness for run/stop, stale/timeout/error behavior, flood input
and clean shutdown; inspect fallback/stubborn startup errors. The product TUI
still displays DEMO and is not expected to connect to a live target yet.

**Status: AWAITING USER ACCEPTANCE.** Deliver the proposed Conventional Commit
message now and stop. M05 requires owner acceptance, commit/push and explicit
authorization.
