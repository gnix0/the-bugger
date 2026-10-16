# Milestone ledger

Target: **v0.1.0**. No release has been accepted.

States: `planned` → `implemented` → `automated-verified` → `awaiting-user` →
`accepted`. Only explicit owner acceptance permits the last transition.

| Milestone | Deliverable | State | Commit SHA |
|---|---|---|---|
| M00 | Repository foundation | accepted | `0f5d05b` |
| M01 | Launcher and DEMO workspace | planned | — |
| M02 | Tiled layout engine | planned | — |
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
M00 commit `0f5d05b` was reliably observed in local Git history. Push confirmation
has not been received.

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
M00 commit was observed locally. Owner push confirmation and authorization to
advance have not been received.

## UI specification correction — pending plan approval

Re-read the updated AGENTS.md and the full approved HTML, now located at
`docs/design/prototype.html` after an owner-authorized move from the root.
The move preserved its contents; the ignore rule now permits this design file.
Recorded the revised plan in `docs/plan.md` and aligned architecture/testing
notes. Removed predefined-layout work from the plan: one six-pane default,
resizable tiled panes, tabs/view switching and dynamic splits remain. Optional
restoration refers only to the user's own geometry. All milestone boundaries,
backend requirements and v0.1.0 release gates remain intact.

This is a documentation correction only. Existing M00 automated evidence is
unchanged; no new executable tests are claimed. Await revised-plan approval,
then remain within M00 and the owner-managed acceptance/commit workflow.
