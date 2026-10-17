# Native session fixtures

These small programs are debug targets authored for M05, not fake debugger
engines or hardware evidence. `make fixtures` creates:

- `build/debug-fixture` from `session.c`: prints its PID, working directory,
  argument values, THE_BUGGER_TEST, stdout/stderr/control-byte markers and ticks.
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

No global ptrace/sysctl setting is changed. Tests terminate/reap their own
external fixture after checking that frontend detach leaves it alive and
resumed. Attached stdout stays with its original destination. The target's
control/OSC bytes are deliberately escaped by the frontend, never executed as
terminal commands. Memory/source/stepping inspection is tested in later gates.
