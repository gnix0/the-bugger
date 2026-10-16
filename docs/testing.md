# Testing and manual acceptance

## M00 automated checks

Run from the repository root on Linux:

```sh
make clean
make all
make test
make sanitize
make CC=clang BUILD_DIR=build/clang test
make analyze
git diff --check
```

`all` compiles C17 with strict warnings including `-Wall -Wextra -Wpedantic`
and `-Werror`. `test` runs seven executable integration tests with Python 3's
standard library. `sanitize` builds separately with ASan/UBSan; a runtime
diagnostic is a failure, not a skip. `analyze` uses clang-tidy and fails on
warnings; an unavailable tool is a reported limitation, never a pass. Checks are
explicitly enabled (`clang-analyzer-*` and `bugprone-*`) rather than relying on
machine defaults.

LeakSanitizer cannot run under ptrace. In an agent sandbox that uses tracing,
the sanitizer command may require execution outside that sandbox with approval.
Do not disable leak detection or label that environment failure a passing test.

`build/` is disposable and ignored. The agent's M00 command output is retained
in `build/logs/` after verification; it is local evidence, not a tracked artifact.
Durable command summaries and limitations belong in `docs/progress.md`.
Changing compiler or flags requires a fresh build directory or cleaning.

## M00 manual test now

1. Run `make clean && make`. Expect a successful build without warnings.
2. Run `./build/the-bugger --version`. Expect exactly `the-bugger 0.1.0`.
3. Run `./build/the-bugger --help`. Expect readable usage and a clear statement
   that the launcher and debugging are not implemented.
4. Run `./build/the-bugger`. Expect an M00 status message and immediate return
   to the shell without changing terminal behavior.
5. Run `./build/the-bugger --unknown`, then `echo $?`. Expect a usage diagnostic
   on stderr and exit status `1`.
6. Run `./build/the-bugger --version </dev/null` and `make test`. Expect the
   version line and seven passing tests, respectively.

These checks support NFR-01/03/08/12 foundations. They do not establish terminal
mouse support, process lifecycle correctness or any functional debugger FR.

## Later verification gates

M01/M02 add PTY and real-terminal/tmux visual/input acceptance plus pure layout
tests. M03 adds a structured MI corpus and explicitly fake-server integration
tests. M04/M05 add real GDB native C/C++ fixtures, optimized/stripped cases,
attach permissions and coherent inspection. M06 adds profiles, configuration
tests and a real external gdbserver test. M07 requires real NUCLEO-F446RE
evidence and the full FR/NFR matrix. Fixture instructions and expected states
will be stored alongside those fixtures when implemented.

No mock transcript is hardware evidence. No automated check marks a milestone
accepted; the owner must perform manual validation and explicitly approve.

## CI proposal

Once CI setup is authorized, use a Linux job with GCC, Clang, Make, Python 3 and
clang-tidy, running the M00 commands above. Upload command logs on failure.
Extend dependencies and suites as milestones arrive. Hardware acceptance stays
a separate recorded owner gate; CI must not label it passed based on fixtures.
No hosted workflow or external service has been configured in M00.
