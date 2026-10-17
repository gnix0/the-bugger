# Native session fixtures

These small programs are debug targets authored for M05/M06, not fake debugger
engines or hardware evidence. `make fixtures` creates:

- `build/debug-fixture` from `session.c`: prints its PID, working directory,
  argument values, THE_BUGGER_TEST, stdout/stderr markers and ticks.
  It runs until terminated, giving Continue/Pause and cleanup a stable target.
- `build/debug-fixture-cpp` from `session.cpp`: uses a C++ string, prints its
  value and sum=42, then exits. It exercises a real C++ ELF without requiring
  C++ in the frontend or test harnesses.

Both use source symbols, -g -O0, strict warnings and no sanitizer runtime.
ASan/UBSan/LSan apply to the frontend/harness during `make sanitize-native`;
LSan in a deliberately ptraced target would fail under GDB.

`make test-native` launches actual GDB, checks argv/cwd/environment without shell
expansion, reads the real output PTY, pauses/continues, exercises console and
log failures, verifies exit/cleanup, and tests attach/detach and GDB crashes.
It also drives the actual TUI in a PTY. See `docs/testing.md` for owner steps.

Special C fixture arguments:

| Argument | Behavior |
|---|---|
| --exit | Print initial data, exit 0 |
| --flood | Print 20,000 numbered lines and FLOOD END, then ticks |
| --allow-attach | Permit tracing this fixture through PR_SET_PTRACER_ANY; used for same-owner test attach |
| --deny-attach | Make only this fixture nondumpable to exercise a real denied attach |
| --ignore-hup | Ignore PTY hangup, verifying that console-run cleanup explicitly terminates the owned program |
| --control-bytes | Explicit output-safety test: emit clear-screen/OSC bytes; use only under the frontend's captured output |

No global ptrace/sysctl setting is changed. Tests terminate/reap their own
external fixture after checking that frontend detach leaves it alive and
resumed. Attached stdout stays with its original destination. The target's
control/OSC bytes are emitted only with --control-bytes and deliberately escaped
by the frontend. Ordinary runs, including --allow-attach, do not clear the host
terminal or erase the printed PID. Memory/source/stepping inspection is tested
in later gates.

## M06 context targets

`context.c` creates two pthread workers blocked in recursive calls. Main waits
for both workers, enters its own recursion through a tail-call bridge and raises
SIGTRAP in `context_checkpoint` (inlined in optimized builds). The trap is an
intentional GDB stop, not a target error or frontend breakpoint implementation.
Continue releases/joins the workers and raises a second trap with only main
remaining. Another Continue sleeps until terminated. Run this target under GDB;
standalone SIGTRAP normally terminates it.

`make fixtures` also builds:

- `build/context-fixture`: strict C17, -g -O0 -pthread; visible recursive arguments.
- `build/context-fixture-optimized`: -g -O2 -pthread; GDB's actual optimized,
  inline and tail-call mappings are authoritative, not reconstructed by the UI.
- `build/context-fixture-stripped`: -O2 -s -pthread; unknown names/source and
  available unwind frames remain legitimate data.

Pass `--deep` in Launch arguments for 70 recursive calls and multiple stack
pages. Normal mode uses six. No attach-policy changes or new frontend library
dependency are needed; -pthread is for these debug targets only. Targets remain
uninstrumented under sanitizers, as in M05. The actual-GDB API test prints its
reported frames/arguments as evidence; the TUI test uses the unoptimized target.
Missing/unreliable unwind errors are deterministic synthetic-peer cases, not
claims that a stripped ELF necessarily lacks unwind information.
