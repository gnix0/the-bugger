# Validation records

This is the project evidence register: implementation references, acceptance
states, executed checks, significant failures and unresolved validation limits.
It complements the [roadmap](roadmap.md), [test procedures](testing.md) and
[architecture](architecture.md). Procedures describe how to test; entries here
state what was actually recorded. No workflow transcript or superseded agent
instructions are required to interpret the results.

## Current evidence boundary

M00–M18 and the listed corrections form the first-release baseline. The maintainer
reported v0.1.0 published on 2026-10-20. Historical preparation entries below retain
their original pending states and test boundaries; publication does not supply
missing individual manual outcomes or new hardware evidence. F19 is accepted
through the maintainer-reported implementation commit. F19 and the accepted
shared navigation/fullscreen work are included in v0.1.1 preparation; publication
and fresh release-matrix validation are not implied by implementation acceptance.

M14 records passing GCC/Clang default/native/tmux suites,
slow watchpoint recovery, full default/native ASan/UBSan with leak detection and
full-project static analysis. These results do not establish physical embedded
compatibility or measured terminal input-to-redraw latency. M15 now records
passing GCC/Clang default/native/remote/tmux regressions, full default/native/remote
ASan/UBSan with leak detection and full-project static analysis. M15 is accepted
through its implementation commit; individual manual outcomes were not separately
reported. M16 records passing GCC/Clang regressions, sanitizer/leak checks and static
analysis, including focused offline OpenOCD lifecycle coverage. M17 records
physical NUCLEO-F446RE/ARM and ESP32-S3/Xtensa workflows with sanitizer and
terminal/tmux coverage. Other physical targets remain unverified. QEMU coverage
is supplementary and currently unverified.

Log paths identify the original local artifacts, which are ignored/disposable;
the command/result summaries below are the durable record. Unreported runs and
failed environment checks remain distinct from PASS. Acceptance does not imply
that every checklist step was separately reported. No tests were rerun to
restructure these records.

## Implementation references and acceptance

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
| M18 | v0.1.0 release evidence/acceptance | accepted; published | v0.1.0; preparation revision recorded below |
| F19 | Themes, variants and Config | accepted; unreleased | [Focused checks](#f19--themes-and-configuration); implementation committed per maintainer report |


### Full recorded commit identifiers

- `75435199e498cc087941cd8a75d7232b65d4c720`

- `1dee59f33323d37af9739ce846f1a676ea56d2d5`
- `4030df3fa41da5805cedf1310b8c8ee7694b3112`
- `f1dbb8a70033821e33e9a5506e23758d0723587d`
- `77446434d8e6e1e1a356abaa4d94e028a1098036`
- `2f951fb8dbaa10f490e5abd6eba31d1ced1dc6af`
- `55ecbd4f627d0374f81176290189ca5b4808ed5d`
- `60df6550d0434b2c32977ec8179f5eb4ca92f55b`
- `094fa714da9400ae7a40c0357a1552ba38a1ffd7`
- `f29ce3c23e3d818c64cdee44ddd501c73251a5bc`
- `0278b612a8169253810c778607aa3d244a7b0407`
- `1b215048c7e0d94e9515f8e9321215f08ce8cc6e`
- `8d6947570a1d8dc1d904472b01b66e6b54415598`

The UI specification correction additionally records `86820c4`; the table
retains other short identifiers exactly where a full identifier was not recorded.

## M00

Initial sanitizer execution failed because LeakSanitizer cannot run under ptrace; the outside-sandbox rerun passed without disabling leak checks. Static analysis initially enabled no checks; explicit analyzer/bugprone configuration corrected that setup. No debugging or board functionality was validated by the foundation tests.

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

## UI correction

The six-pane/no-presets specification correction was accepted at 86820c4. It changed the documented interface direction and prototype location, not the already recorded foundation test results.

## M01

The initial renderer was rejected on visual fidelity despite interaction tests. Palette, surfaces, source coloring, headings, toolbar density and active-row styling were corrected; acceptance followed the visual correction. PTY and simulated startup/stepping evidence is not live debugging or physical mouse/UTF-8 proof.

| Exact command/check | Outcome | Local evidence |
|---|---|---|
| `make BUILD_DIR=build/m01-clean test` | PASS; fresh strict GCC build, C unit/CLI/PTY suites | `build/m01-clean-build-test.log` |
| `make test` | PASS; default build and all C suites | `build/m01-test-final.log` |
| `make CC=clang BUILD_DIR=build/clang test` | PASS; strict Clang build and all C suites | `build/m01-clang-test.log` |
| `make sanitize` (approved outside sandbox) | PASS; ASan/UBSan/leak detection, all C suites | `build/m01-sanitize.log` |
| `make analyze` | PASS; no unsuppressed project diagnostics | `build/m01-analyze-final.log` |
| `git diff --check` | PASS; no whitespace errors | Terminal output |
| Isolated `tmux -L <unique-name> -f /dev/null new-session -d -s demo -x 140 -y 44 <binary>`, opt-in mouse on, `send-keys` startup/F5/F10, `capture-pane -p` | PASS; launcher, six panes, running state and stepped sample PC; test server cleaned up | `build/m01-tmux-{launcher,workspace,running,step}.txt` |

## M02

Layout evidence covers geometry, minimum sizes, split/collapse, compact restoration, bounded pane-pool reuse and real tmux forwarding. It validates UI behavior, not debugger or board support.

| Command | Result | Evidence |
|---|---|---|
| `make test` | PASS | `build/m02-test.log` |
| `make CC=clang BUILD_DIR=build/m02-clang test` | PASS | `build/m02-clang.log` |
| `make sanitize` | PASS, ASan/UBSan/LeakSanitizer | `build/m02-sanitize.log` |
| `make analyze` | PASS | `build/m02-analyze.log` |
| `make test-tmux` | PASS, real isolated tmux input forwarding | `build/m02-tmux.log`, `build/m02-tmux-workspace.txt` |
| `git diff --check` | PASS | No whitespace diagnostics |

## Post-M02

Navigation corrections were accepted at a832eef: no preloaded demo breakpoint, direct viewport scrolling, reachability of disassembly endpoints and scrollbar paging/dragging. This was presentation evidence, not live target validation.

## P00

Scope was documentation expansion and inactive-tab focus correction, including monochrome/truecolor/tmux cases. The implementation used a combined code/documentation commit; that fact does not alter current contribution procedures.

| Exact command | Result | Local evidence |
|---|---|---|
| `make test` | PASS; strict GCC and all default C suites | `build/p00-test.log` |
| `make CC=clang BUILD_DIR=build/p00-clang test` | PASS; separate strict Clang build and suites | `build/p00-clang.log` |
| `make sanitize` | PASS; ASan/UBSan/LeakSanitizer, outside ptrace sandbox | `build/p00-sanitize.log` |
| `make analyze` | PASS; no unsuppressed project diagnostics; existing system-header/NOLINT suppressions reported | `build/p00-analyze.log` |
| `make test-tmux` | PASS; real isolated tmux, including tab styling/focus | `build/p00-tmux.log`, `build/m02-tmux-workspace.txt` |
| `git diff --check` | PASS before final README update; repeated at final handoff | No whitespace diagnostics |

## M03

Parser records retain tokens/byte lengths, recovery and allocation-failure evidence. Actual native and ARM GDB target-free protocol probes do not establish target execution or hardware compatibility. No transport/process model was included in parser acceptance.

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

## M04

Transport evidence includes deadlines, reordered/obsolete replies, bounded floods, descriptor/lifecycle failures, externally reaped children and cancellation. The harness stderr preview was capped without changing production parsing. Measured pump time belongs to the fake-peer transport case, not whole-TUI or hardware performance.

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

## M05

Real GDB tests exposed quoted CLI-derived -gdb-set names returning ^done without applying settings; the encoding was corrected and startup probes verify actual async/all-stop values. Owned launch cleanup uses stable pidfds; attached processes survive detach. A fixture control-byte flag previously erased the standalone printed PID; ordinary fixture output was corrected without suppressing captured-output tests. A sandbox native run failed on ptrace restrictions; the outside-sandbox native rerun passed in build/m05-quit-native.log.

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

## M06

Context tests cover recursive/paged, optimized/stripped, multithread and worker-exit cases. A stale-frame guard/header correction was followed by the recorded final runs. Missing or optimized frames follow GDB rather than fabricated source precision. Real board and RTOS inspection were not established.

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

| Command | Result | Evidence |
|---|---|---|
| `make test-native` | PASS, GCC actual GDB API/TUI and standalone PID/control regression | `build/m06-native-delivery.log` |
| `make CC=clang CXX=clang++ BUILD_DIR=build/m06-clang test-native` | PASS | `build/m06-clang-native-delivery.log` |
| `make sanitize-native` | PASS, ASan/UBSan with leak detection | `build/m06-sanitize-native-delivery.log` |
| `git diff --check` | PASS | No whitespace diagnostics |

## M07

Code/TUI corrections covered source identity, startup Awaiting stop versus stale data, horizontal navigation, one-cell vertical bars, persistent assembly context, short backward-read merging, five-row top/bottom margins and the 62% default upper row. The tables distinguish runs before and after those corrections. tmux automation chiefly validates forwarding/demo interaction; live visual/input acceptance is separate. Hardware compatibility and release-wide performance were not established.

| Command | Result | Evidence |
|---|---|---|
| `make -j4 test test-native` | PASS, all deterministic and actual GDB API/TUI suites | `build/m07-delivery.log` |
| `make -j4 CC=clang CXX=clang++ BUILD_DIR=build/m07-clang test test-native` | PASS | `build/m07-clang-delivery.log` |
| `make sanitize sanitize-native` | PASS, ASan/UBSan with leak detection | `build/m07-sanitize-delivery.log` |
| `make analyze` | PASS, no unsuppressed project diagnostics; 168 non-user and nine NOLINT warnings suppressed | `build/m07-analyze-delivery.log` |
| `make test-tmux` | PASS, isolated real tmux regression of preserved demo tab/mouse/drag/split/F10/resize behavior; live code inside tmux still manual | `build/m07-tmux-final.log` |
| `make -j4 all build/test-cli` then `build/test-cli "$PWD/build/the-bugger"` | PASS after final help-text update | `build/m07-help.log` |
| `git diff --check` | PASS | No whitespace diagnostics |

| Command | Result | Evidence |
|---|---|---|
| `make -j4 test test-native` | PASS | `build/m07-scroll-final.log` |
| `make -j4 CC=clang CXX=clang++ BUILD_DIR=build/m07-clang test test-native` | PASS | `build/m07-scroll-clang-final.log` |
| `make test-tmux` | PASS, preserved demo mouse/drag/key/layout forwarding | `build/m07-scroll-tmux.log` |
| `make sanitize sanitize-native` | PASS, ASan/UBSan with leak detection | `build/m07-scroll-sanitize.log` |
| `make analyze` | PASS, no unsuppressed project diagnostics; 169 non-user/11 NOLINT suppressions | `build/m07-scroll-analyze-final.log` |
| Rebuild/run `test-code-tui` with GCC and Clang after additional track-paging assertions | PASS | `build/m07-scroll-paging{,-clang}.log` |

| Command | Result | Evidence |
|---|---|---|
| `make -j4 test test-native` | PASS | `build/m07-launcher-gate.log` |
| `make -j4 CC=clang CXX=clang++ BUILD_DIR=build/m07-clang test test-native` | PASS | `build/m07-launcher-clang.log` |
| `make analyze` | PASS, no unsuppressed project diagnostics; 173 non-user/11 NOLINT suppressions | `build/m07-launcher-analyze.log` |
| `make test-tmux` | PASS, isolated server and preserved session/workspace forwarding | `build/m07-launcher-tmux.log` |
| `make sanitize sanitize-native` | PASS, ASan/UBSan with leak detection | `build/m07-launcher-sanitize.log` |
| `git diff --check` | PASS | No whitespace diagnostics |

| Command | Result | Evidence |
|---|---|---|
| `make -j4 test test-native` | PASS | `build/m07-assembly-final.log` |
| `make -j4 CC=clang CXX=clang++ BUILD_DIR=build/m07-clang test test-native` | PASS | `build/m07-assembly-clang.log` |
| `make sanitize sanitize-native` | PASS, ASan/UBSan with leak detection | `build/m07-assembly-sanitize.log` |
| `make analyze` | PASS, no unsuppressed project diagnostics; 173 non-user/11 NOLINT suppressions | `build/m07-assembly-analyze.log` |
| `make test-tmux` | PASS, isolated real tmux with the narrow bar/explicit key label | `build/m07-padding-tmux.log` |
| `git diff --check` | PASS | No whitespace diagnostics |

| Command | Result | Evidence |
|---|---|---|
| `make -j4 test test-native` | PASS, including the original-session wheel regression | `build/m07-wheel-final.log` |
| `make -j4 CC=clang CXX=clang++ BUILD_DIR=build/m07-clang test test-native` | PASS | `build/m07-wheel-clang-final.log` |
| `make sanitize sanitize-native` | PASS, ASan/UBSan with leak detection | `build/m07-wheel-sanitize-final.log` |
| `make analyze` | PASS, no unsuppressed project diagnostics; 173 non-user/11 NOLINT suppressions | `build/m07-wheel-analyze-final.log` |
| `make test-tmux` | PASS, isolated demo terminal/mouse/layout forwarding | `build/m07-wheel-tmux.log` |
| `git diff --check` | PASS | No whitespace diagnostics |

### M07 review — symmetric navigation margin and default pane height

| Command | Result | Evidence |
|---|---|---|
| `make -j4 test test-native` | PASS | `build/m07-padding-final.log` |
| `make -j4 CC=clang CXX=clang++ BUILD_DIR=build/m07-clang test test-native` | PASS | `build/m07-padding-clang.log` |
| `make sanitize sanitize-native` | PASS, ASan/UBSan and leak checks | `build/m07-padding-sanitize.log` |
| `make analyze` | PASS, no unsuppressed project diagnostics | `build/m07-padding-analyze.log` |
| `make test-tmux` | PASS, isolated terminal/mouse/layout checks | `build/m07-padding-tmux.log` |
| `git diff --check` | PASS | No whitespace diagnostics |

## M08

Concurrent sanitizer/build execution first failed the existing controller 35 ms timing assertion without a sanitizer runtime diagnostic; the isolated sanitizer rerun passed. Browser/selector warnings and gap rows were tied to visible ranges, and instruction columns aligned. Pre-implementation GDB 18.1 probes inspected native ELF and a Cortex-M4 Thumb object offline, including zero data and unavailable section-query behavior; these were not physical-board tests. Probe artifacts: build/m08-disassembly-probe.mi, build/m08-zero-bytes-probe.mi and build/m08-arm-disassembly-probe.mi.

| Exact command | Outcome | Local evidence |
|---|---|---|
| `make -j4 test test-native` | PASS | `build/m08-review-all.log` |
| `make -j4 CC=clang CXX=clang++ BUILD_DIR=build/m08-clang test test-native` | PASS | `build/m08-review-clang.log` |
| `make sanitize sanitize-native` alongside other builds | FAILED at the existing controller test's 35 ms deadline assertion, not a sanitizer runtime diagnostic | `build/m08-review-sanitize.log` |
| `make sanitize sanitize-native` isolated rerun | PASS, ASan/UBSan with leak checks | `build/m08-review-sanitize-isolated.log` |
| `make analyze` | PASS | `build/m08-review-analyze.log` |
| `make test-tmux` | PASS, isolated tmux input/layout regression | `build/m08-review-tmux.log` |
| `git diff --check` before code delivery | PASS | Terminal output |

## Post-M08

Pane-local add/remove membership, selected-view promotion and last-tab protection were accepted at 0023150. The corrected menu-targeting workspace tests and native suite were reported passing. No additional standalone transcript is inferred.

## M09

The first native test failed selecting frame 1 because it assumed a caller above main. The corrected fixture stops in code_callee/main before caller selection; the focused native/TUI result was reported passing. Full-suite, sanitizer, analyzer and board passes were not individually supplied for M09. Its implementation hash is not recorded.

| Command / evidence source | Outcome |
|---|---|
| Agent: strict `make -j4 all` and focused register/native/TUI executable builds | PASS; `/tmp/the-bugger-m09-build.log` and terminal output |
| Agent: `build/test-registers "$PWD/build/fake-mi"` | PASS; synthetic x86/ARM names, sparse IDs, 600-name cap, long/unavailable values, formats, stale replies, frame/thread baselines, errors and bounded idle/running/hidden work |
| Agent: `git diff --check` before final code delivery | PASS |
| Owner: first `make test-m09-native` | FAILED at caller-frame selection: test assumed a caller above main |
| Owner: corrected `make test-m09-native`, reported "All set" | PASS reported; real-GDB and PTY focused acceptance |

## M10

Earlier test assumptions failed on GDB partial-read boundary behavior, transient status text, pane hit regions and a source-only assertion matching unrelated STALE state. The corrected inline viewer and adapter-supplied region/byte-order model passed the listed focused/regression checks. Explicit Go/Refresh and unknown/MMIO boundaries remain deliberate; native results do not prove peripheral safety.

| Agent-run command | Result and evidence |
|---|---|
| `make -s -j4 all build/test-memory build/fake-mi build/test-memory-native build/test-memory-tui` | PASS, strict GCC C17 build |
| `make test-m10-model test-m10-native` | PASS, `/tmp/the-bugger-m10-inline-focused.log`; inline Go/Enter, scalar expressions, hex/ASCII, bounded keyboard/wheel browsing, independent drafts, target-neutral region/byte-order cases and native partial reads |
| `make test test-native test-tmux` | PASS, `/tmp/the-bugger-m10-inline-regression.log`; existing native/parser/controller/context/code/break/register/layout/terminal and tmux forwarding regressions |
| `TB_MEMORY_TEST_TMUX=1 build/test-memory-tui "$PWD/build/the-bugger" "$PWD/build/memory-fixture"` | PASS, `/tmp/the-bugger-m10-inline-tmux.log`; full live Memory workflow in an isolated mouse-enabled tmux server |
| `ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 make -s -j4 BUILD_DIR=build/m10-sanitize CC=clang CFLAGS='-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer' LDFLAGS='-fsanitize=address,undefined' test-m10-model test-m10-native` | PASS, `/tmp/the-bugger-m10-inline-sanitize.log`; strict Clang build and focused ASan/UBSan/leak checks, without an unrelated full sanitizer matrix |
| `git diff --check` | PASS before handoff |

## M11

Recorded acceptance: 2026-10-18.

Failed native assertions exposed change-baseline invalidation and C++ access-group expansion assumptions. TUI failures involved demo startup, redraw waits and raw-output capture exhaustion; the bounded reader and completed-operation checks were corrected. A type-buffer initialization defect and style findings were fixed before final focused runs. Full-project analysis still reports Memory/native-map/older TUI findings assigned to M14; a focused variables-module pass does not clear them.

| Agent-run command | Result and evidence |
|---|---|
| `make test-m11-model test-m11-native` | PASS after the final initialization fix; `build/m11-final.log`. C peer policy/bounds/stale/cleanup cases, actual GDB C/C++/optimized fixtures and live TUI actions. Strict compiler warnings enabled. |
| `make test test-native` | PASS; `build/m11-regression.log`. Full existing unit/PTY/native regressions, before the final variable-type buffer initialization correction; focused tests were repeated afterward. |
| `env ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 make -s -j4 BUILD_DIR=build/m11-sanitize CC=clang CFLAGS='-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer' LDFLAGS='-fsanitize=address,undefined' test-m11-model test-m11-native` | PASS after final correction; `build/m11-sanitize.log`. Focused ASan/UBSan/leak checks, not a full sanitizer matrix. |
| `make test-tmux` | PASS; `build/m11-tmux.log`. Existing isolated-server forwarding/layout/input regression, not the complete live M11 workflow under tmux. |
| `make analyze` | FAIL; `build/m11-analyze.log`. Found M11 type-buffer initialization and style diagnostics, subsequently fixed; existing findings remain in Memory, native mapping parsing and older TUI register/Memory row calculations. Not counted as PASS. |
| `clang-tidy src/model/variables.c --checks='-*,clang-analyzer-*,bugprone-*,-clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling' --warnings-as-errors='*' -- -Iinclude -Isrc/model -Isrc/gdb -std=c17` | PASS after correction; `build/m11-variables-analyze.log`. Does not clear full-project analyzer findings. |
| `git diff --check` | PASS at implementation delivery; documentation checked separately at handoff. |

## M12

Recorded acceptance: 2026-10-18.

Acceptance preceded individually reported transcripts for some requested suites. Do not infer a slow-suite, full-project analyzer or full sanitizer pass. The later M13 full native run includes passing M12 native/TUI checks after harness targeting corrections. Hardware slot, width, alignment and stop behavior on ARM/Xtensa/RISC-V remain unverified. Creation may read watched storage and does not grant automatic variable inspection.

| Evidence | Result |
|---|---|
| Agent: strict `make -s -j4 all test-m12-model build/test-watchpoints-native build/test-watchpoints-tui build/test-break-tui` | PASS; C17 warning-clean build and focused C peer tests. Covers three modes, width/overflow validation, hits/scope expiry, fallback/downgrade removal, resource errors, context races, malformed replies, uncertain ownership and blocked cleanup without retry spin. Native/TUI binaries were compiled, not executed by the agent. |
| Agent: no-inferior GDB/MI syntax probe for `-break-watch --language c missing_symbol` | GDB accepted the command/options and returned the expected missing-symbol-table error; no target ran. Not hardware evidence. |
| Agent: `git diff --check` | PASS at implementation delivery; repeated for documentation handoff. |
| Owner command supplied: `make test-m12-native test-m12-slow` | Execution/result not individually reported before acceptance. Native hardware/TUI paths and delayed-reply/timeout recovery have tests, but no PASS transcript is claimed here. |
| Owner command supplied: `make test test-native test-tmux` | Execution/result not individually reported before acceptance. |

## M13

Initial broad runs passed default/profile-native tests but failed M12 TUI targeting: upper-pane Actions during redraw, the Memory column heading instead of input, and a body hint instead of Manage. The final helper selects the intended controls and waits for live bytes without weakening application safety. The final full regression and focused ASan/UBSan runs passed. The first sandbox native attempt was blocked by ptrace, not counted as PASS. Full-project analysis and physical embedded evidence remain pending.

| Agent-run command | Result / evidence |
|---|---|
| `make test-m13-model` | PASS; strict GCC build, JSON/XDG/atomic failure/CRUD/keys/bounds and fake-MI ordered source restore/error/interlock coverage. |
| `make test-m12-tui` | First sandbox attempt blocked by ptrace; outside sandbox reproduced the Manage misclick. PASS after final helper correction; actual live watchpoint creation, hits, management, Memory and variable actions. |
| `make test test-native test-tmux` | PASS after final corrections; `build/m13-regression.log`. Existing parser/controller/inspection/layout/terminal regressions, actual GDB native/profile tests and real isolated tmux forwarding. |
| `ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 make -j4 BUILD_DIR=build/m13-sanitize CC=clang CFLAGS='-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer' LDFLAGS='-fsanitize=address,undefined' test-m13-model test-m13-native` | PASS; `build/m13-sanitize.log`. Focused ASan/UBSan/leak and strict Clang checks, actual relocated-source/breakpoint and profile TUI flows. Not a full sanitizer matrix. |
| `clang-tidy src/session/profiles.c --checks='-*,clang-analyzer-*,bugprone-*,-clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling' --warnings-as-errors='*' -- -Iinclude -Isrc/session -std=c17` | PASS; `build/m13-profiles-analyze.log`. Focused new module only; remaining full-project findings stay assigned to M14. |
| `git diff --check` | PASS at implementation delivery; repeated for documentation. |

## M14 — native integration acceptance

Accepted through implementation commit `1dee59f33323d37af9739ce846f1a676ea56d2d5`
(`fix: harden native inspection for M14`). Individual manual checklist outcomes
were not separately reported; acceptance does not supply unrecorded measurements
or physical hardware results.

The first owner-run integrated test failed because the harness expected Continue
to stop at main. Continue correctly runs until a real stop. The harness now
requests `start` explicitly and includes retained logs in stop-timeout diagnostics;
application stop/read assertions are preserved. A sandbox rerun was blocked by
ptrace and is not a PASS. Outside the sandbox, `make -s test-m14-native` passed:
three integrated C/C++ sessions, recursive/optimized/stripped context, explicit
argument reads and terminal interaction. Maximum observed pump duration was
11 ms with a requested 10 ms wait; frontend idle CPU was 10.84–11.40 ms per
roughly one-second observation on Linux 7.2.9-arch1-1 x86-64.

Names-only automatic Call Stack arguments and deliberate selected-frame Read
Args replace page-wide automatic value evaluation. Native setup suppresses
implicit frame-argument values. Checked native mapping parsing and Memory/TUI
arithmetic corrections address the recorded analyzer findings; profile rendering
also guards a possibly missing profile. The request-policy inventory is in
[architecture](architecture.md#value-producing-request-audit).

| Execution | Result / scope |
|---|---|
| Agent: strict `make -s -j4 all build/test-context build/fake-mi build/test-native-words build/test-acceptance-native build/test-context-native build/test-context-tui` | PASS; C17 build. Native/terminal binaries compiled, not executed. |
| Agent: `make -s test-m14-model` | PASS; names-only argument encodings, unsolicited values ignored, selected-frame read, overlapping/hidden/running denial, missing/error replies, refresh/resume obsolescence and no idle retries. |
| Agent: `build/test-native-words` after adding actual `/proc/self/maps` validation | PASS; real host stack mapping recognized, malformed/overflow/truncated/device/unknown-special mappings rejected. No inferior launched. |
| Agent: focused clang-tidy on `context.c`, `memory.c`, `native.c` and `app.c`, using the repository analyzer/bugprone checks and warnings as errors | PASS after corrections; logs `/tmp/the-bugger-m14-analyze.log`, `/tmp/the-bugger-m14-context-analyze.log`, `/tmp/the-bugger-m14-tui-analyze.log`. Initial focused runs found arithmetic conversions, possible null profile access and a nested stream-read error path; these were corrected. This is not a full-project analyzer pass. |
| Agent: local documentation link/anchor/fence check and `git diff --check` | PASS; 93 local links/anchors before this evidence/roadmap update. |
| Agent: `make -s test test-native test-tmux test-m12-slow analyze` | Default/native/tmux and slow recovery goals PASS; `build/m14-regression.log`. The command then failed on the analyzer's adjacent watchpoint width/mode API warning. Existing validated API retained with a narrowly justified local suppression. |
| Agent: `make -s analyze` after that correction | PASS; full project, warnings as errors; `build/m14-analyze.log`. Clears the outstanding full-project analyzer gate for these changes. |
| Agent: `make -s CC=clang CXX=clang++ BUILD_DIR=build/m14-clang test test-native test-tmux` | Default tests PASS; initial native run failed on the harness's assumption that optimized recursive frames always retain argument metadata. Direct GDB MI inspection confirmed `args=[]` for Clang's optimized recursive frame and a named depth argument in tail_bridge. Test now selects a frame with reported depth metadata and retains required name/value/invalidation assertions. `build/m14-clang.log`. |
| Agent: `make -s CC=clang CXX=clang++ BUILD_DIR=build/m14-clang test-native test-tmux` after that correction | PASS; `build/m14-clang-native.log`. Actual C/C++/optimized/stripped/attach, context/read-policy, native integration and isolated tmux coverage. |
| Agent: corrected GCC `build/test-context-native` against recursive/optimized/stripped fixtures | PASS; `build/m14-context-native.log`; verifies the compiler-portable assertion retains GCC coverage. |
| Agent: `ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 make -s CC=clang CXX=clang++ BUILD_DIR=build/m14-sanitize CFLAGS='-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer' LDFLAGS='-fsanitize=address,undefined' test test-native` | PASS; full default/native ASan/UBSan and leak-detection matrix, including repeated integrated C/C++ sessions and actual terminal interaction; `build/m14-sanitize.log`. |
| Acceptance / physical hardware | Accepted through the implementation commit. Individual manual outcomes were not separately reported. No embedded hardware result claimed. |

The integrated native test participates in `make test-native`. Runtime measurements
above cover frontend pumping/idle CPU, not terminal input-to-redraw latency or
GDB CPU. Test host: Intel Core i5-8265U, Linux 7.2.9-arch1-1 x86-64, GCC 16.2.1,
Clang 23.1.1, GDB 18.1 and tmux 3.8. The direct/tmux manual procedure remains
available for reproduction; individual outcomes and terminal input-to-redraw
measurements were not separately reported. Hardware limitations remain unchanged.


## M15 — existing remote server

Status: **accepted** through implementation commit
`75435199e498cc087941cd8a75d7232b65d4c720` (`feat: connect to existing GDB servers`).
The implementation was committed and pushed. Individual manual checklist outcomes
were not separately reported; acceptance does not invent those results. OpenOCD
management and physical embedded validation remain separate gates.

Implemented shared `TbSession` with source-compatible native aliases, guarded
local process ownership, reviewed remote/extended-remote TUI connections and
explicit capability/unknown-region handling. Server IDs never become local PIDs.
Metadata mismatch/refusal/timeout/uncertain disconnect closes only owned GDB.
Remote inferior loss revokes Memory evidence and stops inspector pumping.

Focused checks executed by the coding agent:

| Command | Result and boundary |
|---|---|
| `make -s -j4 all build/test-remote-native build/test-remote build/test-memory build/fake-mi` | PASS strict build of changed application/model and harness paths. |
| `make -s test-m15-model` | PASS endpoint/ID/ARM/big-endian/unknown/non-octet/width bounds, ordinary RAM/Flash browsing, refusal, timeout, cancellation, transport/disconnect failure and symbol-machine mismatch. Synthetic evidence only. |
| `build/test-remote-native /home/gnix/developer/the-bugger/build/remote-fixture --tui /home/gnix/developer/the-bugger/build/the-bugger` | PASS actual localhost gdbserver: extended attach, bounded Memory, both protocol reconnects, optional configured symbols, server/fixture survival after stopped/running frontend close, server loss/revoked reads, refused reconnect, live TUI/review/resize/disconnect and terminal restoration. |
| `build/test-native-words` | PASS actual native maps and structured words through retained source API. |
| `build/test-memory build/fake-mi` | PASS existing bounded Memory policy/partial/stale/session-reset regressions. |
| `clang-tidy src/session/session.c src/session/native.c src/model/memory.c src/gdb/controller.c` with repository analyzer/bugprone checks, warnings-as-errors and normal includes | PASS focused modules; not a full-project analyzer result. Only existing suppressions/system diagnostics excluded. |

A final focused analyzer pass of `src/session/session.c` and `src/tui/app.c`
also passed with the same checks after the running-close corrections.

Corrections during focused validation: socket creation was unavailable in the
restricted environment; actual localhost validation passed with the required
socket/ptrace access. An initial harness context request was replaced with a
visible context consumer because the code model owns the request flag; actual
context assertions were retained. The live UI test exposed an obsolete review
label, corrected in the application. Numeric Memory locations now undergo the
same target-width bounds as expression results. A synthetic RAM/Flash response
initially omitted MI's required end address; its malformed fixture was corrected
without weakening parser validation. Running close initially failed because
GDB rejects disconnect while running: production close now issues one interrupt
and waits for the typed stop, with bounded failure rather than retrying forever.
The strengthened test then exposed a harness process-group issue: isolated GDB
protocol probes showed interrupt was acknowledged without a stop for a fixture
sharing the harness group, while a separate group received the stop. The harness
now launches children in separate process groups; disconnect and survival
assertions remain intact and the final actual run passes.

Quick documentation checks passed: local Markdown link targets, balanced code
fences, retained M00–M18/F01–F47 identifiers and `git diff --check`.

Full regression validation executed by the coding agent after the final fixes:

| Command | Result / local artifact |
|---|---|
| `make test test-native test-tmux test-m15-native` | PASS, exit 0; `build/m15-gcc-validation.log`. Full default/native, isolated tmux and actual remote API/TUI suites. |
| `make CC=clang CXX=clang++ BUILD_DIR=build/clang-m15 test test-native test-m15-native` | PASS, exit 0; `build/m15-clang-validation.log`. Separate strict Clang build and full default/native/remote regressions. |
| `make CC=clang CXX=clang++ BUILD_DIR=build/clang-m15 test-tmux` | PASS, exit 0; `build/m15-clang-tmux.log`. |
| `ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 make CC=clang CXX=clang++ BUILD_DIR=build/m15-sanitize CFLAGS='-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer' LDFLAGS='-fsanitize=address,undefined' test test-native test-m15-native` | PASS, exit 0; `build/m15-sanitizer-validation.log`. Full default/native/remote frontend and harness instrumentation with leak detection; target fixtures remain uninstrumented. No sanitizer failures reported. |
| `make analyze` | PASS, exit 0; `build/m15-analysis.log`. All 18 application translation units, repository analyzer/bugprone checks and warnings-as-errors; existing NOLINT and non-user-code exclusions retained. |

Test host/tools: Linux 7.2.9-arch1-1 x86-64, glibc 2.44, GCC 16.2.1,
Clang 23.1.1, GDB/gdbserver 18.1 and tmux 3.8. No application/test fixes were
needed during this full matrix. Earlier focused failures and corrections above
remain part of the record. Automated success does not establish manual acceptance.

Physical board evidence remains outstanding. No new physical hardware
compatibility is claimed; actual server evidence is limited to the documented
localhost GDB/gdbserver setup. Configured ELF metadata does not verify runtime firmware identity or
relocation; default remote region evidence is empty. Explicit bounded inspection
remains available while automatic unknown/MMIO reads remain denied.


## M16 — OpenOCD wizard, lifecycle and actions

Status: **accepted** through implementation commit
`18e460ddd4cbc897409e2879c03ed0fc983212df`. Commit/push was reported by the
repository owner. Individual manual checklist outcomes were not separately
reported. Physical-board compatibility remains M17.

Implemented bounded installed-script browsing/filtering, reviewed literal argv,
board versus adapter/target selection and ordered explicit scripts/search paths.
The shared session manages only its own OpenOCD child, waits for its listener
before GDB connection and retains logs for startup/USB/script/port failures.
Explicit Reset/Halt flushes GDB register/frame caches and verifies a fresh stopped
PC from structured metadata. Pending operations interlock execution. Errors leave
inspection unavailable and target disposition unverified. Close offers halted or
resume policy, with backend/script limitations and owned-only cleanup.

Focused checks executed by the coding agent:

| Check | Result / boundary |
|---|---|
| Strict C17 build and `make test-m16-model` | PASS; `build/m16-build.log`. Metadata catalog, board dependency exclusion, explicit argv/search order, literal paths, missing/duplicate configurations and external-server mode. |
| `make test-m16-server test-m16-slow` | PASS; `build/m16-server-validation.log` records the successful lifecycle/timeout cases before the initial TUI failure. Authored fake server/MI peers, bounded output, process cleanup, startup timeout, action errors and close choices; no hardware evidence. |
| Final `make test-m16-server test-m16-tui` | PASS; `build/m16-final-focused.log`. Final cache-flush/stop-verification failure cases, execution interlocks, unverified close disposition, real PTY filter/review/start/Reset/Halt/close/resize/signal paths. |
| `make test-m16-openocd` | PASS; `build/m16-openocd-validation.log`. Actual installed OpenOCD processes ordered Tcl scripts/search paths containing spaces and deliberately fails before adapter initialization; no hardware or connected target is exercised. |
| Focused clang-tidy on OpenOCD/session/controller/TUI | Initial findings corrected: explicit string comparison, duplicate endpoint branch, signed descriptor flags and guarded session-array indexing. Full-project result is recorded below. |

The initial terminal test clicked filter-summary text instead of the matching
script row. Screen evidence showed the actual row was present; the test now
clicks the row's path, retaining selection and configuration assertions. A later
endpoint-ownership assertion initially ran against a stale session object while
source/tests were being finalized (`build/m16-gcc-validation.log`, assertion at
`test_openocd_server.c:77`). Source/object timestamps confirmed the mismatch.
Fresh rebuilding retained the assertion and passed; the finalized full matrix
uses separate logs. No application assertion or memory-access protection was
removed to obtain these passes.

Known boundaries: generic catalog roots require explicit search paths for custom
installations; managed servers use local IPv4, with arbitrary hosts/IPv6 available
in existing-server mode. No automatic programming/reset/resume commands are added,
but OpenOCD initialization and selected Tcl scripts can affect the target. A
listener is not proof of target readiness. Physical reset/halt/disconnect behavior,
RAM/Flash correctness and probe-loss recovery remain UNVERIFIED until M17's named
board/probe/GDB/server checks. No automatic MMIO authorization is inferred.

Regression validation executed by the coding agent on Linux 7.2.9-arch1-1
x86-64, GCC 16.2.1, Clang 23.1.1, GDB/gdbserver 18.1, tmux 3.8 and OpenOCD
0.12.0-01004-g9ea7f3d64-dirty:

| Command / configuration | Result and local artifact |
|---|---|
| `make test test-native test-tmux test-m15-native test-m16-server test-m16-tui test-m16-slow test-m16-openocd` | PASS; `build/m16-gcc-final.log`. Includes native launch/attach, context, code, breakpoints/watchpoints, registers, Memory, variables, profiles and actual external gdbserver regressions. |
| Same targets with `CC=clang CXX=clang++ BUILD_DIR=build/clang-m16` | PASS; `build/m16-clang-validation.log`. |
| `test test-native test-m15-native test-m16-server test-m16-tui test-m16-slow test-m16-openocd` with `CC=clang CXX=clang++ BUILD_DIR=build/m16-sanitize CFLAGS='-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer' LDFLAGS='-fsanitize=address,undefined'`, `ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1` | PASS; `build/m16-sanitizer-validation.log`; no sanitizer/leak diagnostics. |
| Final `make test-m16-server test-m16-tui test-m15-native` | PASS; `build/m16-final-correction.log`. Explicit Resume correction and rejected-resume failure/cleanup, with remote regressions retained. |
| Final `test-m16-server test-m16-tui` using the Clang and sanitizer configurations above | PASS; `build/m16-clang-correction.log` and `build/m16-sanitizer-correction.log`. |
| `make test-m12-slow` | PASS; `build/m16-watchpoint-recovery.log`; shared-controller recovery, uncertain ownership and blocked-cleanup regressions. |
| `make analyze` | PASS across all 19 application translation units; `build/m16-final-correction.log`. No unsuppressed diagnostics under the repository analyzer configuration. |

After the full matrices, protocol review of
[OpenOCD's detach handler](https://openocd.org/doc/doxygen/html/gdb__server_8c_source.html)
identified an incorrect assumption in the initial Resume implementation: detach
acknowledgement alone does not request target execution. Resume now explicitly
issues `monitor resume` before disconnect. The authored MI tests require that
command, reject reliance on detach and cover resume errors. The final affected
GCC/Clang/sanitizer suites above were rerun after this correction; the earlier full
matrix logs describe the preceding revision. Physical disposition can still be
affected by scripts and requires M17 verification.

Documentation consistency checks passed: 10 Markdown files, 99 relative links,
balanced fenced blocks, retained roadmap/requirement identifiers and
`git diff --check`. Acceptance is established by the implementation commit;
physical-board results remain UNVERIFIED.

## M17 — Physical-board validation

Status: **accepted** through implementation commit
`f568bd2482b2309e3534ebc8e197854ada37bf77`; implementation commit/push was
reported by the repository owner. Final scope covers NUCLEO-F446RE/ARM and
ESP32-S3/Xtensa. The broader manual acceptance session is deferred to M18;
individual manual checklist outcomes are not inferred from this acceptance.
The every-release matrix and v0.1.0 release acceptance remain separate.

### Hardware and fixtures

| Setup | Recorded configuration |
|---|---|
| NUCLEO-F446RE | Cortex-M4 r0p1; onboard ST-LINK/V2.1, serial `066CFF495154787867161137`, firmware V2J48M35; direct DAP/SWD through `tests/fixtures/embedded/openocd/nucleo-f446re.cfg`. OpenOCD `0.12.0-01004-g9ea7f3d64-dirty`, ARM GCC 16.2.0 and ARM GDB 18.1. |
| ESP32-S3 | Chip revision v0.2, dual Xtensa LX7; native USB/JTAG serial `84:FC:E6:64:27:EC`; `tests/fixtures/embedded/openocd/esp32s3.cfg` with hardware-thread awareness. ESP-IDF v6.1, toolchain `esp15.2.0_20251204`, Xtensa GDB `17.1_20260402`, Espressif OpenOCD `v0.12.0-esp32-20260703`. |

The authored fixtures expose nested calls, explicit locals/aggregate reads, a
counter, SRAM ASCII buffer and Flash string. Programming was separately approved
and performed externally with verification. Default fixtures do not configure
GPIO; the separate STM32 LED variant was built/inspected offline only. No frontend
programming feature, security-eFuse modification or automatic MMIO permission is
introduced. Native Linux remains regression-tested.

Recorded SHA-256 identities:

- STM32 loaded symbol ELF: `e05d162fb4fe1a130c25c257e0dbd3435bb7bd4b71448e425b568956d67af1ed`.
  The originally programmed ELF was `66aafe4d81cee4efeb277ab64904b4b5a4c91516efd241402d96e74c0e75cef5`;
  rebuilding changed debug metadata. Actual GDB `compare-sections .vectors`
  and `.text` matched the current ELF to the board; this does not claim that
  the complete rebuilt ELF was programmed. Artifact: `build/m17/stm32-image-identity.log`.
- S3 symbol ELF: `29d12b3bc5c52e5b0435f49c8af9c4dad40610b532021feec59414cabfd35c50`;
  application binary: `f4da1cffca36eb6a675f797363d7d889dc9ff3e36025e477a6cd91d1b4ce7476`.
  Actual GDB comparisons of `.flash.text` and `.flash.rodata` matched;
  artifact: `build/m17/esp32s3-image-identity.log`.

### Executed validation

All checks below were executed by the coding agent. Host: Linux
7.2.9-arch1-1 x86-64, GCC 16.2.1, Clang 23.1.1 and tmux 3.8.
The hardware runner commands and manual procedures are in
[testing](testing.md#physical-board-core-workflow-m17).

| Check | Result / local artifact |
|---|---|
| `make all test-m17-offline` | PASS; strict build, both STM32 ELF variants, reset vectors, SRAM/stack/DWARF and offline ARM GDB source/disassembly. `build/m17/offline-final.log`. |
| `make test test-native test-tmux test-m15-native test-m16-server test-m16-tui test-m12-slow` | PASS; default/native/remote/terminal regressions and monitor-verification failure cases. `build/m17/gcc-regression.log`. |
| `make CC=clang CXX=clang++ BUILD_DIR=build/clang-m17 all test-m17-offline test-m16-server test-m12-slow` | PASS; focused strict Clang build and recovery regressions. `build/m17/clang-focused.log`. |
| `test test-native test-m15-native test-m16-server test-m16-tui test-m12-slow` with `CC=clang CXX=clang++ BUILD_DIR=build/m17-sanitize CFLAGS='-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer' LDFLAGS='-fsanitize=address,undefined'`, `ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1` | PASS; full default/native and focused remote/OpenOCD regressions, with leak detection. `build/m17/sanitizer-regression.log`. |
| `make analyze` and focused harness clang-tidy using the repository analyzer/bugprone checks with warnings-as-errors | PASS after harness corrections; no unsuppressed diagnostics. `build/m17/analyze.log`, `analyze-harness-final.log`. |
| Actual `test-board GDB ENDPOINT ELF ARCH --reset-halt` on each named board | PASS; `build/m17/{stm32,esp32s3}-board-reconnected-1.log` and corresponding `-session-reconnected-1.log`. |
| ASan/UBSan/leak-instrumented `test-board` on each board | PASS; `build/m17/{stm32,esp32s3}-board-sanitized-reconnected.log`. |
| Actual `test-board-tui` on each board | PASS; `build/m17/{stm32,esp32s3}-tui-reconnected.log`. |
| Instrumented frontend and terminal harness with `TB_BOARD_TMUX=1` on each board | PASS; `build/m17/{stm32,esp32s3}-tmux-sanitized-reconnected.log`. |

The hardware assertions cover source/disassembly, discovered PC/SP and stopped-PC
agreement, two independent SRAM/Flash snapshots with expected bytes/ASCII, explicit
Locals/aggregate reads, caller selection without moving execution, source and
instruction stepping/Step Out, hardware code breakpoints and classified data
watchpoint hits. Resume-disconnect advances the physical counter; leave-halted
retains it after reconnect. Reset requires a fresh stopped context. PTY/tmux checks
cover review/connect, actual instruction stepping, inline Memory, reset cancellation,
resize and terminal restoration. Repeated sessions retained external servers;
explicit final cleanup verified no OpenOCD/GDB/GDBserver processes remained
(`build/m17/cleanup-reconnected.log`).

### Findings and evidence boundaries

S3 reset with FreeRTOS-aware GDB task contexts exposed a production defect:
stopped threads alone could confirm an action after the selected task vanished,
without a valid current frame/PC. The controller now requires a reported current
thread with a numeric frame address for explicit monitor verification. Fake-MI
regressions retain failure assertions for missing current threads, missing frames
and unavailable addresses. Normal aborted-execution recovery stays state-only.
The tested S3 configuration uses explicit core-thread awareness across reset;
it does not implement an RTOS task inspector.

The installed STM32 HLA configuration produced an unclassified SIGTRAP for a
hardware watchpoint, reproduced with GDB alone. Direct ST-LINK DAP/SWD supplied
classified watchpoint stops and passed the retained hit-count assertions. This is
a configuration boundary, not evidence that every ST-LINK/OpenOCD combination
reports the same reasons.

Harness corrections preserved application assertions: the register wait must
include fresh SP as well as PC; watchpoint checks use the retained typed event and
actual hit count rather than transient status text; TUI clicks target the actual
managed-server toggle. Reusing existing Save Log paths caused two final-run
failures after hardware assertions completed; fresh non-overwriting paths passed.
Initial harness static-analysis findings in feature macros, parameter ordering and
unsigned bit operations were corrected before the final passing checks.

This record does not establish physical probe-removal/recovery, optional LED
behavior, exhaustive manual appearance/usability, or every profile/split workflow
on hardware. Those manual checks remain for M18. Firmware identity was checked
externally; the frontend does not automatically verify flashed-image identity.
No unlisted board, architecture, probe or acquisition feature gains a support
claim from these results. Documentation finalization uses consistency checks;
application suites were not rerun for the documentation-only commit.

## M18 — Release preparation and requirements audit

Status: **awaiting-user**. A Linux x86-64 host distribution and corresponding
source archive are prepared; no release is published or tagged. The integrated
preparation revision on `develop` is
`ad606dc6d7af7a3384401fb97e7c328ea1d8b62b`, including the CI corrections below.
Physical RISC-V
acceptance is explicitly deferred for v0.1.0; the accepted release matrix is
native Linux x86-64, NUCLEO-F446RE and ESP32-S3. This deferral does not establish
RISC-V hardware compatibility or waive future release requirements.

### Executed checks

The coding agent ran these checks with authorization for M18. Host: Linux
7.2.9-arch1-1 x86-64, glibc 2.44, GCC 16.2.1, Clang 23.1.1, GDB/gdbserver 18.1,
tmux 3.8. Instrumented target fixtures remain excluded from sanitizer flags;
frontend and harness leak detection stays enabled.

| Command / check | Result / local artifact |
|---|---|
| `make BUILD_DIR=build/m18-gcc all test test-native test-tmux test-m15-native test-m16-server test-m16-tui test-m16-slow test-m16-openocd test-m12-slow` | PASS, exit 0; `build/m18/gcc.log`. Clean strict GCC build, full default/native/tmux, actual localhost remote and OpenOCD lifecycle/recovery checks. |
| Same targets with `CC=clang CXX=clang++ BUILD_DIR=build/m18-clang` | PASS, exit 0; `build/m18/clang.log`. |
| `test test-native test-m15-native test-m16-server test-m16-tui test-m16-slow test-m16-openocd test-m12-slow` with `CC=clang CXX=clang++ BUILD_DIR=build/m18-sanitize CFLAGS='-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer' LDFLAGS='-fsanitize=address,undefined'`, `ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1` | PASS, exit 0; `build/m18/sanitizers.log`. No sanitizer/leak failure reported. |
| `make analyze` | PASS, exit 0; all 19 production translation units, no unsuppressed analyzer/bugprone diagnostics. `build/m18/analyze.log`. |
| `make release` | PASS; isolated hardened build, packaged version/help checks outside the source tree. `build/m18/release-build.log`. |
| Extract source archive outside the repository; `make all` and `make fixtures` in that directory | PASS; source build and fixture build require no original objects or Git metadata. `build/m18/source-archive-build.log`, `source-fixtures.log`. |
| Existing `test-cli`, `test-tui`, `test-workspace`, `test-native-tui` and `test-code-tui` against the extracted binary from outside the repository, using source-archive fixtures | PASS, exit 0; actual PTY, native GDB and code-stepping workflows. `build/m18/packaged-smoke.log`. |
| ELF/runtime inspection; archive checksum verification; invalid executable packaging | PASS; x86-64 PIE, non-executable stack, RELRO/BIND_NOW, resolved host libraries and rejected missing executable, wrong version and ARM firmware. `build/m18/elf-inspection.log`, `runtime-libraries.log`, `checksums.log`, `invalid-package.log`. |
| System OpenOCD with an Espressif `OPENOCD_SCRIPTS` override and explicit `-s /usr/share/openocd/scripts` | PASS; resolves the matching system ST-LINK script without hardware access. `build/m18/script-root-check.log`. |

Repeated packaging with unchanged inputs produced identical archive checksums
(`build/m18/repeat-packaging.log`). Final process inspection found no remaining
OpenOCD, GDB, GDBserver or test processes. Documentation link/fence/identifier and
whitespace checks passed; release preparation did not change application code.

The optimized executable uses FORTIFY, stack protection, PIE and full RELRO.
Direct runtime dependencies are ncursesw, libelf, json-c and libc; distribution
transitive dependencies and terminfo are still required. Executable GLIBC symbols
extend through 2.38; the actual tested host is glibc 2.44. No older-distribution
compatibility is inferred from symbol inspection. No third-party runtime, GDB,
OpenOCD, vendor SDK or firmware binary is bundled.

Artifacts are `build/dist/the-bugger-v0.1.0-linux-x86_64.tar.gz`, the corresponding
`the-bugger-v0.1.0-source.tar.gz` and `SHA256SUMS`. The binary archive embeds the
source archive's checksum and the GPLv3 license. Checksums are distributed with
the artifacts rather than embedded in this source record, avoiding a circular
source-archive identity. Rebuild after final source/documentation edits.

### Functional requirement audit

“Covered” identifies actual test evidence, not an unreported manual action or
final release acceptance. Capability-specific hardware limits remain those in M17.

| Requirements | Evidence / assessment |
|---|---|
| FR-01/06/13/17 | Covered by launcher, action keys, six-pane tabs/splits/resizing, monochrome/color and direct/tmux tests; physical appearance remains manual. |
| FR-02/03 | Covered by actual native C/C++ launch, argv/cwd/env, permitted/denied attach and integrated native acceptance. |
| FR-04/05/18/30 | Covered by catalog/filter/ordered-script tests, actual localhost remote API/TUI, managed/external server failure/ownership cases and M17 physical reset/disconnect checks. |
| FR-07/08/10/15/21/22/28 | Covered by actual source/disassembly, discovered registers, frames/threads, both stepping modes, optimized/stripped and relocated sources, plus M17 ARM/Xtensa workflows. |
| FR-09/20/26 | Covered by persistent inline Memory, independent views/drafts/settings, endian/grouping, partial/error reads and bounded region authorization; M17 verifies actual SRAM/Flash bytes. |
| FR-11/23/24/25 | Covered by code/data breakpoint tests, conditions/counts, run-to cleanup and hardware policy. Native tests cover change/read/access; physical checks cover the supported watchpoint workflow, without claiming every mode on every target. |
| FR-12/19/29 | Covered by actual Console, output isolation/flood, retained diagnostics, Save Log and file-write error tests. |
| FR-14 | Covered by profile persistence, literal paths, atomic writes, conflicts/corruption, native reopen and reviewed inert/active connection settings. |
| FR-27 | Covered by C/C++/optimized Locals/Watches, bounded lazy children, explicit reads, safe-footprint authorization before dispatch and stale-context tests. |
| FR-31 | Covered for the approved v0.1.0 matrix by current native checks and accepted M17 physical evidence. Physical RISC-V acceptance remains deferred and UNVERIFIED. |

### Non-functional requirement audit

| Requirements | Assessment / limits |
|---|---|
| NFR-01/12 | Strict GCC/Clang C17 builds and independent source-archive build pass; build, runtime and packaging dependencies are documented. |
| NFR-02 | GCC integrated acceptance measured 1001–1003 ms idle windows with 5.22–5.89 ms frontend CPU and maximum pump waits of 11–14 ms. Inspector revisions remained stable. This measures idle/pump behavior, not terminal input-to-redraw latency; the under-100-ms redraw target is not claimed as measured. |
| NFR-03 | Repeated native lifecycle, timeout/EOF/flood/error, signal terminal restoration, isolated tmux, actual remote and ASan/UBSan/leak checks pass. Physical unplug/recovery remains a distinct unperformed hardware procedure. |
| NFR-04 | MI corpus, splits/chunks, malformed/limit/mutation and allocation-failure tests pass. |
| NFR-05/10/11 | Mouse/key workflows, compact/large resize, color fallbacks, labels/markers and isolated tmux pass automatically. Full visual/usability and physical-keyboard review remain manual acceptance. |
| NFR-06 | Pre-dispatch value authorization, bounded unknown/MMIO behavior, literal argv/paths, no implicit frontend programming and external ownership assertions pass. Backend initialization can affect hardware, as documented. |
| NFR-07 | Synthetic architecture/byte-order/unknown-metadata cases plus actual ARM/Xtensa evidence cover the shared design; no untested hardware compatibility is inferred. |
| NFR-08 | Missing tools, server startup/action errors, disconnect, output bounds and retained diagnostics are covered. The manual STM32 setup exposed mismatched system OpenOCD/Espressif scripts; matching-root remedies are documented. |
| NFR-09 | Existing parser/controller/session/model/TUI boundaries are preserved; packaging is isolated build infrastructure and introduces no application dependency. |

The initial release artifact is host-specific and dynamically linked, not a
universal Linux binary. Runtime firmware identity is not automatically verified
by the frontend. Optional LED behavior, physical probe removal/recovery, exhaustive
manual appearance and other boards remain outside the recorded passing evidence.
Final manual/release acceptance and owner-controlled Git/publication remain pending.

### CI and contribution finalization

Added `.github/workflows/ci.yml` with Ubuntu 24.04 **Tests** and **Static analysis**
jobs for pushes to `main`/`develop`, pull requests and manual dispatch. Tests use
actual native GDB, localhost servers and PTY/tmux fixtures; OpenOCD coverage stops
before adapter initialization. Hosted jobs do not replace physical acceptance.
Actions are pinned, repository permissions are read-only, checkout credentials
are not persisted, superseded runs are cancelled and logs are retained seven days.

Local workflow validation passed: YAML parsing with duplicate-key checks,
trigger/permission/action-pin assertions, Bash syntax for all four run blocks,
and a dry run resolving the referenced Make targets. Markdown links/fences and
whitespace checks passed. No hosted GitHub Actions execution is claimed before
the workflow is pushed. Existing M18 suite results above are local results, not
Ubuntu runner evidence; no application suites were rerun for YAML/doc changes.

Contributor guidance now describes `develop` integration, stable `main`, feature,
fix, release and hotfix branches, merge-commit traceability and CI review.
The approved finalization sequence combines pending release preparation, CI,
security policy and documentation in the first commit, followed by a separate
owner-created release commit. Final release acceptance/publication is pending.

### CI failure reproduction and toolchain correction

Initial hosted CI exposed three failures missed by workflow-only validation:
the quote parser's promoted conditional assignment triggered clang-tidy 18;
unrelated inherited descriptors reached the debugger child; and fortified GCC
rejected an ignored `freopen` result in Memory's tmux cleanup. The descriptor
failure was reproduced with a deliberately inherited non-CLOEXEC descriptor,
fixed in production spawning and covered by a regression that retains the
parent's descriptor and unrelated-child checks. Memory cleanup now checks the
redirection result. No analyzer checks or application assertions were disabled.

An isolated Ubuntu 24.04 run then exposed a separate native backend limitation:
GDB 15 splits a quoted argument containing spaces into separate literal pieces
when startup-with-shell is off. Upstream GDB 15.1/16.3 source has this whitespace
split; GDB 17.1 uses quote-aware parsing. Native launch therefore requires GDB 17+
to satisfy the existing literal-argument guarantee without enabling a shell.
The original native failure remains a failure, not evidence of compatibility.

The revised runtime job uses Ubuntu 26.04/GDB 17+; analysis remains on Ubuntu
24.04. Fortification is explicit, and the analysis job also compiles every C
test source. The Ubuntu runtime also exposed delayed variable freshness
invalidation: a pending request could leave old rows marked fresh after resume.
The production model now clears freshness before waiting for completion while
retaining object identities for correct stale-create cleanup. A deterministic
resume assertion failed before the correction and passed afterward; the native
freshness assertion remains intact. M11 model/native/TUI checks passed under
ASan/UBSan after the correction.

The optimized context fixture exposed a separate harness assumption. Direct
GDB MI reported no arguments for optimized `recursive` or `tail_bridge`, but
reported `argc`/`argv` for `main`. The explicit argument-read test now uses
reported `argc` only when optimized `depth` is absent; unoptimized deep-stack
validation still requires `depth`, and explicit-read/reset assertions remain.
The code-view test also assumed three scroll batches reached the first ELF gap.
Ubuntu's CET/PLT layout required additional scrolling. Its replacement uses a
bounded search while retaining the mandatory gap marker, menu roundtrip and
viewport assertions; the actual section layout was checked before changing it.

Final agent-run verification:

- Arch/GCC 16: before the freshness correction, the complete configured suite
  with `_FORTIFY_SOURCE=3` passed,
  as did the real Memory/tmux path and compilation of all 56 C test sources.
- Ubuntu 24.04: `make test`, clang-tidy 18 across all 19 production files,
  fortified compilation of all 56 C test sources, Memory/tmux and the remaining
  tmux/remote/OpenOCD targets passed. Full native acceptance on its GDB 15 did not.
  Analysis and all-source compilation passed again on the final code.
- Ubuntu 26.04/GCC 15.2/GDB 17.1, unprivileged runner: the final uninterrupted
  `make test test-native test-tmux test-m15-native test-m16-server test-m16-tui
  test-m16-openocd` run with `CPPFLAGS=-D_FORTIFY_SOURCE=3` passed, followed by
  `TB_MEMORY_TEST_TMUX=1 build/test-memory-tui` with its binary/fixture paths.
  Both commands exited zero: 83 PASS reports, zero failure markers.
- ASan/UBSan with leak detection: M11 model/native/TUI and M14 integrated
  acceptance/context/TUI targets passed after the corrections.
- Workflow shell syntax, package-script syntax, Markdown fences/local targets
  and `git diff --check` passed. Final process inspection found no remaining
  GDB, gdbserver, OpenOCD or checked harness processes.

Local logs are retained under ignored `build/ci-local/`: `ubuntu26-tests.log`,
`ubuntu24-analysis.log`, `variables-sanitizers.log` and `context-sanitizers.log`.
The same directory retains original fortification/native/context/gap failures,
the freshness regression before/after results and direct GDB MI evidence.

Ubuntu environments use official base archives with verified published SHA256
checksums, isolated filesystems and the host kernel; they are not GitHub-hosted
runners or physical-board evidence. Revised hosted confirmation and final release
acceptance remain pending.

### Final distribution preparation

The repository owner reported `make release` passing on the integrated `develop`
revision above: the hardened executable built, reported `the-bugger 0.1.0`, and
the packaging command prepared `build/dist/`. These generated archives are
ignored distribution outputs; they are uploaded as release assets rather than
committed to Git. A clean working tree after packaging is expected.

The release documentation identifies v0.1.0 in the changelog and links the actual
preparation revision. Final owner release acceptance, hosted confirmation and
tag/publication remain separate from successful packaging. The agent refreshed
the archives after the tracked release notes and verified their SHA-256 checksums.
The source archive contains those final tracked notes. Later content changes
require another `make release` before publication.


## F19 — Themes and configuration

Accepted through the maintainer's reported feature commit. This post-v0.1.0 work
is unreleased. Under the updated [revision-recording policy](maintaining.md#commit-organization-and-traceability),
its exact implementation reference is recorded with the next release evidence;
no placeholder hash is assigned here. Acceptance does not imply separately
reported manual visual checks.

Scope: centered Config → Profiles/Themes, seven built-in families and 37 variants,
Catppuccin Macchiato default, dependent variant controls, simulated temporary
preview, Back cancellation and private atomic global preferences. Monokai Pro
remains selectable. Upstream revisions/licenses are retained in
`THIRD_PARTY_NOTICES`, also included in binary packaging. Custom themes and
workspace redesign remain outside F19.

Agent-run implementation checks:

| Check | Recorded result |
|---|---|
| Strict `make -j2 all build/test-preferences build/test-render build/test-themes-tui` builds | PASS |
| `build/test-preferences` | PASS: default, persistence, private atomic writes, stale writers, invalid schema and file preservation |
| `build/test-render` | PASS: 37 palettes, RGB contrast, direct/256/8-color/monochrome, execution highlight and scrollbar |
| `build/test-themes-tui "$PWD/build/the-bugger"` | PASS: centered menus, all variants, dependent selection, mouse/keyboard, Back/Save/restart, no preview persistence and 48×18 monochrome |
| `build/test-profiles-tui "$PWD/build/the-bugger" "$PWD/build/debug-fixture"` | PASS: existing profile-management regression through Config → Profiles; non-native invocation |
| Focused clang-tidy with repository analyzer/bugprone checks and warnings as errors | PASS after correction; `theme.c`, `preferences.c` and `app.c` |
| `_FORTIFY_SOURCE=3`, strict warnings and `-fsyntax-only` for changed production/test C files | PASS |
| `bash -n tools/package-release.sh`; `git diff --check` | PASS |

Checks exposed a real 256-color collision between a Gruvbox light variant's border
and selected surface; the renderer now falls back to contrasting text color.
The analyzer also flagged a nullable catalog lookup; internal access now uses
the module's validated selected index. Two PTY failures were harness assumptions:
vt100 has no mouse support, and resizing resets focus to the first control. The
harness uses actual keyboard navigation and the existing resize-focus behavior;
application assertions and fallback checks remain intact.

Local artifacts: `build/the-bugger`, `build/test-preferences`, `build/test-render`,
`build/test-themes-tui`; temporary logs included `/tmp/tb-theme-final-build.log`,
`/tmp/tb-theme-analysis-final.log` and `/tmp/tb-theme-app-analysis-final.log`.
These are disposable local paths, not durable CI artifacts.

Full default/native/tmux regression suites, sanitizer/compiler matrices,
GitHub-hosted CI, physical hardware and manual all-variant terminal/tmux visual
acceptance were not run or separately reported for F19. Prior release evidence
is not a substitute. The [manual procedure](testing.md#themes-and-configuration)
remains reproducible without treating unreported observations as PASS.

## Always-on h/j/k/l navigation

Accepted through the subsequent implementation commit; unreleased. This shared
UI addition normalizes the
four lowercase letters to Left/Down/Up/Right outside editable fields, retaining
the actual arrow dispatch for controls, scrolling, tab changes and resizing.
Text fields consume literal characters first, including file-filter page reset.
No modes, extra Vim commands, target operations or configuration switch are added.

Agent-run checks:

- Strict `make -j2 all build/test-navigation`: PASS.
- `build/test-navigation "$PWD/build/the-bugger"`: PASS. Paired real demo PTYs
  compare text and inverse selection for launcher, toggles, literal fields,
  themes/variants, workspace tabs/bodies, divider resizing, confirmations and
  compact rendering. A first harness comparison sampled incomplete redraws;
  it now waits through Escape recognition and drains output for a bounded
  interval before comparing. Application assertions were retained.
- `_FORTIFY_SOURCE=3`, strict warnings and `-fsyntax-only` on `src/tui/app.c`
  and `tests/integration/test_navigation.c`: PASS.
- Documentation local-link/code-fence checks and `git diff --check`: PASS.

Artifacts: `build/test-navigation`, `/tmp/tb-navigation-build.log` and
`/tmp/tb-navigation-test-build.log`. Full native/tmux, sanitizer/static-analysis
matrices, physical hardware and manual acceptance were not run for this change.
The [manual steps](testing.md#letter-key-navigation) remain to be reviewed.


## ESP fixture warning policy

Accepted through the subsequent implementation commit; unreleased. The root
Makefile already applies `-Wall`
and `-Werror` to frontend/tests, native C/C++ fixtures, STM32 firmware, release
and sanitizer builds. Only the authored ESP-IDF component needed an explicit
policy: private `-Wall -Wextra -Werror` plus error-category overrides for inherited
unused-function, unused-variable, unused-but-set-variable,
deprecated-declarations and extra-warning exceptions. SDK components are unchanged.

Agent-run focused verification:

- Isolated CMake configure including the actual component file: PASS. Target
  options contain the strict flags; an unrelated target receives none. This
  checks component scoping, not a full ESP-IDF reconfiguration.
- Actual installed Xtensa/ESP32-S3 and RISC-V/ESP32-C6 compilers, using each cached
  ESP-IDF compile command plus the component options: PASS, both `main.c` and
  shared `workload.c` compiled to temporary objects with no emitted warnings.
- Deliberate unused-function, unused-variable, unused-but-set-variable and
  deprecation probes: each compiler accepted the inherited downgraded warning
  but rejected it with the component policy. An enabled extra-warning probe also
  fails under the strict policy; that particular diagnostic was already an error
  under the inherited policy. An initial probe incorrectly assumed otherwise;
  the expectation was corrected without weakening the component policy.
- Documentation local-link/code-fence checks and `git diff --check`: PASS.

Temporary artifacts are under `/tmp/tb-warning-policy-probe/`; configure and
result logs are `/tmp/tb-warning-policy-cmake.log` and
`/tmp/tb-warning-policy-results.log`. These are disposable local checks, not new
tracked test infrastructure. No full SDK rebuild, linking, programming, hardware
connection or application regression suite was run. C6 cross-compilation is not
physical RISC-V acceptance.

## Source fullscreen rendering

Accepted through the subsequent implementation commit; unreleased. Source Actions and the Source pane-actions
menu can temporarily expand Source into the workspace area, retaining the global
header/footer. Restore, workspace Escape/q and keyboard activation return to the
existing split tree and tabs. Hidden pane selectors, tabs and divider hit regions
are inactive. This does not implement saved layouts or expand F06.

Agent-run focused checks:

- Strict `make -j2 all build/test-fullscreen build/code-fixture` build and final
  rebuild: PASS, with the existing `-Wall -Wextra -Wpedantic -Werror` policy.
- `build/test-fullscreen "$PWD/build/the-bugger"`: PASS, including Source-only
  rendering, toolbar/footer retention, resized/nested split restoration,
  keyboard/mouse/Escape restoration, hidden-divider input and compact resize.
- `build/test-fullscreen "$PWD/build/the-bugger" "$PWD/build/code-fixture"`:
  PASS with installed GDB 18.1 outside the sandbox. Live Source Actions entry
  preserves the stopped source line, F10 advances source, restoration retains
  that stopped context and confirmed session close cleans up the owned fixture.
- Fortified strict `-fsyntax-only` checks on `app.c` and `test_fullscreen.c`: PASS.
- Focused clang-tidy on `app.c`, with repository checks/warnings as errors: PASS.
  The analyzer initially flagged padding from the additional state field; placing
  it next to render rectangles in an existing alignment gap resolved it without
  suppressing warnings or rearranging unrelated state.
- Documentation local links/anchors/fences and `git diff --check`: PASS.

The initial native sandbox run could not start the inferior/pidfd path and failed;
it is not a passed native run. The permitted unsandboxed focused rerun passed.
An initial demo harness counted the footer's ellipsis as a pane menu; counting
only the workspace area corrected the oracle without weakening the required
seven-pane restoration or zero-menu fullscreen assertions.

Local artifacts: `/tmp/tb-fullscreen-final-build.log`,
`/tmp/tb-fullscreen-demo.log`, `/tmp/tb-fullscreen-native.log`,
`/tmp/tb-fullscreen-analysis-final.log` and `build/test-fullscreen`.
Full default/native/tmux suites, sanitizer/compiler matrices, physical hardware
and manual visual acceptance were not run for this change. Follow the
[manual fullscreen procedure](testing.md#source-fullscreen); focused native
success does not establish embedded hardware compatibility.


## Spatial control navigation

Accepted through implementation commit `53b1e5a`; included in v0.1.1 preparation.
Arrow/hjkl navigation uses rendered rectangles
and stable control identities. Horizontal movement stays within a visual row;
vertical movement prefers alignment, and directions do not wrap. Tab/Shift+Tab
exclude content, scrollbars, dividers and hidden panes. Tabs activate with Enter.
Body/scrollbar clicks explicitly acquire content navigation; Escape/Tab or an
outside-pane/control click returns to controls. Fullscreen restoration and
per-view navigation retain their existing behavior. No target-read authorization,
layout structure, protocol or dependency changes are included.

Agent-run focused checks:

- Strict build with the repository warning-as-error policy: PASS.
- Paired `build/test-navigation` PTYs: PASS for toolbar directions/edges,
  control-only traversal, deliberate content focus, Escape/outside/control-click
  exit, fullscreen Quit/Restore reachability, compact resize identity and literal
  text entry; hjkl and arrow screens/selection agree.
- `build/test-workspace`, `build/test-tui`, `build/test-fullscreen`: PASS,
  retaining tab promotion, real scrolling, split/divider/compact rendering,
  terminal restoration and fullscreen behavior.
- Focused live `build/test-fullscreen` with `build/code-fixture`, and
  `build/test-memory-tui` with `build/memory-fixture`: PASS outside the sandbox,
  using owned GDB fixtures. Fullscreen source stepping and Memory inline editing,
  explicit scrolling, independent views, errors and cleanup remain functional.
- Focused clang-tidy on app/navigation test and fortified strict syntax checks
  on all five changed C files: PASS.
- Full `make analyze`: PASS with the repository analyzer/bugprone checks and
  warnings as errors. Reported suppressed system-header/existing NOLINT counts
  are not new application warnings.

The first broad run stopped in the Themes PTY test: it assumed resize reset
focus to Theme. Preserving Variant correctly made the old three-Tab sequence
activate Save. The test now asserts retained Variant focus and uses two Tabs to
reach Preview; `make test-themes` passes. Existing tab-switching/body tests were
updated to the explicit Enter/content-click interaction while retaining their
view, scroll and safety assertions. The new paired-click generator initially
sent press/release together; separating them with settled frames, matching the
existing terminal helper, fixed its input simulation. Early directional checks
also exposed a diagonal horizontal focus jump; restricting horizontal movement
to the visual row resolved that application behavior.

Final agent-run regression, explicitly authorized for this task:

```sh
make test test-native test-tmux test-m15-native test-m16-server test-m16-tui test-m16-openocd
make analyze
```

Both commands completed with exit status 0 on the local Linux host. The runtime
command ran outside the sandbox for owned GDB fixtures, localhost servers and
isolated tmux sockets. It covers the default/model/PTY suites, native acceptance
and live inspectors, real tmux workspace, actual localhost GDB-server integration,
authored OpenOCD lifecycle/TUI exchanges and actual OpenOCD script loading stopped
before hardware initialization. Existing safety and cleanup assertions passed.
Fortified syntax, documentation relative file links/code fences and
`git diff --check` also passed. No commit, tag, push or publication was performed.

Final logs: `/tmp/spatial-regression-final.log`,
`/tmp/spatial-analyze-all.log`, `/tmp/spatial-analysis-final2.log`,
`/tmp/spatial-fortify.log` and `/tmp/spatial-themes-fortify.log`.
The initial broad-run failure remains in `/tmp/spatial-regression.log`.
These logs are disposable local artifacts; this section preserves the results.
Hardware and sanitizer/compiler matrices were not run for this task and remain
separate release checks. These host tests do not establish new board compatibility.
Manual review follows the
[spatial navigation procedure](testing.md#spatial-control-and-content-navigation).


## v0.1.1 release preparation

Status: **awaiting-user**. The checkout targets `0.1.1`; the changelog entry is
prepared for 2026-10-24. No v0.1.1 tag, published release or final release acceptance
is recorded. The earlier v0.1.0 publication and physical-validation boundaries
remain historical evidence rather than fresh v0.1.1 hardware results.

Included implementation references, observed in local history:

| Change | Implementation reference |
|---|---|
| F19 themes and variants | `9079203` |
| Theme documentation and routine commit organization | `e21cc36` |
| Always-on h/j/k/l | `079d3f1` |
| Strict authored ESP fixture warnings | `0dcae74` |
| Source fullscreen | `f3eb663` |
| Spatial controls and deliberate content focus | `53b1e5a` |

Release preparation changes only the version, version-dependent tests and
release documentation. CLI and launcher tests now derive their expected version
and label width from the shared version header. The stale version-header link
now points to this register. Previously recorded broad runtime/static-analysis
results for `53b1e5a` remain in the spatial-navigation section above; they are
not relabeled as newly executed release checks.

Agent-run preparation checks:

- `make -j2 all build/test-cli build/test-tui`, CLI and launcher/terminal tests:
  PASS with the existing strict C17 warning-as-error policy; version is `0.1.1`.
- `make release`: PASS. The hardened Linux x86-64 executable and matching source
  archives are prepared under `build/dist/`; nothing is published automatically.
- SHA-256 verification, extracted binary `--version`/`--help`, GPLv3 license,
  adapted-palette notices and matching embedded source-archive hash: PASS.
- Extracted executable CLI, launcher/terminal, navigation and fullscreen PTY
  checks from outside the repository: PASS. A clean `make -j2` from the extracted
  source archive also passed and reported the same version.
- ELF x86-64, PIE, non-executable GNU stack, GNU RELRO and BIND_NOW checks: PASS.
  Host: Linux 7.2.9-arch1-1 x86_64, GCC 16.2.1, glibc 2.44. The executable records
  GLIBC symbol requirements through GLIBC_2.38; dependency compatibility on other
  distributions remains unverified. `BUILD-INFO.txt` retains dependency details.
- Missing-executable, invalid `SOURCE_DATE_EPOCH` and executable/source-version
  mismatch packaging cases: rejected as expected.
- Fortified strict syntax checks on both changed test files: PASS.
- Documentation relative file links/code fences and `git diff --check`: PASS.
  The final source archive matches all 153 tracked files in the checkout.

An additional analyzer invocation on the CLI/TUI test sources flagged the CLI
harness's intentional `execv` of its caller-selected test binary as a taint sink.
The same check reproduced on the unchanged `HEAD` CLI harness. This optional
harness analysis is not a passed check; no suppression or harness behavior change
was introduced. The canonical `make analyze` scope is production sources.

The first hosted v0.1.1 default-suite run failed at
`tests/integration/test_registers.c:83`: the hidden-register baseline check saw
`changed` after a fake stop. The test had treated the stop command's `^done`
reply as completion, although the fake peer emits `*stopped` separately. It could
make Registers visible before the stop event reached the model. The test now
waits for the controller's new stopped generation before revealing Registers;
the fake peer can hold that event after acknowledging the command to exercise
the ordering explicitly. The register model and its change-marker assertion are
unchanged. Agent-run `make -s test-m09-model`, a separate fortified
`make -s BUILD_DIR=build/ci-register CPPFLAGS=-D_FORTIFY_SOURCE=3 test-m09-model`,
and `make -s test-controller` passed. The failed hosted run remains a failure;
broader post-fix CI evidence is pending.

Artifacts to attach after final release acceptance:

- `build/dist/the-bugger-v0.1.1-linux-x86_64.tar.gz`
- `build/dist/the-bugger-v0.1.1-source.tar.gz`
- `build/dist/SHA256SUMS`

The source hash is embedded in the binary archive's `BUILD-INFO.txt`; the external
checksum asset records both final archive hashes. Rebuild after any subsequent
source/documentation edits. Private, ignored `AGENTS.md` is excluded from the
source archive. Older v0.1.0 archives may coexist in the output directory and are
not v0.1.1 assets.

Current local artifacts include `/tmp/v011-build.log`, `/tmp/v011-package.log`,
`/tmp/v011-artifacts.log`, `/tmp/v011-packaged-ui.log`,
`/tmp/v011-package-negative.log`, `/tmp/v011-fortify.log`,
`/tmp/v011-focused-analysis.log` and `/tmp/v011-analysis-baseline.log`.
The extracted build/check directories are disposable local artifacts.

The fresh host compiler/sanitizer/full-regression matrix is pending explicit run
authorization/results. Fresh physical STM32/ARM, ESP32/Xtensa and ESP32/RISC-V
release validation, manual review and final release acceptance remain pending.
The v0.1.0 RISC-V deferral does not authorize a v0.1.1 waiver. Follow the
[every-release matrix](testing.md#every-release-regression-matrix); do not infer
hardware PASS from the host or packaging checks above.

## Multi-file Source tabs

State: **automated-verified; awaiting-user**. This shared Source UI addition is
unreleased and does not establish new target compatibility. Each Source pane can
retain a Current tab and up to seven GDB-known files. Opened tabs remain pinned
across stops; the active file is read through the existing bounded local source
loader. Full path identity distinguishes duplicate basenames.

Agent-run focused checks on 2026-10-24:

- `make -s all build/test-code` and `build/test-code "$PWD/build/fake-mi"`:
  PASS, including pinned-file stop retention, duplicate-open selection, close,
  reset and invalid-tab handling.
- `build/test-code-native "$PWD/build/code-fixture" "$PWD/build/code-fixture-optimized" "$PWD/build/code-fixture-stripped" "$PWD/build/code-fixture-cpp" "$PWD/build/code-fixture-relocated" "$PWD/tests/fixtures/native/relocated"`:
  PASS outside the sandbox with real GDB, including two duplicate-basename files,
  tab selection, a later stop and closing a tab.
- `build/test-code-tui "$PWD/build/the-bugger" "$PWD/build/code-fixture" "$PWD/build/debug-fixture"`:
  PASS outside the sandbox with real GDB. The Source TUI check also switches,
  closes and reopens Current/file tabs before execution. An earlier sandbox
  invocation could not start its ptraced inferior. An initial outside-sandbox
  run found a TUI helper that selected a disassembly text match before the
  Source marker after the new tab row shifted layout. The helper now checks
  the Source gutter, and
  the rerun passed.
- `build/test-navigation "$PWD/build/the-bugger"`: PASS for shared spatial
  controls and explicit content focus. `make -s all build/test-code-tui
  build/test-navigation`, a final `make -s all build/test-code`, and
  `git diff --check`: PASS with strict warnings.

The broader default/native/tmux and static-analysis matrices were not run for
this gate. Manual multi-file use, compact tab cycling and acceptance remain
pending.

### CI correction — source catalog ordering

The hosted Ubuntu 26.04 / GDB 17.1 run for PR #19, merge revision `e6fcf0a`,
passed the default suite and then failed the native suite at
`test_code_native.c:185`. The preceding assertions verified the correct contents
for each opened file. The new tab assertion incorrectly assumed that GDB listed
`left/duplicate.c` before `right/duplicate.c`, assigning those identities to tab
1 and tab 2. The local run had used that order; the hosted run did not.

The test now finds each source by full path, records its actual tab, and runs
the relocated-source workflow with both explicit opening orders. Content,
full-path identity, stop retention, source mapping, breakpoint and close
assertions remain. Application code is unchanged by this correction.

Agent-run `make -s BUILD_DIR=build/ci-source-order CPPFLAGS=-D_FORTIFY_SOURCE=3
build/ci-source-order/test-code-native` passed with strict warnings. Running that
binary with the six fixture/source arguments recorded above passed outside the
sandbox, including left-first and right-first tab opening. `git diff --check`
also passed. The failed hosted run remains failed; native checks after this
failure, tmux and the remaining integration targets were not reached. A new
hosted run is pending.

## Spatial content-focus transfer

State: **automated-verified; awaiting-user**. Ctrl+arrows and Ctrl+h/j/k/l
transfer existing content focus to a visible neighboring pane using the rendered
layout. The transfer preserves active view tabs, viewports and the last control
identity. Directions stop at edges; compact/fullscreen rendering excludes hidden
panes. Source/Disassembly transfers update stepping mode without moving execution.
Legacy Ctrl+H/J input overlaps Backspace/line feed; those bytes are treated as
pane movement only while workspace content focus is active.

Agent-run focused checks:

- `make -s all build/test-navigation build/test-code-tui build/test-fullscreen`:
  PASS with the existing C17 warning-as-error policy.
- Final `make -s test-navigation`: PASS for Ctrl+arrow/Ctrl+hjkl equivalence,
  decoded Backspace, all four directions, edge stopping, nested split selection,
  retained viewports, Escape recovery, literal field editing and fullscreen/
  compact isolation. Local output: `build/content-focus-navigation.log`.
- `build/test-fullscreen "$PWD/build/the-bugger"`: PASS for demo rendering,
  restoration, splits/resizing and hidden hit regions.
- `build/test-code-tui "$PWD/build/the-bugger" "$PWD/build/code-fixture" "$PWD/build/debug-fixture"`:
  PASS outside the sandbox with real GDB, including Source → Disassembly → Source
  content transfer, corresponding stepping modes and unchanged PC.
- Focused `clang-tidy src/tui/app.c` with the `make analyze` analyzer/bugprone
  checks and warnings-as-errors: PASS; no unsuppressed diagnostics. Local output:
  `build/content-focus-analysis.log`.
- Fortified strict `cc -fsyntax-only` on `src/tui/app.c`,
  `tests/integration/test_navigation.c` and `tests/integration/test_code_tui.c`:
  PASS. `git diff --check`: PASS.

Full default/native/remote/tmux, sanitizer/compiler matrices and fresh physical
checks were not run for this change. Manual terminal/multiplexer key forwarding
and user acceptance remain pending; no new hardware compatibility is implied.

### CI correction — Enter versus Ctrl+J

The Tests log in `logs_108737438474.zip` records hosted merge revision
`6f461b3` failing `test-context-tui` while expecting `LIVE | T1 frame #4`.
After Down+Enter in Call Stack, content focus moved to Locals and the selected
frame remained #3. The same failure reproduced locally with the unchanged
Call Stack test. This was an application regression: curses translated Enter's
carriage return into line feed, which the new content shortcut treated as Ctrl+J.

Startup now explicitly calls `nonl()` after `raw()` to keep those inputs distinct.
The existing Call Stack frame-selection assertion remains unchanged and passed
after the fix. The navigation regression additionally checks that Enter retains
Registers content focus while Ctrl+J transfers it down to Console.

The shared `make test-ci` target contains the exact previous hosted Tests target
list; workflow triggers, job count and the analysis job remain unchanged.
Local code/test/build preflight now requires this complete baseline in a fresh
fortified build directory rather than only selected focused tests.

Agent-run verification of the uncommitted fix based on
`f591e631f2e85dc3c79c4a9a9e0240fbbba1a5ce`:

```sh
TERM=xterm-256color make BUILD_DIR=build/ci-content-enter CPPFLAGS=-D_FORTIFY_SOURCE=3 test-ci \
  > build/ci-content-enter-tests.log 2>&1
```

Result: **PASS, exit 0**, from a previously nonexistent build directory. The
complete default, native, tmux, real localhost remote, OpenOCD server/TUI and
offline real-OpenOCD target list completed. This includes the original Call Stack
keyboard-selection test, the explicit Enter/Ctrl+J navigation regression, both
duplicate-source opening orders and terminal/signal restoration. No GDB/OpenOCD
processes remained after completion. The full local log is
`build/ci-content-enter-tests.log`.

Local environment: Linux 7.2.9-arch1-1 x86_64, GCC 16.2.1, GDB 18.1, tmux 3.8,
ncursesw 6.6.20251230 and OpenOCD 0.12.0-01004-g9ea7f3d64-dirty. These are local
host results, not a replacement for the pending Ubuntu 26.04 / GDB 17.1 hosted
rerun. Workflow target-list equivalence, relative documentation file links and
`git diff --check` also passed. The archive's analysis log was not inspected;
fresh sanitizer/compiler/hardware matrices were not run for this correction.

## Inline GDB Console command entry

Implemented for owner review on 2026-10-24; acceptance and physical-board manual
checks are not implied. The separate command screen is replaced by a bounded
pane-local field below Console output, with Enter/Send, retained rejected drafts,
Escape to leave editing and unchanged deliberate GDB/MI dispatch. Save Log keeps
its destination dialog. Output scrollbars exclude the new input row.

Agent-run focused native and code TUI checks passed with real GDB, including
actual command output/errors, Source/Disassembly PC assertions, horizontal log
scrolling, keyboard-only access, pane-local drafts and compact resizing. The
new focused Console case also passed through an isolated real tmux server using
its default Escape forwarding delay. `make test-native` now includes that case.
Logs: `build/console-inline-focus-native.log`, `build/console-inline-code.log`
and `build/console-inline-tmux-synchronized.log`.

The first complete baseline failed an existing code-stepping assertion because
the test pressed F10 while the new command field still had editing focus. The
test now explicitly selects Source before stepping; its original PC assertion
is preserved and passes. The initial baseline log remains
`build/ci-console-inline-tests.log`. Early focused checks also exposed harness
assumptions: field focus was initially checked through reverse-video styling
instead of the actual caret, and the tmux test used a pre-existing Loaded label
as an Escape acknowledgement. The tests now inspect the caret and await actual
field departure without changing tmux's Escape delay. Initial focused logs are
retained under `build/console-inline-*-initial.log` and
`build/console-inline-tmux-debug.log`. A sandboxed native run could not launch
the inferior; real-GDB verification ran outside that sandbox without changing
host ptrace settings.

All 60 unit/integration/support C sources compiled successfully with
`CPPFLAGS=-D_FORTIFY_SOURCE=3` and the project's warning-as-error flags in
`build/console-inline-compilation`; the complete compiler log is
`build/console-inline-test-compilation.log`. This includes the physical-board
TUI runner's updated inline-entry expectation, but does not execute hardware.

Final complete baseline command (fresh build directory):

```sh
TERM=xterm-256color make BUILD_DIR=build/ci-console-inline-final CPPFLAGS=-D_FORTIFY_SOURCE=3 test-ci \
  > build/ci-console-inline-final-tests.log 2>&1
```

Final baseline outcome: **PASS, exit 0**. Default, native, tmux, localhost remote
and owned-server/TUI/offline OpenOCD checks completed, including both inline
Console modes and the original code-stepping assertions. The exact source/test/Makefile diff and base
revision are retained in `build/ci-console-inline-final-tree.patch` and
`build/ci-console-inline-final-base.txt`. Local environment: Linux
7.2.9-arch1-1 x86-64, GCC 16.2.1, GDB 18.1, tmux 3.8 and ncursesw
6.6.20251230. Local results do not establish hosted Ubuntu success. No fresh
physical hardware, sanitizer/compiler matrix or static-analysis run is claimed.
The final host process audit found no remaining GDB, OpenOCD or frontend processes.
The tested source/build snapshot comparison, relative documentation file-link
checks and `git diff --check` passed.

Handoff correction: the isolated test build initially left the usual
`build/the-bugger` executable stale, so the owner still saw the old Command page.
Agent rebuilt it with `make all` (exit 0), then reran the native TUI runner against
that exact default executable (exit 0), including inline entry and cleanup.
Logs: `build/console-inline-default-build.log` and
`build/console-inline-default-native.log`. Restart the application to use the
rebuilt executable; an already-running process retains its previous code.

## Terminal-size warning correction

Implemented for owner review on 2026-10-25. The warning below 48 columns or
18 rows now says `Terminal too small` in both demo and live sessions, shows
`Current: COLSxROWS`, and retains the existing minimum/state/quit instructions.
No layout threshold or target-control behavior changed.

Agent-run focused TUI and native TUI checks passed against the rebuilt normal
`build/the-bugger`. Coverage includes 15x5, width-only and height-only failures,
recovery at exactly 48x18, retained command drafts and a running native target
remaining running after warning/recovery. Logs:
`build/terminal-size-focused-build.log`, `build/terminal-size-focused-tui.log`
and `build/terminal-size-focused-native.log`. The initial native check assumed
the command field was still focused after returning from the control-free
warning; it now explicitly resumes editing after verifying the draft survived.
The original failed log is `build/terminal-size-focused-native-initial.log`.

The first complete baseline stopped at the workspace runner's remaining
expectation of `DEMO:`. That expectation now matches the deliberately changed
warning; its layout-restoration assertions remain intact. Initial full log:
`build/ci-terminal-size-tests.log`. The updated focused workspace runner passed;
its log is `build/terminal-size-focused-workspace.log`.

Final complete local baseline, with an initially nonexistent build directory:

```sh
TERM=xterm-256color make BUILD_DIR=build/ci-terminal-size-final CPPFLAGS=-D_FORTIFY_SOURCE=3 test-ci \
  > build/ci-terminal-size-final-tests.log 2>&1
```

Baseline result: **PASS, exit 0**, including default/native, real tmux,
localhost remote, owned-server/TUI and offline OpenOCD checks. Source/test diff
and base revision are recorded in
`build/ci-terminal-size-final-tree.patch` and `build/ci-terminal-size-final-base.txt`.
Environment: Linux 7.2.11-arch1-1 x86-64, GCC 16.2.1, GDB 18.1, tmux 3.8 and
ncursesw 6.6.20251230. Real-GDB checks run outside the ptrace sandbox. These are
local results, not hosted Ubuntu or fresh physical-board evidence; no additional
sanitizer/compiler matrix or static-analysis run is claimed.
The final host audit found no remaining GDB, OpenOCD or frontend processes.
The tested source snapshot, relative documentation file links and
`git diff --check` also passed.

## Personal Keybinds configuration

Implemented for owner review on 2026-10-25; **AWAITING USER ACCEPTANCE**.
Config → Keybinds edits 30 personal debugger/navigation actions with staged
validation, Back cancellation, Defaults and Save. Bindings share global
`preferences.json` with themes, preserving each other on writes. Connection
profiles contain no bindings and cannot override them. Legacy version-1 profile
keys are ignored; subsequent writes (including recents) use key-free version 2.
Older apps requiring version 1 cannot read those new stores. Terminal interception
still varies; fixed arrows/Ctrl+arrows, Tab/Shift+Tab, Escape and Ctrl+Q remain.

Agent-run focused checks passed: key parsing/matching/defaults; duplicate/reserved
keys; preference persistence, corrupt-file preservation and stale writers;
legacy-profile compatibility and key-free writes; centered editor, draft
save/cancel/defaults, compact/monochrome, literal fields and theme independence;
profile loading with global shortcuts; real GDB Continue/Pause, Console input
and content-pane transfer, directly and through an isolated real tmux server.
Existing Themes, navigation and native profile checks also passed. Artifacts:
`build/keybinds-unit-final.log`, `build/keybinds-themes.log`,
`build/keybinds-native-final.log`, `build/keybinds-tmux-final.log`.

Development failures are retained, not counted as passes. Initial mouse editor
checks exposed press/release queue ordering in the new harness; separating the
events with the existing terminal helper's settling behavior fixed the harness.
The first native run passed its debugger assertions but its later keyboard
traversal overlooked the newly created recent-profile row; the fixture now
accounts for that row. Logs: `build/keybinds-focused.log`,
`build/keybinds-native.log`. An initial attempted tmux run used the older harness
binary because compilation had not finished; only the rebuilt run with the
explicit real-tmux PASS marker in `build/keybinds-tmux-final.log` is tmux evidence.

The authorized static-analysis command initially failed on a parser increment
inside a condition and adjacent action/key parameters. The parser now advances
separately; the domain-validated index/key interface has a documented targeted
analyzer suppression. Final `make BUILD_DIR=build/keybinds-analysis analyze`
passed, exit 0; initial/final logs are `build/keybinds-analysis.log` and
`build/keybinds-analysis-final.log`. All C unit/integration/support sources also
compiled with `_FORTIFY_SOURCE=3` and strict warnings, exit 0, in the fresh
`build/keybinds-test-compilation` directory; log:
`build/keybinds-test-compilation.log`.

The complete ordinary host baseline uses a fresh directory:

```sh
TERM=xterm-256color make BUILD_DIR=build/ci-keybinds CPPFLAGS=-D_FORTIFY_SOURCE=3 test-ci \
  > build/ci-keybinds-tests.log 2>&1
```

Baseline result: **PASS, exit 0**. The shared CI target includes default,
native, real tmux, localhost remote, owned-server/TUI and offline OpenOCD suites,
plus the new editor/model and direct/tmux native keybind checks. Terminal fixtures
use isolated personal preferences; persistence tests explicitly opt into owned
fixtures through `TB_TEST_CONFIG_HOME`, never developer settings.

The source/test/build SHA-256 manifest is `build/keybinds-tested-tree.json`;
base revision and tracked diff are `build/keybinds-base.txt` and
`build/keybinds-tree.patch`. The manifest includes new untracked source/test
files. Environment: Linux 7.2.11-arch1-1 x86-64, GCC 16.2.1, GDB 18.1,
tmux 3.8, ncursesw 6.6.20251230 and clang-tidy 23.1.3; complete versions:
`build/keybinds-environment.log`. GDB runs outside the ptrace sandbox.
The normal `build/the-bugger` was rebuilt (`build/keybinds-default-build.log`).
Local results do not establish hosted Ubuntu success, physical-board acceptance
or sanitizer/compiler-matrix coverage; none of those additional runs is claimed.

Final process audit found no remaining GDB, OpenOCD or frontend processes
(`build/keybinds-process-audit.log`). The unchanged source/test/build manifest,
relative documentation file links and `git diff --check` passed. Final delivery
file hashes, including documentation, are in `build/keybinds-delivery-tree.json`.
The isolated fortified test-compilation command was:

```sh
sources=(tests/unit/*.c tests/integration/*.c tests/support/*.c)
objects=("${sources[@]/%.c/.o}")
objects=("${objects[@]/#/build/keybinds-test-compilation/}")
make BUILD_DIR=build/keybinds-test-compilation CPPFLAGS=-D_FORTIFY_SOURCE=3 "${objects[@]}"
```

## Keybinds tables and direct capture revision

The initial selector/text-entry design above was not accepted. The owner requested
this replacement on 2026-10-25; status remains **AWAITING USER ACCEPTANCE**.
Config → Keybinds now shows Action/Key tables, side by side at ≥112 columns and
one scrollable table below that threshold. Key cells capture actual keypresses,
blank the old value and emphasize only the capture border. Draft differences
retain a distinct border. Undo reverses individual edits or a confirmed restore
of defaults. Restore Defaults and discard/quit with unsaved changes require
confirmation; only Save persists. Inactive buttons have no focus/hit rectangles
or action side effects. Personal preferences and profile separation are unchanged.

The expanded agent-run focused table test passed in
`build/keybinds-table-focused-5.log`. It checks border-only capture emphasis,
Escape cancellation, Delete clearing, reserved/conflicting keys, keyboard Enter
capture, inactive screen/file no-ops, undo, all warning/cancel/confirm paths,
save/reload, theme independence, every binding category and literal fields.
It covers resize while capturing, the 111/112-column transition, all 30 rows,
scroll/navigation and 48×18/monochrome behavior.

Initial focused failures remain in `build/keybinds-table-focused.log` through
`build/keybinds-table-focused-3.log`: the new harness used the wrong application-mode
Home sequence, matched warning prose instead of the bracketed confirmation button,
and needed bounded mouse/input settling. The next run passed. Adding capture-resize
coverage then exposed a partial-frame timing assumption in the test
(`build/keybinds-table-focused-final.log`); the harness now waits for the table
headers and footer after resize, preserving the capture assertions.

Static analysis initially reported possible negative/oversized table indices
(`build/keybinds-table-analysis.log`). Row counts, selected actions and array
accesses now have explicit bounds. The final authorized command
`make BUILD_DIR=build/keybinds-table-analysis analyze` passed, exit 0; log:
`build/keybinds-table-analysis-final.log`. All C unit/integration/support sources
also compiled with `_FORTIFY_SOURCE=3` and strict warnings in the initially fresh
`build/keybinds-table-test-compilation` directory. The final harness edit was
recompiled afterward; logs: `build/keybinds-table-test-compilation.log` and
`build/keybinds-table-test-compilation-final.log`, both exit 0.

Complete ordinary host baseline, using an initially nonexistent directory:

```sh
TERM=xterm-256color make BUILD_DIR=build/ci-keybinds-table CPPFLAGS=-D_FORTIFY_SOURCE=3 test-ci \
  > build/ci-keybinds-table-tests.log 2>&1
```

This initial baseline passed the direct/native flow but stopped on the tmux
capture-border assertion, exit 2. The diagnostic byte stream in
`build/keybinds-table-tmux-diagnostic.log` proves that tmux forwards UTF-8
box-drawing characters with the reverse attribute present. The ASCII-only test
screen reader dropped those bytes, losing both their positions and attributes.
The reader now decodes single-cell box drawing while retaining border emphasis;
no application assertion was removed. The focused direct and real-tmux/native
checks then passed in `build/keybinds-table-direct-final.log` and
`build/keybinds-table-tmux-final.log`, including the unchanged border assertion.
All C test sources were recompiled after the reader/header change, exit 0;
`build/keybinds-table-reader-compilation.log`.

The last resize-wait harness edit preceded its compilation in that initial run;
the corresponding object timestamp and final source manifest were checked. The source/test/build SHA-256 manifest is
`build/keybinds-table-tested-tree.json`. Environment is unchanged from the
personal-keybind record above; these are local host results, not hosted Ubuntu,
physical-board or sanitizer/compiler-matrix evidence. The normal
`build/the-bugger` was rebuilt; log: `build/keybinds-table-build-5.log`.

Final complete baseline, with another initially nonexistent build directory:

```sh
TERM=xterm-256color make BUILD_DIR=build/ci-keybinds-table-final CPPFLAGS=-D_FORTIFY_SOURCE=3 test-ci \
  > build/ci-keybinds-table-final-tests.log 2>&1
```

Final baseline result: **PASS, exit 0**. Its frozen source/test/build hashes
are `build/keybinds-table-final-tested-tree.json`. This run includes the UTF-8
screen-reader correction throughout the shared input/terminal suites.

The final baseline includes default/native, direct and real-tmux keybind capture,
context/source navigation, terminal restoration, localhost remote, owned-server
and offline OpenOCD checks. The final process audit found no remaining GDB,
OpenOCD or frontend processes (`build/keybinds-table-process-audit.log`).
The frozen source manifest, relative documentation file links and
`git diff --check` passed. Final delivery hashes, including documentation and new
files, are `build/keybinds-table-delivery-tree.json`. No hosted Ubuntu,
physical-board or additional sanitizer/compiler-matrix result is claimed.

## Keybind column spacing and header padding

Manual review requested tighter horizontal spacing and a small additional gap
below the instructions. Implemented for review: fixed compact 34-column tables
with a 22-column action area and 12-column key cells, centered within narrower
menu cards. Long additional-key suffixes were removed from row labels without
changing their behavior. The 112-column two-table threshold is unchanged.
One blank line now separates the instructions and table headings; row capacity
accounts for that padding at the 48×18 minimum.

Agent-run focused capture/table tests passed, exit 0, after rebuilding the normal
`build/the-bugger`; logs: `build/keybinds-spacing-padding-build.log` and
`build/keybinds-spacing-padding-focused.log`. This preserves capture, borders,
undo, inactive-button no-ops, confirmations, all rows, responsive transitions,
persistence and literal fields. The earlier column-only focused check also
passed (`build/keybinds-spacing-focused.log`) before the requested padding was
added; it is not the final revision's evidence.

Fresh final ordinary host baseline:

```sh
TERM=xterm-256color make BUILD_DIR=build/ci-keybinds-spacing-padding CPPFLAGS=-D_FORTIFY_SOURCE=3 test-ci \
  > build/ci-keybinds-spacing-padding-tests.log 2>&1
```

Baseline result: **PASS, exit 0**. Frozen source/test/build hashes:
`build/keybinds-spacing-padding-tested-tree.json`. The host environment and
local-vs-hosted/hardware limitations are unchanged from the records above.
Status remains **AWAITING USER ACCEPTANCE**; no Git mutations were performed.

The final spacing/padding static-analysis command
`make BUILD_DIR=build/keybinds-spacing-padding-analysis analyze` passed, exit 0;
log: `build/keybinds-spacing-padding-analysis.log`. The earlier column-only
analysis also passed but predates the requested header padding; it is not the
final static-analysis record.

Final audit found no remaining GDB, OpenOCD or frontend processes; log:
`build/keybinds-spacing-padding-process-audit.log`. The tested source manifest,
local documentation links and `git diff --check` passed. Final delivery file
hashes are `build/keybinds-spacing-padding-delivery-tree.json`.

## Opt-in Vim-like input — 2026-10-25

State: implemented, awaiting user acceptance. The owner selected Normal on field
entry. Vim-like navigation is now opt-in in Config → Keybinds, with a personal
boolean independent of profiles. New/legacy preferences default off; legacy stock
aliases alone are cleared without rewriting the file. Custom assignments and
appearance are preserved. Toggle/Undo/discard/Restore Defaults/Save share the
existing draft transaction. Normal/Insert field editing adds explicit footer
and edge feedback without target operations on mode changes.

Agent-run development checks compiled with GCC 16.2.1, C17, strict warnings and
`CPPFLAGS=-D_FORTIFY_SOURCE=3` under `build/vim-focused`. `test-editor`, preference
unit checks, `test-keybinds` and `test-navigation` passed after fixes. The first
navigation run exposed a real cursor bug: rendering an unfocused empty field
clamped the shared focused cursor and reversed subsequent typing. Rendering now
uses a separate idle cursor for unfocused fields. An earlier keybind test still
expected factory `k`; it now asserts the deliberately unbound factory value,
while the navigation comparison explicitly enables Vim input. Existing default-off
literal-entry and real custom Ctrl+H dispatch assertions remain covered.

Focused logs: `build/vim-evidence/initial-build.log`,
`focused-initial-failure.log`, `focused-final.log`. A preliminary build invocation
also requested nonexistent `test-preferences`; the actual unit binary was built
and executed explicitly. No application assertion was removed to hide a failure.

Full baseline command (agent-run, outside the sandbox for ptrace, sockets and tmux):

```sh
TERM=xterm-256color make BUILD_DIR=build/vim-ci-20261025 CPPFLAGS=-D_FORTIFY_SOURCE=3 test-ci
```

Environment: Linux 7.2.11-arch1-1 x86-64, GCC 16.2.1 (20260810), GDB 18.1,
tmux 3.8; `TERM=xterm-256color`. Fresh build directory; the existing CI workflow's
Tests job uses this same target, on hosted Ubuntu 26.04. Log:
`build/vim-evidence/test-ci.log`. Exact application/test/build inputs are recorded
in `build/vim-evidence/source-manifest.sha256` (manifest SHA-256
`b08a32f8e49d854a07596839578ab8302f8bc688727809b85718ded97aee4c28`).

The first complete baseline stopped in the isolated tmux keybind test while
waiting for Normal feedback after the Console → Memory handoff. Its full log is
`build/vim-evidence/test-ci-initial-tmux-failure.log`. Diagnostic rerun
`tmux-debug.log` located the failure at Memory focus: the harness sent a mouse
sequence while the prior standalone Escape could still be pending in tmux.
The harness now waits for the field-mode indicator to disappear and asserts
that the stopped workspace remains open before sending another click. It also
waits for Insert feedback before submitting `start`. No application assertion
was weakened. `tmux-debug2.log` records the passing real-GDB/tmux rerun.

The final baseline uses `BUILD_DIR=build/vim-ci-final-20261025` with the same
command/flags, in another fresh directory. Log: `build/vim-evidence/test-ci-final.log`.
Final input manifest: `build/vim-evidence/source-manifest-final.sha256`, SHA-256
`99dd6dc5396cc2963cc4a5e29b8a0f1f53833e22030da141f9e37bcb55c3ed38`.
That run reached the existing source-view integration test and exposed another
old default assumption: it sent Ctrl+H without enabling Vim-like input. The test
now uses fixed Ctrl+Left, retaining its source-stepping and unchanged-PC
assertions. Opt-in Ctrl+H coverage remains in navigation and custom keybind tests.
The failure log is `build/vim-evidence/test-ci-default-alias-failure.log`.
A subsequent complete retry was interrupted when the owner's desktop session
ended, during the remote test; `test-ci-session-interrupted.log` preserves that
partial run. A read-only host process audit found no surviving test/debugger
processes before restarting. The complete baseline was restarted in the same
freshly created task directory with unchanged flags; no older task's objects were
used. Final baseline result: PASS (exit 0), including default/native tests, direct and
isolated tmux input, real localhost remote sessions, authored managed-server
cases and actual OpenOCD pre-initialization script checks. Native coverage includes
context selection, text-entry/Enter/Backspace, source stepping, terminal restoration
and cleanup. New mode checks passed directly and through real tmux. A final
read-only host process audit found no surviving test/frontend/GDB/gdbserver/
OpenOCD/tmux processes. `git diff --check` passed. Status remains awaiting owner
manual review and acceptance; this is local host evidence, not a hosted CI result.

After explicit owner permission, agent-run
`make BUILD_DIR=build/vim-input-analysis analyze` passed with clang-tidy 23.1.3
(`build/vim-evidence/analysis.log`); project diagnostics were not suppressed to
obtain success. Fortified compilation of every `tests/{unit,integration,support}/*.c`
object under `build/vim-input-analysis` also passed, matching CI's compilation
step (`build/vim-evidence/test-compilation.log`). Local host success does not establish hosted Ubuntu success or
physical-board validation. No hardware, sanitizer or additional compiler matrix
was run. `make all` rebuilt the ordinary review binary at `build/the-bugger`
(`build/vim-evidence/review-build.log`). Final manual review uses the
[opt-in walkthrough](testing.md#opt-in-vim-like-input); no owner manual acceptance
is recorded. Full Vim editor emulation remains excluded.

## tmux RGB theme rendering — 2026-10-25

The owner reported that Macchiato backgrounds differed inside tmux despite a
working `tmux-256color`/`Tc` configuration. The app selected Alacritty direct
color outside tmux but kept the 256-color description inside it, quantizing
subtle blue-gray surfaces. The owner subsequently confirmed that the focused
executable (`build/tmux-color-focused/the-bugger`) corrected the visual issue in
their own tmux setup. This is an owner-reported visual check, not a full hardware
or release-matrix result.

Implementation uses ncurses terminfo and conventional RGB advertisements:
COLORTERM truecolor/24bit, RGB/Tc flags, or setrgbf plus setrgbb. It selects a
matching tmux-direct description for tmux/screen names inside tmux, preserves
Alacritty handling, and falls back to the original TERM if no direct description
is available. NO_COLOR remains authoritative. No palette slots, environment,
user tmux configuration or server options are changed. A preliminary subprocess
probe was replaced during development with the smaller existing-library approach
following review of Neovim, Notcurses, tmux and ncurses primary documentation.

Agent-run focused checks passed with GCC 16.2.1 and fortification under
`build/tmux-color-focused`: `test-terminal-description` and `test-tmux-colors`.
The real tmux test uses an isolated server and the reported `*:Tc` override;
it checks exact Macchiato workspace/panel/raised background RGB values and
text/accent foreground RGB values against standalone output, changes to Rose
Pine Dawn, and checks 256-color and NO_COLOR fallback behavior. Existing terminal
restoration checks run on every exit. `focused-final.log` under
`build/tmux-color-evidence` records success. Earlier focused failures are retained:
`focused-theme-selection-failure.log` used the wrong family-cycle count;
`focused-fallback-fixture-failure.log` assumed unsetting the parent's COLORTERM
would remove it from tmux children. Current tmux sets that variable itself, so
the fixture now removes it explicitly only for its app process. Application
assertions and real color comparisons remain intact.

Fresh full host baseline (agent-run outside sandbox for ptrace, sockets and tmux):

```sh
TERM=xterm-256color make BUILD_DIR=build/tmux-color-ci-20261025 CPPFLAGS=-D_FORTIFY_SOURCE=3 test-ci
```

Environment: Linux 7.2.11-arch1-1 x86-64, GCC 16.2.1 (20260810), GDB 18.1,
tmux 3.8. Log: `build/tmux-color-evidence/test-ci.log`. Exact application, test,
workflow and build inputs: `build/tmux-color-evidence/source-manifest.sha256`
(manifest SHA-256
`09159c7235d1ddaf278584b065cd4628b747c6393bd30e91135c56fc5e5cb13f`).
Full baseline result: PASS (exit 0). New exact-color/fallback tests, existing
mouse/drag/split/function-key/resize behavior, live keybind/Normal/Insert input,
context selection, native/remote sessions and actual OpenOCD pre-init checks all
passed. The final read-only host process audit found no surviving frontend,
test, GDB, gdbserver, OpenOCD or tmux processes. `git diff --check` passed.
This is automated local host evidence; hosted Ubuntu execution remains unverified.

With explicit owner permission, `make BUILD_DIR=build/tmux-color-analysis analyze`
passed with clang-tidy 23.1.3 (`build/tmux-color-evidence/analysis.log`).
Fortified compilation of every C test/support object passed (exit 0); its log is
`build/tmux-color-evidence/test-compilation.log`. The ordinary review executable
was rebuilt through `make all` at `build/the-bugger`
(`build/tmux-color-evidence/review-build.log`). Local results do not establish
hosted Ubuntu success. No physical targets, sanitizer or additional compiler
matrix were run. Exact rendering still requires RGB forwarding by tmux and a
capable outer terminal; the application cannot supply absent terminal capability.

## v0.1.2 release preparation — 2026-10-25

Status: prepared and locally verified; awaiting owner acceptance, not published
or release-approved. The owner
confirmed the tmux visual correction and merged the preceding work. Preparation
uses the owner's existing `chore/prep_v0.1.2_release` branch, based on
`f2d085e78552b933f792330a1ed07d0bea5c7761`. The version and dated changelog now
identify 0.1.2. A version directory is the default distribution destination;
`RELEASE_DIST_DIR` remains an explicit override. This prevents one release's
checksum file from replacing another version's checksum file. Existing
`build/dist/v0.1.0/` and `v0.1.1/` artifacts are preserved. The owner's preliminary
`make release` invocation used the old 0.1.1 version; those root-level outputs
are not v0.1.2 artifacts and were not relabeled.

Included implementation references, read from actual local history at preparation:

| Change | Implementation reference |
|---|---|
| Multi-file Source tabs | `d94ffac72bb467e95c08ec9fefb162cc781c2208` |
| Source catalog-order regression | `9a869d203820bae28d2c575471612ddfcc3fa65a` |
| Spatial content-pane transfer | `4a55838a8e8942954446780b41ade18716fde84b` |
| Preserve Enter during content focus | `84efd848748e0479add02d52f244b31a4005f908` |
| Startup debugging / F13 documentation | `5c31adacc2e10006c7728b2748959da6a0b6e9af` |
| Inline Console command entry | `85bd6fd76f2beab2ca4b2b8a866b0a67d1daf7b5` |
| Terminal-size warning | `77b2d86fb3cedbda4d3729a17e4c44a3df0841e2` |
| Personal keybind tables | `4e857b55a75b3db1b9f85cb8221010ec8a67ed9a` |
| Opt-in Vim-like input | `40894bf8d51b216a77c067555b6111452db9b560` |
| tmux RGB rendering | `f2d085e78552b933f792330a1ed07d0bea5c7761` |

The final release-preparation commit and resulting main/tag commit remain
owner-controlled; no hash is invented for an uncreated commit. The preceding
rendering feature's source analysis passed on this production code; preparation
changes the version, distribution destination and release documentation.

Agent-run local checks passed (exit 0):

- Fresh fortified baseline:
  `TERM=xterm-256color make BUILD_DIR=build/v0.1.2-ci CPPFLAGS=-D_FORTIFY_SOURCE=3 test-ci`.
  Log: `build/v0.1.2-evidence/test-ci.log`. Exact application, test, workflow
  and build inputs: `build/v0.1.2-evidence/test-inputs.sha256` (manifest SHA-256
  `e5c85141009278efd122ee6dcf4ac61fd221213f96ca264aaa5d00b17744f03b`).
- Hardened `make release`, extracted binary/checksum/license/source-identity and
  ELF inspection, clean build from extracted source, then packaged CLI, terminal,
  workspace, navigation and exact-color real-tmux checks. Logs:
  `build/v0.1.2-evidence/package-final.log`, `artifact-checks.log`,
  `source-clean-build.log`, `packaged-ui.log`.
- The first source-completeness comparison correctly found that README had
  changed after the preliminary archive was built; the archive is regenerated
  after documentation edits. The initial failure is retained in
  `build/v0.1.2-evidence/artifact-initial-snapshot.log`; the successful final
  comparison supersedes that incomplete snapshot.
- Missing executable, invalid SOURCE_DATE_EPOCH and old-binary/new-source
  version mismatches were rejected as expected (`package-negative.log`). The
  explicit RELEASE_DIST_DIR override produced exactly three matching assets
  (`package-output-override.log`).

Environment: Linux 7.2.11-arch1-1 x86-64, GCC 16.2.1 (20260810), GDB 18.1,
tmux 3.8 and glibc 2.44. The packaged executable references GLIBC symbols
through GLIBC_2.38; shared dependencies may impose additional requirements.
The final documentation snapshot is repackaged and the extracted binary, source
identity, independent source build and packaged UI checks are verified again.
The ordinary review executable is rebuilt at `build/the-bugger`.

Prepared distribution files (attach separately after release acceptance):

- `build/dist/v0.1.2/the-bugger-v0.1.2-linux-x86_64.tar.gz`
- `build/dist/v0.1.2/the-bugger-v0.1.2-source.tar.gz`
- `build/dist/v0.1.2/SHA256SUMS`

SHA256SUMS records the final archives' numeric hashes; the binary's BUILD-INFO.txt
also embeds its corresponding source archive's hash. The source archive cannot
contain its own final checksum. A local external release manifest records the
final archive identities without a self-referential documentation/hash cycle.
GitHub's two automatic source snapshots are separate from these three uploads;
filenames in documentation and local preparation do not establish publication.

Release limitations remain explicit: local host tests do not establish hosted
Ubuntu success or fresh physical-board results. Fresh STM32/ARM, ESP32/Xtensa
and ESP32/RISC-V release-matrix evidence, required PR checks and final owner
release acceptance remain outstanding. M17 is historical board evidence, and
the v0.1.0 physical RISC-V deferral does not automatically apply to v0.1.2.
No hardware, sanitizer or additional compiler matrix was run during preparation.
The packaged executable is dynamically linked; recorded host/library requirements
do not establish compatibility with older distributions.
