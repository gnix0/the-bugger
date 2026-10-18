# Milestone ledger

Target: **v0.1.0**. No release has been accepted.

M11 accepted (2026-10-18). The owner confirmed acceptance and commit/push, then
authorized this separate documentation handoff. Observed implementation:
`77446434d8e6e1e1a356abaa4d94e028a1098036`
(`feat(inspect): add scoped locals and watches`). The owner specifically checked
that `variables_ready(0)` is rejected as a call; other manual steps were not
individually reported and are not invented here. M10 remains accepted at
`2f951fb8dbaa10f490e5abd6eba31d1ced1dc6af`.
The product is embedded-first; Linux validation through M14 is preserved, and
all physical embedded coverage remains UNVERIFIED.

## M11 completion verification

Read-only Locals/Watches now expose selected-context values/types, numeric
formats, change indicators, lazy aggregate/pointer/C++ expansion, bounded array
pages and add/remove actions. Value-producing requests are authorized before
dispatch. Unknown storage requires deliberate inspection; the native adapter
does not claim complete expression-footprint proof for automatic values.
See [architecture](architecture.md#m11-scoped-locals-and-watches) and
[acceptance procedures](testing.md#m11-localswatches-acceptance).

The owner explicitly authorized agent-run tests for this correction. This is a
scoped exception, not permission to run future lengthy suites autonomously.

| Agent-run command | Result and evidence |
|---|---|
| `make test-m11-model test-m11-native` | PASS after the final initialization fix; `build/m11-final.log`. C peer policy/bounds/stale/cleanup cases, actual GDB C/C++/optimized fixtures and live TUI actions. Strict compiler warnings enabled. |
| `make test test-native` | PASS; `build/m11-regression.log`. Full existing unit/PTY/native regressions, before the final variable-type buffer initialization correction; focused tests were repeated afterward. |
| `env ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 make -s -j4 BUILD_DIR=build/m11-sanitize CC=clang CFLAGS='-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer' LDFLAGS='-fsanitize=address,undefined' test-m11-model test-m11-native` | PASS after final correction; `build/m11-sanitize.log`. Focused ASan/UBSan/leak checks, not a full sanitizer matrix. |
| `make test-tmux` | PASS; `build/m11-tmux.log`. Existing isolated-server forwarding/layout/input regression, not the complete live M11 workflow under tmux. |
| `make analyze` | FAIL; `build/m11-analyze.log`. Found M11 type-buffer initialization and style diagnostics, subsequently fixed; existing findings remain in Memory, native mapping parsing and older TUI register/Memory row calculations. Not counted as PASS. |
| `clang-tidy src/model/variables.c --checks='-*,clang-analyzer-*,bugprone-*,-clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling' --warnings-as-errors='*' -- -Iinclude -Isrc/model -Isrc/gdb -std=c17` | PASS after correction; `build/m11-variables-analyze.log`. Does not clear full-project analyzer findings. |
| `git diff --check` | PASS at implementation delivery; documentation checked separately at handoff. |

Earlier failed native assertions exposed change-baseline invalidation and C++
access-group assumptions; corrected comparisons and explicit group expansion
passed the final tests. TUI failures involved accidental demo startup, incomplete
redraw waits and raw-output capture exhaustion. The test now uses live argv,
elapsed deadlines and completed-operation checks; the shared PTY reader keeps a
bounded diagnostic tail and checks sanitizer evidence before eviction. No failed
attempt is counted as passing. Previous owner-run native API passes are retained
in the conversation; the table records the later agent-run final evidence.

Known limitations: explicit reads until complete safe-footprint proof exists;
no editing, specialized STL/pretty-printer display or live-while-running polling.
M14 owns the remaining analyzer/read-policy audit; M15 remote integration must
preserve pre-dispatch authorization. Board validation remains M17. No release
acceptance or M12 implementation follows this documentation handoff.

## M10 completion verification

One-time explicit owner authorization allowed the agent to execute the following
checks. It does not change the standing owner-run policy for lengthy tests.

| Agent-run command | Result and evidence |
|---|---|
| `make -s -j4 all build/test-memory build/fake-mi build/test-memory-native build/test-memory-tui` | PASS, strict GCC C17 build |
| `make test-m10-model test-m10-native` | PASS, `/tmp/the-bugger-m10-inline-focused.log`; inline Go/Enter, scalar expressions, hex/ASCII, bounded keyboard/wheel browsing, independent drafts, target-neutral region/byte-order cases and native partial reads |
| `make test test-native test-tmux` | PASS, `/tmp/the-bugger-m10-inline-regression.log`; existing native/parser/controller/context/code/break/register/layout/terminal and tmux forwarding regressions |
| `TB_MEMORY_TEST_TMUX=1 build/test-memory-tui "$PWD/build/the-bugger" "$PWD/build/memory-fixture"` | PASS, `/tmp/the-bugger-m10-inline-tmux.log`; full live Memory workflow in an isolated mouse-enabled tmux server |
| `ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 make -s -j4 BUILD_DIR=build/m10-sanitize CC=clang CFLAGS='-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer' LDFLAGS='-fsanitize=address,undefined' test-m10-model test-m10-native` | PASS, `/tmp/the-bugger-m10-inline-sanitize.log`; strict Clang build and focused ASan/UBSan/leak checks, without an unrelated full sanitizer matrix |
| `git diff --check` | PASS before handoff |

Earlier M10 failures were test assumptions: exact 32-byte boundary count (GDB
can report an additional byte), transient footer text, equal-width pane hit
regions/unsettled mouse events, and a source-only check searching all inspectors
for STALE. Corrections preserve the asserted data paths; no failed runtime result
was counted as PASS. The new correction adds the persistent inline viewer and
adapter boundary documented in architecture.md. Physical STM32/ESP32/QEMU runs
remain UNVERIFIED; no hardware support or release acceptance follows these checks.
Owner acceptance is recorded above against the observed implementation hash.
This documentation handoff remains a separate owner-created commit; no agent
commit/push was performed and no release acceptance follows milestone acceptance.

States: `planned` → `implemented` → `automated-verified` → `awaiting-user` →
`accepted`. Owner acceptance permits the last transition. Per the owner's later
instruction, simple positive feedback without issues also means acceptance and
next-gate authorization, unless specified otherwise; questions/instructions do
not. The owner handles commits/pushes; "Approved" authorizes direct continuation
without Git-state checks or repeated commit/push confirmation.

| Milestone | Deliverable | State | Commit SHA |
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
| Post-M08 | Reversible pane tabs and combined documentation handoff | accepted | `0023150` |
| M09 | CPU registers | accepted | Not supplied |
| M10 | Persistent inline Memory and embedded-readiness review | accepted | `2f951fb8dbaa10f490e5abd6eba31d1ced1dc6af` |
| M11 | Scoped read-only Locals/Watches | accepted | `77446434d8e6e1e1a356abaa4d94e028a1098036` |
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
new work. Historical failures and acceptance evidence are retained. M09 is
accepted; M10 and M11 are accepted at the implementation commits above. No complete
live workflow or release is accepted. Historical awaiting-user statements below
describe their handoff dates and are superseded by the acceptance entries.

Current workflow: deliver code/tests/build staging commands and a detailed
Conventional Commit message; the owner manually validates and commits/pushes.
For this handoff, finish M11 documentation and stop for the owner's documentation
commit; M12 remains planned. Record a SHA when the owner supplies it or it has
already been observed; do not invent one. Prepare documentation separately,
updating README last, and let the owner commit it. Agents run only quick checks;
lengthy suites/stress/sanitizer matrices are supplied as commands for the owner.

## M09 — accepted CPU-register inspection

The owner accepted M09 with "All set. Proceed to documentations update and
return with the commit message." M09's code SHA was not supplied or checked;
no commit identity or remote push is claimed. The earlier pane-tab correction
is accepted at the previously observed `0023150`. No Git writes were performed.

Implemented target-discovered names and numeric IDs, preserving unnamed-register
holes; selected thread/frame reads; Hex, Decimal, Octal, Binary, Raw and Natural
formats; Refresh; scrollbars and non-color `*` changed-value indicators. Reads
are bounded to visible rows in batches of 32, with 512 register IDs and 1024-byte
display values. Unavailable, bounded, stale and error states remain explicit.
Only structured MI data is used. No register writes, peripheral polling or
guessed architecture groups were introduced; current display is All (ungrouped).

Canonical hexadecimal samples keep format changes from creating false change
markers. Thread/frame/architecture changes reset comparison baselines; obsolete
context/format replies are rejected. Registers alone can request fresh selected
context without making Source, Call Stack or Threads visible. No reads are
repeated automatically while running, hidden or idle; failures wait for Refresh.

| Command / evidence source | Outcome |
|---|---|
| Agent: strict `make -j4 all` and focused register/native/TUI executable builds | PASS; `/tmp/the-bugger-m09-build.log` and terminal output |
| Agent: `build/test-registers "$PWD/build/fake-mi"` | PASS; synthetic x86/ARM names, sparse IDs, 600-name cap, long/unavailable values, formats, stale replies, frame/thread baselines, errors and bounded idle/running/hidden work |
| Agent: `git diff --check` before final code delivery | PASS |
| Owner: first `make test-m09-native` | FAILED at caller-frame selection: test assumed a caller above main |
| Owner: corrected `make test-m09-native`, reported "All set" | PASS reported; real-GDB and PTY focused acceptance |

The caller test now runs to code_callee before selecting its real main caller;
GDB's default backtrace boundary at main is preserved. Initial synthetic-frame
test setup was also corrected to request visible frame data before selection.
No runtime failure was reclassified as a passing check. Broader GCC/Clang suites,
sanitizer/static-analysis/tmux reruns and real-board results were not reported
for M09 and are not claimed PASS. Hardware remains UNVERIFIED.

FR-08/20 have scoped native register/format evidence and owner acceptance;
NFR-04/07/09 gain bounded, architecture-neutral model coverage. M10 Memory,
editing, SVD/peripherals, groups not supplied through structured MI and release
acceptance remain outside this gate. This documentation-only follow-up reruns
no tests and stops for the owner's documentation commit; it implements no M10.

## Post-M08 — reversible pane tabs and combined handoff

Owner-run `make test` failed in the new workspace PTY scenario: a text-only
click selected the Symbols header tab instead of its same-named menu option.
The test now selects within that pane's menu body. Failure diagnostics escape
and bound the raw terminal tail instead of replaying ANSI controls into the
owner's shell. The owner subsequently reported `make test` PASS after this
correction; the earlier failure remains recorded for traceability.

The owner authorized this narrow UI correction and the pending M08 documentation
in one commit, overriding the separate-docs rule for this handoff. M08 code
remains accepted at 55ecbd4; the new correction awaits manual acceptance.

The `...` list toggles membership only in that pane. Included views alone have
a right-aligned filled circle (ASCII star fallback). Default tabs start marked.
Adding activates the view; removing an inactive tab preserves selection, and
removing the active tab selects a survivor. The last tab is protected. Per-view
positions remain intact. Other panes are independent; no persistence is added.

Quick checks passed: `make -j4 all build/test-unit`, `build/test-unit` and
`git diff --check` before the final documentation edits. Unit cases cover
membership, active/inactive removal, scroll retention, invalid choices,
last-tab protection and pane independence. A workspace PTY case was added for
actual menu toggles and last-tab protection; it passed in the owner's `make test`.

Owner-run `make test-native`: PASS, supported by the supplied complete output
and returned shell prompt. All M05–M08 native API/TUI checks completed, including
breakpoints, source recovery, selector roundtrips, scrolling and stepping.
No native rerun is required without further relevant changes. The owner also
confirmed `make test` already passed after the menu-option correction. Both
required suites now have owner-run PASS evidence. Optional broader checks
are `make sanitize sanitize-native`, `make analyze` and `make test-tmux`; no new
passes for those are claimed. Manually verify selected-only dots, reversible Threads/default tabs and
independent menus. Stop for owner acceptance/commit/push before M09. No agent
Git writes or lengthy suites were performed for this correction.

## M08 — accepted code; documentation handoff

State history: `planned` → `implemented` → `automated-verified` →
`awaiting-user` → `accepted`. After the final disassembly corrections and manual
handoff, the owner reported "Done." Code commit
`55ecbd4f627d0374f81176290189ca5b4808ed5d` is observed locally. The local
origin/main tracking ref still pointed to `8d69475`; remote push is not claimed
as independently verified. No Git write was performed by the agent.

Delivered bounded libelf-based code/data metadata and native image mapping,
earlier/later instruction browsing, explicit raw decoding, labeled gaps and
honest unknown-range states. GDB remains the decoder. Live breakpoint management
includes gutter/F9, conditions, ignore counts, pending/multiple locations and
required hardware. Run to Selected owns temporary backend state separately,
preserves user entries and handles intervening stops/failures/timeouts. A same-
token GDB running/error sequence is reconciled using structured thread states.

Manual feedback corrected selector roundtrips replacing a broad cache with a
short section, warnings surviving outside the visible range, and backward scans
decoding the gap between .init and .plt. The fixture's five-byte gap is now
consistently labeled as bytes not decoded. Aligned address/byte/mnemonic/operand
columns retain complete text through horizontal scrolling. Five-row margins,
one-cell scrollbars, default pane proportions and the approved workspace remain.

Agent-run final code verification (before the owner's new lengthy-check rule):

| Exact command | Outcome | Local evidence |
|---|---|---|
| `make -j4 test test-native` | PASS | `build/m08-review-all.log` |
| `make -j4 CC=clang CXX=clang++ BUILD_DIR=build/m08-clang test test-native` | PASS | `build/m08-review-clang.log` |
| `make sanitize sanitize-native` alongside other builds | FAILED at the existing controller test's 35 ms deadline assertion, not a sanitizer runtime diagnostic | `build/m08-review-sanitize.log` |
| `make sanitize sanitize-native` isolated rerun | PASS, ASan/UBSan with leak checks | `build/m08-review-sanitize-isolated.log` |
| `make analyze` | PASS | `build/m08-review-analyze.log` |
| `make test-tmux` | PASS, isolated tmux input/layout regression | `build/m08-review-tmux.log` |
| `git diff --check` before code delivery | PASS | Terminal output |

Coverage includes 1,000 deterministic malformed/truncated ELF mutations,
ELF32/64 and both byte orders, ARM mapping symbols, 100 browsing sequences,
100 breakpoint lifecycle cycles, actual C++ multiple locations, pending shared-
library resolution, quoted/relocated source breakpoints and Run to Selected,
real native hardware-breakpoint exhaustion, delayed/racing cleanup and no retry
spin. C PTY cases repeat selector roundtrips at several positions and compare
the restored listing/viewport. No test was disabled to obtain a pass.

FR-11/23/24 have native automated/manual milestone evidence; FR-07/22/28 gain
browsing/source-path regressions. NFR-01/03/04/05/06/09/10/12 have scoped evidence.
No complete release or new performance measurement is claimed. ARM ELF corpus
checks are not board tests. STM32, ESP32/Xtensa, ESP32/RISC-V and supplementary
QEMU execution remain UNVERIFIED. M09–M18 and data watchpoints remain separate.

This documentation follow-up records the accepted implementation, dependency,
usage, tests and corrected collaboration workflow. No application code changed
and no suites were rerun for this documentation-only handoff. Documentation
commit/push remains with the owner; do not start M09 within this handoff.

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

### M04 acceptance and M05 authorization

The owner explicitly requested the next milestone after committing/pushing M04.
Commit `f29ce3c23e3d818c64cdee44ddd501c73251a5bc` is reliably observed at both
HEAD and origin/main, with a clean tree before M05. M04 is accepted; its earlier
awaiting-user status is historical. No Git write was performed by the agent.

## M05 — native sessions, output, console and Save Log

State: `planned` → `implemented` → `automated-verified` → `awaiting-user` → `accepted`.
The owner explicitly accepted M05 and authorized M06. Commit
`0278b612a8169253810c778607aa3d244a7b0407` is observed at HEAD and origin/main.

Implemented:

- Internal C native-session layer over the MI controller, with validated ELF/
  PID/cwd, copied structured arguments/environment and shell-free execution.
  ELF paths resolve before changing GDB cwd. Attach ignores hidden Launch
  settings; form inputs survive recoverable failures and mode switching.
- Bare invocation now starts real native configuration. Launch loads the ELF;
  Continue starts/resumes it, Pause interrupts it, and actual backend state/PID
  are displayed. Explicit --demo preserves the accepted visual reference and
  legacy visual/input regressions. Embedded/remote remain labelled previews.
- Live Console/GDB command input, output PTY isolation, bounded escaped logs,
  independently flushed partial streams, scrolling/following and Save Log to a
  new mode-0600 file. Existing files survive; open/short-write errors are visible.
- Full-page exit confirmation: terminate a frontend-launched inferior or
  detach/resume an attached process, then close/reap GDB. Signal exits use the
  same cleanup. Linux pidfds verify/recover owned inferior exit without stale
  numeric-PID signals. Basic console run/start commands also establish ownership.
- C native API and real-TUI PTY tests, C/C++ target fixtures, explicit native/
  native-sanitizer targets, documentation and README updates. C++ is only a
  deliberately debugged fixture; application/harness code remains C.

Prerequisite correction: real GDB revealed that the M04 encoder always quoted
-gdb-set arguments, allowing ^done without applying settings. It also quoted
native attach PIDs incompatibly with GDB's CLI-derived command handling.
The encoder now emits safe ASCII tokens normally and uses restricted names/
literal CR/LF-free tails for -gdb-set/-gdb-show. Actual probes query mi-async and
non-stop values, and native fixtures verify literal argv/env with the inferior
startup shell disabled. This fixes a prerequisite without rewriting M04 history.

Explicit exclusions: live Source/Disassembly/registers/memory/variables/frames/
threads, stepping/breakpoint UI, dedicated Restart/Reset, profiles, program input,
remote/OpenOCD workflows and hardware. Live inspection panes show unavailable
messages, never sample values. M06 and later gates remain planned.

Verification (final commands exited 0 unless explicitly labelled expected failure):

| Exact command | Result | Evidence |
|---|---|---|
| `make test test-native test-gdb-startup test-tmux` | PASS; GCC, deterministic suites, actual native sessions/TUI, actual startup-setting queries, isolated tmux --demo forwarding | `build/m05-gate.log` |
| `make CC=clang BUILD_DIR=build/m05-clang test` | PASS; deterministic Clang suites | `build/m05-clang.log` |
| `make sanitize sanitize-native` | PASS; default and actual-native ASan/UBSan/LeakSanitizer outside ptrace sandbox | `build/m05-sanitize.log` |
| `make test-native` | PASS after final stream/ownership/mode/status fixes; actual native API and TUI | `build/m05-native-delivery.log` |
| `make CC=clang CXX=clang++ BUILD_DIR=build/m05-clang test-native` | PASS after final fixes | `build/m05-clang-native-delivery.log` |
| `make sanitize-native` | PASS after final fixes, ASan/UBSan/LeakSanitizer enabled | `build/m05-sanitize-native-delivery.log` |
| `make analyze` | PASS; no unsuppressed project diagnostics; existing system-header/NOLINT suppressions reported | `build/m05-analyze-delivery.log` |
| `./build/test-gdb-startup arm-none-eabi-gdb` | PASS, actual target-free ARM GDB startup and setting-value queries | `build/m05-arm-startup.log` |
| `git diff --check` | PASS, repeated after README | No whitespace diagnostics |

The broad suites passed before final native-only refinements. Focused native
GCC/Clang/sanitizer suites and static analysis were then repeated for the final
code, including hidden-Launch-field Attach, independent partial console output,
actual echo output rather than its command log, and console-run ownership.
The ignore-HUP fixture ensures cleanup is deliberate termination, not merely
PTY hangup. No automated failure remains.

Coverage includes C/C++ launch; spaces/empty arguments/quotes/backslashes and
literal shell metacharacters; cwd/env fidelity; stdout/stderr and control/OSC
isolation; pause/continue; console recovery; existing-file preservation and
actual short-write failure under a temporary per-test RLIMIT_FSIZE; normal exit;
real allowed/denied ptrace; external survival and resumed heartbeat; output
flood/512-line retention; GDB crash/owned-inferior exit verification; GUI input
retention, forms/actions, launch/attach confirmations and signal TTY restoration.
Only owned test fixtures change per-process ptrace/dumpable flags; no system
ptrace settings or user tmux server/configuration are modified.

The new startup-setting regression was also compiled against the committed
pre-fix controller using:
`git show HEAD:src/gdb/controller.c > build/m05-controller-before.c`,
`cc -std=c17 -D_DEFAULT_SOURCE -Isrc/gdb -Itests/support tests/integration/test_gdb_startup.c build/m05-controller-before.c src/gdb/mi.c -o build/m05-settings-before`,
then `./build/m05-settings-before gdb`. It failed as expected (exit 1) when the
old quoted query did not produce a setting value. See
`build/m05-settings-before.log`. Actual native argv/env tests additionally caught
the original settings not being applied. The current probe passes.

Intermediate failures were inspected: sandbox ptrace/pidfd denial (rerun with
execution permission, never counted as a pass), incorrect set/attach argument
encoding, compiler truncation/aliasing diagnostics, analyzer bounds/casts, an
overbroad Cancel-button text match in the PTY test and a test that demanded
immediate numeric-PID disappearance before the adopting parent reaped a dead
inferior. Tests now verify actual behavior and permit a bounded reaping interval.
Logs from those attempts remain under `build/m05-*`.

Environment: native Linux x86-64, GDB/ARM GDB 18.1, GCC 16.2.1, Clang 23.1.1.
No timing claim for complete native UI response is made; full performance/stress
and live tmux acceptance remain M14/M18. pidfd recovery is Linux-dependent and
reports unsupported/unconfirmed cases. The crash case verifies owned death
and attached survival; it does not force every low-level recovery failure path.

Requirement evidence: FR-02 launch/cwd/args foundation (main/stepping still later),
FR-03/12/19/29 automated native paths, FR-16 lifecycle and NFR-01/03/04/06/08/09/12
foundations. Visual quality, physical input, the owner's native/tmux workflow and
M05 acceptance remain manual. All real boards, QEMU and complete release readiness
remain UNVERIFIED. An ARM GDB startup probe is not ARM firmware evidence.

Manual test now: follow **M05 manual acceptance** in docs/testing.md. Build the
fixtures, use the bare native launcher, inspect actual output, Continue/Pause,
Console/Save Log, launch termination and attached-process detach/resume. Inspect
the explicit unavailable live panes and separate --demo reference. Review the
project-focused README. AGENTS.md is updated locally but remains ignored by the
existing policy; the tracked contract is mirrored in docs/plan.md.

M05 manual-feedback correction: `q` now closes/backs out of the current screen
outside text fields, leaves the workspace through session confirmation and
returns to the launcher without exiting. At the launcher it stays open;
`Ctrl+Q` quits the application from any screen with active-session confirmation.
Footers name both actions. Text fields retain literal `q`; Escape cancels input.
The native TUI regression verifies launcher survival, `q` confirmation/cancel,
return to launcher, Ctrl+Q attached-session confirmation and commands/log paths
containing `q`. `make test` passed in `build/m05-quit-controls.log`; the subsequent
native part of that sandbox run failed on denied ptrace, not counted as PASS.
`make test-native` rerun outside the sandbox passed, including the native API
and TUI suites: `build/m05-quit-native.log`.

**Status: ACCEPTED.** The owner completed manual validation, committed/pushed
0278b61 and explicitly authorized M06. The agent performed no Git writes.

## M06 — selected context, Call Stack and Threads

M05 was explicitly accepted and committed/pushed as
`0278b612a8169253810c778607aa3d244a7b0407`, observed at HEAD and origin/main.
The owner authorized M06 only. No Git write, tag or milestone advancement has
been performed by the agent.

Implemented:

- C debugger-context model independent of curses, with owned snapshots of
  selected thread/frame, the original stopped thread/PC, structured frame
  metadata and simple arguments. Native session owns lifetime and routes
  distinct request cookies without completing another owner's pending request.
- Callback-safe deferred requests and coherent snapshot publication. Resume,
  thread lifecycle/selection, manual console operations and reconnect invalidate
  obsolete data. Pending superseded thread choices cannot publish old replies;
  frame selection rejects stale rows from a previous thread. No automatic
  context reads while running, invisible or idle.
- Live Call Stack/Threads in the approved workspace, with mouse selection,
  keyboard row browsing/Enter, caller labels, `>` selected and `*` stopped
  markers, scrollbars, Refresh and stack pages. Geometry/tabs/splits/theme and
  q-close/Ctrl+Q-quit semantics are preserved. Missing symbols/unwind and
  optimized/unavailable arguments remain honest. Unavailable context never
  displays a sentinel negative frame number as a real selection.
- 32-frame pages plus one lookahead, first 128 threads and a 4096-level display
  bound, with visible limits/errors. Strings and requests are bounded; selection
  changes GDB inspection context without moving execution. Source/Disassembly,
  stepping, Locals/Watches and other inspection remain in their own gates.
- C fake-peer context tests and real-GDB API/TUI fixtures with recursion, two
  workers, deliberate SIGTRAP checkpoints, optimized inline/tail-call metadata,
  stripped symbols, worker exit and repeated stop/resume. No extra frontend
  library/framework was introduced; pthread is only for the debug target.

Narrow correction to the earlier fixture PID report: the M05 fixture printed its
PID and then unconditionally emitted ESC[2J/OSC bytes, clearing a standalone
terminal. The agent's earlier scrolling explanation was wrong. Those bytes now
require explicit `--control-bytes`, used by the real-GDB output-safety test.
Ordinary and --allow-attach runs preserve the PID. The C harness verifies the
exact child PID prefix and no ESC/BEL through the first tick, while the explicit
control-byte test still verifies escaped output in the frontend.

Verification (local ignored artifacts, not release/hardware evidence):

| Command | Result | Evidence |
|---|---|---|
| `make test test-native` | PASS, deterministic and actual GDB API/TUI | `build/m06-gate.log` |
| `make CC=clang CXX=clang++ BUILD_DIR=build/m06-clang test test-native` | PASS | `build/m06-clang.log` |
| `make sanitize sanitize-native` | PASS, ASan/UBSan with leak detection | `build/m06-sanitize.log` |
| `make test-tmux` | PASS, isolated real tmux forwarding of the preserved demo | `build/m06-tmux.log` |
| `make analyze` | PASS, no unsuppressed project diagnostics | `build/m06-analyze-final.log` |
| `make CC=clang CXX=clang++ BUILD_DIR=build/m06-clang test` | PASS after final stale-frame guard/header correction | `build/m06-clang-final.log` |
| `make sanitize sanitize-native` | PASS after final stale-frame guard/header correction | `build/m06-sanitize-final.log` |
| `./build/test-context "$PWD/build/fake-mi"` | PASS including rejection of stale frame rows and session reset | `build/m06-context-final.log` |

Final native suites after the test-only control-byte/PID correction all passed:

| Command | Result | Evidence |
|---|---|---|
| `make test-native` | PASS, GCC actual GDB API/TUI and standalone PID/control regression | `build/m06-native-delivery.log` |
| `make CC=clang CXX=clang++ BUILD_DIR=build/m06-clang test-native` | PASS | `build/m06-clang-native-delivery.log` |
| `make sanitize-native` | PASS, ASan/UBSan with leak detection | `build/m06-sanitize-native-delivery.log` |
| `git diff --check` | PASS | No whitespace diagnostics |

Actual optimized GCC frames included context_checkpoint, recursive and tail_bridge
with optimized-out depth/worker values. Clang also reported these logical frames,
with its own unavailable arguments. Stripped runs retained valid addresses and
unwound unknown functions. Actual frame/argument rows are retained in the native
logs; no same-stack reconstruction or unwinding capability is inferred merely
from stripping symbols. Missing/malformed unwind is separately exercised by the
synthetic peer. There is no hardware/QEMU evidence in this gate.

Intermediate failures were inspected and corrected: GDB stop frames omit level;
the TUI test initially sent input before raw startup and clicked banner text
rather than a tab; skipping BODY activation initially omitted focused-pane
selection in the demo regression. Static analysis caught a missing enum zero
member and boolean bitwise assignments. A sandbox-only tmux run was denied its
local socket; it is not counted as a pass and the isolated run outside passed.
Analyzer success suppresses 149 system-header warnings and six existing NOLINT
annotations; it is not a claim of no generated warnings.

Requirement evidence: FR-10 Call Stack/context foundation (Locals/Watches remain
M11), Threads inventory, selected-vs-stopped correctness for FR-21's later code
views, and NFR-01/03/04/05/07/08/09/12 foundations. Manual visual/input acceptance,
complete native acceptance, UI/idle performance measurements and release-wide
requirements are not inferred from the automated results. STM32/ARM, ESP32/Xtensa,
ESP32/RISC-V and QEMU remain UNVERIFIED.

Manual test now: use the M06 checklist in docs/testing.md, launching
`./build/context-fixture` under the frontend. Inspect recursive arguments, choose
callers by mouse/keyboard, show Threads in another pane, switch workers, continue
to their exit, verify stale running state, Pause and page the --deep variant.
Repeat in tmux and review optimized/stripped behavior. M07 remains planned.

State: `implemented` → `automated-verified` → `awaiting-user`.
**Status: AWAITING USER ACCEPTANCE.** Include the proposed Conventional Commit
message at delivery. Do not start M07 until owner acceptance, owner commit/push
and explicit authorization. AGENTS.md is updated locally but remains ignored by
the existing policy; its authorization is mirrored in the tracked docs/plan.md.

Subsequent owner acceptance: M06 was approved, committed and pushed as
`1b215048c7e0d94e9515f8e9321215f08ce8cc6e`, observed in local and origin history.
The owner authorized M07 and clarified the positive-feedback rule recorded above.
An owner cleanup commit `10a4662` removes an unrelated root `test` file and is
also observed at HEAD/origin; accepted M06 history is preserved. No agent Git
writes were performed.

## M07 — live code, stepping and source recovery

Scope: FR-06 stepping, FR-07, FR-21/22/28 and NFR-01/03/04/05/06/07/08/09/12
foundations. No M08 breakpoint/gutter/F9/Run-to implementation, later inspection,
profile persistence, remote/OpenOCD connection or release claim is included.

Implemented:

- Owned C code model, raw filesystem paths separate from escaped display labels,
  GDB-known source-file catalog, bounded source buffers and instruction windows.
  Live Source/Disassembly follow selected frame/PC; actual stopped execution and
  inspected callers have distinct markers. Code remains explicitly stale while
  running or refreshing, and missing/optimized/stripped mappings stay honest.
- Real MI source/instruction Next/Step Into and Step Out. Source/Disassembly
  focus selects F10/F11 mode; toolbar/inspectors retain it. Execution operates on
  the selected thread's execution frame zero, including after caller inspection.
  Browsing, address/symbol lookup and return-to-stop never write PC or resume.
- Optional mixed source/assembly with independent pane fallback and AT&T x86-64
  syntax. Actual addresses/opcodes and source lexical styling use the approved
  surfaces. Scrollbars, code horizontal pan, bounded instruction paging and tiled
  Actions/input screens preserve the existing default workspace and split/tab UX.
- File filtering, current-file text find/line navigation, strict hex/simple-symbol
  assembly navigation and original stopped-thread/frame recovery. Symbol lookup
  uses C address-of semantics, never a C++ overloaded operator/inferior call.
- GDB source directories and ordered build-prefix substitutions, successful-reply
  state updates, duplicate/replaced rules and Clear overrides. Literal spaces,
  quotes and metacharacters pass through distinct CLI/MI encoders without a shell.
  Unresolved relative paths are rejected rather than guessed from frontend cwd.
  These source settings are session-only until M13.
- C synthetic model tests plus actual GDB API/TUI tests and -O0/-O2/stripped/C++/
  relocated-source fixtures. Source recovery preserves duplicate-basename identity.
  The application gains no library dependency or test scripting language.

Reviewed limits: 256 catalog files, 1 MiB / 32,768 source lines per code pane,
128 instructions per 256-byte window, 16 previous window anchors and eight
directory/mapping overrides each. Limits/errors are visible. Mixed groups outside
the pane's cached source retain file/line information with unavailable text;
the frontend does not eagerly load every inline source file. Local source reads
are bounded but synchronous; unusually slow filesystems remain a latency risk.
No quantitative UI response/performance acceptance is claimed in this gate.

Corrections during verification: an assembly request's argument count initially
exceeded its argv array (real-GDB crash), then was fixed using the array's size.
TUI tests needed real mouse press/release settling and compiler-independent main
stop expectations. The older context test exposed an actual focus-index defect:
source availability adds a scrollbar before another pane's controls. Focus now
tracks body identity across redraws; keyboard caller selection is regression
covered. Sanitizer compilation caught a restrict-copy warning, corrected with
an exact-size nonoverlapping-field copy. Failed attempts are not passing evidence.

Hardware/remote/QEMU: **UNVERIFIED** for this gate. Native API/PTY and isolated
tmux tests do not replace physical visual/input acceptance or board evidence.
Manual procedure: docs/testing.md's M07 checklist. Launch code-fixture, use the
deliberate Console `start`, exercise both stepping modes, mixed/AT&T, caller/stop
markers, navigation and relocated source; repeat in terminal/tmux and degraded
fixtures. The owner must validate the rendered result before acceptance.

Verification (GCC 16.2.1, Clang 23.1.1, GDB 18.1, tmux 3.8 on Linux x86-64;
local ignored logs, not hardware/release evidence):

| Command | Result | Evidence |
|---|---|---|
| `make -j4 test test-native` | PASS, all deterministic and actual GDB API/TUI suites | `build/m07-delivery.log` |
| `make -j4 CC=clang CXX=clang++ BUILD_DIR=build/m07-clang test test-native` | PASS | `build/m07-clang-delivery.log` |
| `make sanitize sanitize-native` | PASS, ASan/UBSan with leak detection | `build/m07-sanitize-delivery.log` |
| `make analyze` | PASS, no unsuppressed project diagnostics; 168 non-user and nine NOLINT warnings suppressed | `build/m07-analyze-delivery.log` |
| `make test-tmux` | PASS, isolated real tmux regression of preserved demo tab/mouse/drag/split/F10/resize behavior; live code inside tmux still manual | `build/m07-tmux-final.log` |
| `make -j4 all build/test-cli` then `build/test-cli "$PWD/build/the-bugger"` | PASS after final help-text update | `build/m07-help.log` |
| `git diff --check` | PASS | No whitespace diagnostics |

The final source-path safeguard refuses unresolved relative paths; its synthetic
case includes valid address zero and reruns above cover final model/TUI sources.
The initial targeted rerun used an old fake-peer binary, correctly failed its
catalog assertion, then passed after rebuilding that peer. No test was disabled.
Earlier full GCC/Clang/sanitizer passes and focused code/context TUI runs are in
`build/m07-{gate,clang-final,sanitize-final,code-native,context-tui}.log`.

Changed files: Makefile and src/main.c help; src/model/code.{c,h}, context.{c,h};
src/session/native.{c,h}; src/tui/app.c and render.{c,h};
tests/support/fake_mi.c, tests/integration/test_code*.c and test_context_tui.c;
tests/fixtures/native/code.{c,cpp} and relocated source tree; README/CHANGELOG,
architecture/roadmap/testing/progress and test/fixture documentation. AGENTS.md
tracks the same authorization/workflow locally and remains ignored by policy.

State: `implemented` → `automated-verified` → `awaiting-user`.
**Status: AWAITING USER ACCEPTANCE.** No commit exists for M07 yet. Stop for
manual validation; include the proposed Conventional Commit message at delivery.
M08 requires owner acceptance, owner commit/push and continuation authorization
under the standing positive-feedback rule. No Git writes or release actions.

M07 manual feedback correction: the owner reported STALE in both code panes;
the status was Loaded / PID not started. Empty panes were incorrectly described
as stale before any execution. They now say Awaiting stop; browsed loaded source
still says BROWSE. True running/refreshing retained data keeps STALE. The direct
Console-start path is now tested without prior file browsing, asserting both
initial labels, actual live source/assembly and real PC after `start`.

`make -j4 all build/test-code-tui build/test-context-tui`, followed by
`build/test-code-tui "$PWD/build/the-bugger" "$PWD/build/code-fixture"` and
`build/test-context-tui "$PWD/build/the-bugger" "$PWD/build/context-fixture"`
passed (`build/m07-stale-{build,tui,context}.log`). `git diff --check` passed.
The checklist explicitly identifies the Console Send action. This corrects a
misleading Loaded label, not a reproduced failure to refresh after a real stop.
M07 remains awaiting user acceptance; the proposed commit includes this fix.
`make analyze` also passed (`build/m07-stale-analyze.log`), with the same
168 non-user/nine NOLINT suppressions and no unsuppressed project diagnostics.

M07 manual-review scrollbar follow-up: the owner requested wider vertical bars,
horizontal access to clipped text without resizing panes, and hiding bars when
their content fits. Vertical tracks/thumbs now have two-cell paint/hit regions.
Live Source/Disassembly, Console, Call Stack and Threads share overflow geometry,
reserve horizontal tracks above action footers, and retain independent horizontal
offsets per pane/view. Source line/execution gutters stay fixed. Thumb dragging,
track paging and Left/Right operate within clamped bounds; horizontal wheels work
over the track. Neither axis draws a bar when its data fits. Static demo vertical
bars use the wider target; live horizontal inspection does not fabricate demo
backend functionality. Browsing still leaves stopped PC unchanged.

C TUI regressions exercise Source/Console horizontal drag, track paging, keyboard
clamping, independent positions, two-cell vertical paint, PC preservation and
bar disappearance when source fits after resizing. The prior render assertion
expected selection in a column now occupied by the wider scrollbar; its new
assertions verify both track columns and the adjacent selection boundary.

| Command | Result | Evidence |
|---|---|---|
| `make -j4 test test-native` | PASS | `build/m07-scroll-final.log` |
| `make -j4 CC=clang CXX=clang++ BUILD_DIR=build/m07-clang test test-native` | PASS | `build/m07-scroll-clang-final.log` |
| `make test-tmux` | PASS, preserved demo mouse/drag/key/layout forwarding | `build/m07-scroll-tmux.log` |
| `make sanitize sanitize-native` | PASS, ASan/UBSan with leak detection | `build/m07-scroll-sanitize.log` |
| `make analyze` | PASS, no unsuppressed project diagnostics; 169 non-user/11 NOLINT suppressions | `build/m07-scroll-analyze-final.log` |
| Rebuild/run `test-code-tui` with GCC and Clang after additional track-paging assertions | PASS | `build/m07-scroll-paging{,-clang}.log` |

Analyzer originally flagged adjacent axis dimensions and view/row parameters;
two documented narrow NOLINT annotations identify their intentional domain order.
No diagnostic check was disabled globally. `git diff --check` passes.
The final additional paging assertions also pass under ASan/UBSan with leak
detection: rebuild the sanitizer application/test-code-tui using the flags from
Makefile's sanitize target, then run
`UBSAN_OPTIONS=halt_on_error=1 build/sanitize/test-code-tui "$PWD/build/sanitize/the-bugger" "$PWD/build/sanitize/code-fixture"`
(`build/m07-scroll-paging-sanitize{,-build}.log`).

M07 owner-requested launcher refinement: the later supplied image replaces only
the startup presentation. The card is centered on both axes (up to 100 columns;
23 rows normally, 15 below 28 terminal rows), using existing Monokai Pro roles,
pink title, right-aligned v0.1.0, subtle rules, whole-row choices/descriptions,
small ASCII icons and chevrons. Selected rows retain a non-color marker and
monochrome behavior. No assets, additional library, animation or polling work was
introduced. Existing configuration/workspace, session availability, unavailable
profiles, return-to-preview and quit semantics remain intact; no beta suffix.

The C PTY regression verifies horizontal/vertical centering at 140x44 and 200x60,
description-row activation, suffix-free version and all three keyboard-selectable
flows at 48x18. Normal suites retain mouse session forms, direct/256/monochrome
rendering, workspace controls, terminal cleanup and real native code debugging.

| Command | Result | Evidence |
|---|---|---|
| `make -j4 test test-native` | PASS | `build/m07-launcher-gate.log` |
| `make -j4 CC=clang CXX=clang++ BUILD_DIR=build/m07-clang test test-native` | PASS | `build/m07-launcher-clang.log` |
| `make analyze` | PASS, no unsuppressed project diagnostics; 173 non-user/11 NOLINT suppressions | `build/m07-launcher-analyze.log` |
| `make test-tmux` | PASS, isolated server and preserved session/workspace forwarding | `build/m07-launcher-tmux.log` |
| `make sanitize sanitize-native` | PASS, ASan/UBSan with leak detection | `build/m07-launcher-sanitize.log` |
| `git diff --check` | PASS | No whitespace diagnostics |

Changed src/tui/app.c, tests/integration/test_tui.c and the existing specification,
design mapping, architecture, roadmap, testing/progress/changelog and README.
Manual acceptance: inspect the new launcher at ordinary/large/compact sizes,
activate titles/descriptions by mouse and keyboard, then complete pending M07
debugging/scrollbar checks. M07 remains AWAITING USER ACCEPTANCE; no Git writes.

M07 further assembly/scrollbar review: the owner rejected instruction windows
restarting at PC on every step and the two-column vertical bar. The later agreed
policy supersedes that earlier width request: vertical bars are one cell, with
the same cyan thumb/muted track as horizontal bars. Current instructions are
retained within a refreshed window, and 16 earlier anchors/ranges survive stop
changes for backward browsing and returning from callees. Wheel/key scrolling
at window edges can fetch known previous/next windows. Browsing never moves PC.

Initial context uses GDB's structured instruction function offset, where valid,
for one bounded preceding decode (1024-byte lookbehind plus the ordinary 256-byte
forward range). Only four preceding instructions and the bounded forward listing
are retained. Nearby function-origin browsing can decode up to the current window
and retain at most 128 preceding instructions. No guessed x86 boundaries, eager
whole-function scan, growing execution history or frontend expression evaluation
is introduced. Missing/distant origins retain honest navigation limitations.
Every stopped refresh reads current bytes; old windows are never passed off as
historical execution or current target values without a fresh reply.

The owner agreed to a stable viewport through the middle, scrolling only to keep
about five rows below PC; short panes scale this margin. First context can place
PC around the fifth visible instruction if four earlier instructions are known.
Manual browsing stays put until stepping/Stopped. Toolbar now spells out
Shift+F11; actual key input is regression tested returning from code_callee to
main. Step Out is function return, not leaving a loop; an endless-looping function
can keep running until interrupted. Official IAR Debug menu documents the same
binding/meaning; this is not an F12 remap or a new loop-exit action.

Tests add synthetic offsets/earlier bounded decoding, native retention of the
previous PC instruction across steps, earlier-window browsing after return, and
20 actual TUI instruction steps checking viewport stability and the lower margin.
Rendering asserts one-cell styling and the adjacent selection boundary. Earlier
test failures were stale toolbar text, a clipped source-line expectation and a
tab-label coordinate used for the execution gutter; all were inspected against
actual rendered rows rather than changing debugger semantics to satisfy tests.

Changed model/code, TUI app/render, fake MI peer, code model/native/TUI and render
tests, plus existing specification/roadmap/design/architecture/testing/README/
changelog documentation. M07 still awaits owner review. No Git writes or M08 work.

| Command | Result | Evidence |
|---|---|---|
| `make -j4 test test-native` | PASS | `build/m07-assembly-final.log` |
| `make -j4 CC=clang CXX=clang++ BUILD_DIR=build/m07-clang test test-native` | PASS | `build/m07-assembly-clang.log` |
| `make sanitize sanitize-native` | PASS, ASan/UBSan with leak detection | `build/m07-assembly-sanitize.log` |
| `make analyze` | PASS, no unsuppressed project diagnostics; 173 non-user/11 NOLINT suppressions | `build/m07-assembly-analyze.log` |
| `make test-tmux` | PASS, isolated real tmux with the narrow bar/explicit key label | `build/m07-padding-tmux.log` |
| `git diff --check` | PASS | No whitespace diagnostics |

Manual test now: repeat instruction F11/F10 in code-fixture, scroll before/after
PC, verify earlier instructions persist and a five-row lower margin only moves
the viewport when necessary. Check one-cell vertical style against the horizontal
bar. Try Shift+F11 or its explicit toolbar button inside code_callee; it returns
from that finite function. In an endless-looping main, Step Out may run until
Pause because main does not return. Inspect ordinary and short panes in terminal/
tmux. GDB metadata limits initial preceding context; hardware remains UNVERIFIED.
Status remains **AWAITING USER ACCEPTANCE** for M07 and its corrections.

Latest M07 screenshot feedback — only immediate points 1–2 implemented:

- Vertical scrollbar width stays one cell. Its muted `|` track/cyan `#` thumb
  express the vertical axis; horizontal bars retain their own `-`/`=` glyphs.
- Wheel-up at the initial assembly boundary could read a one-instruction prefix
  and replace the entire forward listing. Backward-prefix replies now prepend
  those instructions to a bounded owned copy of the existing forward data,
  retaining at most 128 combined instructions. The old first row anchors viewport
  movement, so a short prepend neither collapses the display nor jumps to the
  tail. PC remains unchanged. Copies are freed on completion/error/cancellation,
  navigation changes, reset and destruction. Further actions reject a queued
  stale window rather than overwrite a pending navigation.
- Synthetic regression includes exactly one preceding instruction. Actual GDB
  repeats wheel-up/down at debug-fixture's main entry and checks that at least
  eight rows and the original PC remain visible. Existing code/scroll/step and
  workspace suites remain enabled. Makefile passes the additional fixture to the
  C TUI harness; its original three-argument invocation remains supported.

Points 3–4 are deliberately **not researched or redesigned in this correction**.
The tracked docs/plan.md records M08's owner-requested stages: first thorough
official-source research/report and owner decisions about wider disassembly
browsing/availability and repeated zero-byte decoding; then the approved browsing
implementation; finally original M08 breakpoints/Run to Selected. Wait for M07
manual acceptance, owner commit/push, owner go-ahead and planned model switch.
Neither the screenshot nor these fixes establish that an entire target's
assembly is already available or that zero-byte decoding denotes application code.

Immediate changes: src/model/code.c; src/tui/app.c/render.c; C fake-peer/model/TUI
tests and Makefile; existing specification, roadmap, design, architecture, tests,
changelog/progress and README. M07 remains AWAITING USER ACCEPTANCE. No Git writes.

| Command | Result | Evidence |
|---|---|---|
| `make -j4 test test-native` | PASS, including the original-session wheel regression | `build/m07-wheel-final.log` |
| `make -j4 CC=clang CXX=clang++ BUILD_DIR=build/m07-clang test test-native` | PASS | `build/m07-wheel-clang-final.log` |
| `make sanitize sanitize-native` | PASS, ASan/UBSan with leak detection | `build/m07-wheel-sanitize-final.log` |
| `make analyze` | PASS, no unsuppressed project diagnostics; 173 non-user/11 NOLINT suppressions | `build/m07-wheel-analyze-final.log` |
| `make test-tmux` | PASS, isolated demo terminal/mouse/layout forwarding | `build/m07-wheel-tmux.log` |
| `git diff --check` | PASS | No whitespace diagnostics |

Manual check: relaunch, load debug-fixture and send Console `start`; wheel up at
the top of Disassembly and back down. Rows/current PC must not collapse to the
single earlier push instruction. Check the vertical one-cell `|`/`#` bar and
unchanged horizontal `-`/`=` bar. Complete owner review before committing M07.
Broader browsing/data-boundary limitations remain for M08 research; hardware and
release acceptance remain UNVERIFIED.

Additional changed files: tests/unit/test_render.c and docs/design/tui-mapping.md;
the shared scrolling correction stays in src/tui/app.c/render.c and the existing
code TUI harness. Specification/roadmap/checklists describe the owner request.
M07 remains awaiting manual acceptance; no Git writes or next-gate work occurred.

### M07 review — symmetric navigation margin and default pane height

Owner requested the existing five-row Disassembly follow margin at the top as
well as the bottom, and slightly more height for Source/Disassembly/Registers.
Following now scrolls only when PC enters either margin, with bounds for short
panes and the available listing. Manual browsing remains unchanged until a new
step/return-to-stop update. Initial limited preceding context is unchanged;
broader disassembly availability remains the M08 research decision.

The default upper row receives 62% rather than 56% of usable height. Only the
initial ratio changed: user dragging, minimum sizes, splits and resize retention
continue through the existing layout engine. No additional workspace mode.

Changed src/tui/app.c, src/layout/layout.c, tests/unit/test_layout.c and
tests/integration/test_code_tui.c, plus the existing specification/design/testing/
architecture/roadmap/changelog notes. The real-GDB C terminal test now browses PC
near the top, resumes instruction stepping and checks both five-row margins;
the layout test checks that the default upper row is taller. README is updated
last. M07 remains awaiting-user; hardware/release evidence is still UNVERIFIED.

| Command | Result | Evidence |
|---|---|---|
| `make -j4 test test-native` | PASS | `build/m07-padding-final.log` |
| `make -j4 CC=clang CXX=clang++ BUILD_DIR=build/m07-clang test test-native` | PASS | `build/m07-padding-clang.log` |
| `make sanitize sanitize-native` | PASS, ASan/UBSan and leak checks | `build/m07-padding-sanitize.log` |
| `make analyze` | PASS, no unsuppressed project diagnostics | `build/m07-padding-analyze.log` |
| `make test-tmux` | PASS, isolated terminal/mouse/layout checks | `build/m07-padding-tmux.log` |
| `git diff --check` | PASS | No whitespace diagnostics |

Manual check: start the TUI, load code-fixture and send Console `start`. Inspect
the taller default upper row. Step instructions toward the bottom; browse PC
near the top and step again. Expect roughly five visible rows on either side
where context permits. Drag the row divider and shrink/restore the terminal;
your ratio must remain in effect. Owner visual acceptance remains pending.

### M07 acceptance

The owner explicitly accepted M07 after the symmetric navigation-margin and
default-pane-height corrections. Commit
`8d6947570a1d8dc1d904472b01b66e6b54415598` is reliably observed at both local HEAD
and origin/main. Earlier awaiting-user entries are historical. No Git write was
performed by the agent.

M08 remains planned, awaiting the owner's go-ahead after the requested model
switch. Its first stage is the documented disassembly research and decision
discussion; implementation follows only the approved findings. Hardware/release
acceptance remains UNVERIFIED. This acceptance-ledger update changes no code;
`git diff --check` passes.

### M08 Stage 1 — disassembly research started

The owner clarified that the acceptance message authorized continuation, and
8d69475 is already at HEAD and origin/main. The earlier wait for another
go-ahead is superseded. M08 remains planned as an implementation gate while its
research/decision stage is active; no new application feature is implemented.

Consulted official IAR Disassembly window reference (Toolbar, Display area,
Context menu), GDB MI data/file/symbol commands and memory/machine-code manuals,
Microsoft VS Code DisassemblyView and MIEngine Disassembly implementations,
Arm AAELF32 mapping-symbol rules and OpenOCD GDB memory-map documentation.
Findings and product choices are presented in the discussion, without a separate
research report or an independently finalized browsing policy.

Read-only local probes using GDB 18.1 and ARM GDB 18.1 demonstrate whole-function
MI disassembly on native ELF and a Cortex-M4 Thumb object, x86 zero-filled data
decoded as repeated add instructions, and unavailable file-list-exec-sections
on both installed GDBs. A range extending beyond available offline code returns
an MI error. Logs: build/m08-disassembly-probe.mi,
build/m08-zero-bytes-probe.mi and build/m08-arm-disassembly-probe.mi. The temporary
C fixture lives in /tmp; no new application code or dependencies were added.
These probes are not live hardware acceptance. Broad app suites are not rerun
for this research/documentation step. `git diff --check` passes.
