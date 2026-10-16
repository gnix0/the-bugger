# Milestone ledger

Target: **v0.1.0**. No release has been accepted.

States: `planned` → `implemented` → `automated-verified` → `awaiting-user` →
`accepted`. Only explicit owner acceptance permits the last transition.

| Milestone | Deliverable | State | Commit SHA |
|---|---|---|---|
| M00 | Repository foundation | accepted | `0f5d05b` |
| M01 | Launcher and DEMO workspace | accepted | `60df655` |
| M02 | Tiled layout engine | accepted | `094fa71` |
| M03 | MI and owned-process lifecycle | planned | — |
| M04 | Native Linux debugging | planned | — |
| M05 | Live inspection | planned | — |
| M06 | OpenOCD, remote sessions and profiles | planned | — |
| M07 | Integration and real-hardware release acceptance | planned | — |

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
Status: **AWAITING USER ACCEPTANCE** for these corrections. M02 acceptance and
its existing commit are retained; no new commit/push/tag was made by the agent.

Final correction verification: `make test`, `make sanitize`, `make analyze`,
`make test-tmux` and `git diff --check` all passed. Sanitizer/tmux runs used the
required execution permission; leak detection stayed enabled. Logs are
`build/navigation-fixes-{test,sanitize,analyze,tmux}.log`. C regressions cover
ninth-instruction access, scrollbar paging/drag, execution-only highlighting
and empty initial breakpoints. No running the-bugger process remained after
checks. Manual visual acceptance is pending.
