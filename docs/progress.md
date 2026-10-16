# Milestone ledger

Target: **v0.1.0**. No release has been accepted.

States: `planned` → `implemented` → `automated-verified` → `awaiting-user` →
`accepted`. Only explicit owner acceptance permits the last transition.

| Milestone | Deliverable | State | Commit SHA |
|---|---|---|---|
| M00 | Repository foundation | awaiting-user | — |
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

State history: `implemented` → `automated-verified` → `awaiting-user`.
Manual acceptance: pending. **AWAITING USER ACCEPTANCE**.
No milestone commit SHA is recorded because none exists yet.

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

The next action after automated checks is owner manual validation using
`docs/testing.md`. After acceptance, propose a Conventional Commit message and
wait for the owner to commit/push and explicitly authorize M01.
