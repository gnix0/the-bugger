# 03 — Native Linux sessions

[Guide index](README.md) · Previous: [Interface](02-interface_and_navigation.md) · Next: [OpenOCD](04-openocd_sessions.md)

A native session debugs a Linux process on the same host. **Launch** lets The Bugger start and own the debugged program. **Attach** connects to a process that already exists. Their configuration and close behavior differ, so choose the one matching your task.

**On this page**

- [Prepare a program for inspection](#prepare-a-program-for-inspection)
- [Launch an executable](#launch-an-executable)
- [Attach to a process](#attach-to-a-process)
- [Close or quit](#close-or-quit)
- [Reuse the configuration](#reuse-the-configuration)
- [Common native-session problems](#common-native-session-problems)

## Prepare a program for inspection

Build your program with debug information, commonly `-g`. For learning and predictable source stepping, disable optimization, commonly `-O0`. Optimized programs remain debuggable, but lines may execute out of source order, variables may have no recoverable storage, and some functions may disappear through inlining.

Use an executable built for the host and a compatible GDB. Native Linux x86-64 is the recorded native configuration. Having an ELF file alone does not establish compatibility with every architecture.

## Launch an executable

Choose **Linux application → Mode: Launch executable**.

| Field | What to enter |
|---|---|
| Executable / ELF | A local executable path, such as `./build/code-fixture`. |
| Arguments (quoted) | Arguments to the target program, without repeating the executable. |
| Working directory | An optional directory for the program. |
| Environment (NAME=value) | Optional environment overrides for this session. |
| GDB binary | An executable name on PATH or a local executable path. An empty field uses the native `gdb` fallback. |

Choose **Review session**, check the floating review, then **Start native**. Back/Escape returns to the intact form. Errors retain your entries for correction. A successful launch initially says **Loaded**; Continue starts execution. To stop at `main`, send `start` through Console instead of immediately continuing.

### Quoting is grouping, not a shell

Arguments and environment entries support whitespace, quotes and backslashes for grouping. They are passed as literal arguments; shell variables, command substitutions, wildcards, pipelines and redirections are not evaluated.

For example:

```text
"two words" "" '$HOME;$(false)'
```

This represents three program arguments: a spaced string, an empty string, and literal metacharacters. An environment entry such as `THE_BUGGER_TEST="hello world"` supplies that value; it is not a shell command.

Path fields accept `~/` or `~` for the current user's home. Bare tool names use PATH. Review shows effective launch paths; profiles keep the entered path text. Do not put flags after a tool name in **GDB binary**: that field identifies an executable.

Home expansion uses an absolute `HOME` when set, otherwise the account's home directory. A nonempty relative `HOME` is rejected; correct the launching environment rather than expecting a relative home prefix to be guessed. No other shell expansion occurs. These path rules also apply to local tools and files in embedded/remote configuration; source mapping origins remain literal build paths.

### Observe program output

Build the output fixture:

```sh
make build/debug-fixture
./build/the-bugger
```

Launch `./build/debug-fixture`. After Continue, Console shows its PID, working directory, argument/environment information and periodic ticks. Pause stops it. Use a quoted argument and `THE_BUGGER_TEST="hello world"` to check what the fixture receives.

Launched stdout and stderr share an isolated captured terminal stream. Control and non-ASCII bytes are escaped for display. The Console **Command** field sends GDB commands; it is not the target program's stdin. Interactive program input and a terminal emulator are not provided. If your program requires an interactive terminal, attaching to a separately started process may suit the workflow better, provided tracing is permitted.

## Attach to a process

1. Start the program separately and obtain its current PID.
2. Choose **Linux application → Mode: Attach to PID**.
3. Enter that PID, an optional matching **Symbols ELF**, and the GDB executable.
4. Review, then choose **Start native**. Attachment halts the process for inspection.

Use a symbol file matching the running executable. A different build can produce misleading source or type information. Attach does not move the process's output into Console; stdout/stderr retain their original destination.

For a reproducible same-owner exercise, run in another terminal:

```sh
make build/debug-fixture
./build/debug-fixture --allow-attach
```

Enter its printed PID. `--allow-attach` changes only that fixture's per-process tracing permission. It is not a frontend option or a recommendation to weaken system-wide ptrace settings. After detach, stop the fixture in its original terminal when finished.

GDB enforces Linux tracing permission. Same-owner identity alone may be insufficient under a restrictive ptrace policy, sandbox or container. An attach error does not mean the frontend can or should bypass that policy.

## Close or quit

| How the target began | Confirmed Close Session |
|---|---|
| Launched through The Bugger | Terminates the launched process and closes its debugger. |
| Attached to an existing PID | Detaches and resumes that process. |

**Close Session** returns to the launcher. **Quit** exits the application after the same active-session confirmation. **Cancel** keeps debugging. Outside text fields, default `q` closes/backs out of the current screen; from the workspace it requests session closure. Ctrl+Q requests application exit. Escape handles local navigation and does not close the session.

Ctrl+C is application interruption and cleanup, not the toolbar's Pause operation. SIGINT, SIGTERM and SIGHUP follow cleanup and terminal restoration paths. If cleanup or disconnection cannot be confirmed, read the reported result and inspect the actual process before assuming its final state.

## Reuse the configuration

Choose **Save profile** before starting, give it a name and confirm replacement if needed. Profiles retain connection definitions, arguments and source lookup settings. They omit PID and environment overrides. An Attach profile therefore needs a new current PID each time; a successful previous attachment is not permission or identity for a later process with the same number.

Use [saved setups](16-profiles_and_saved_setups.md) for pane arrangement, pinned source files, Memory definitions, Watches and supported code-breakpoint intent. Saving a profile alone does not save live values or reconstruct a running process.

## Common native-session problems

- **Loaded but no code:** use Console `start` or set a breakpoint and Continue. The program has not stopped yet.
- **Paused in a library:** inspect your application's caller in Call Stack, or break at a known function on the next run. Source stepping needs usable line information.
- **Cannot spawn debugger executable:** check the GDB field with `command -v gdb` or the exact chosen path. Check executable permissions and required runtime dependencies.
- **No locals at a line:** initialization may not have happened, the frame may be wrong, or optimization may have removed the value. Stop at a known checkpoint and select its caller.
- **Program ended:** its state is no longer a fresh stopped context. Start a new session for the exercise rather than treating retained inspector samples as live.

Continue with [execution control](06-execution_control.md) or the [first investigation](18-debugging_workflows.md#follow-a-calculation-through-a-call).
