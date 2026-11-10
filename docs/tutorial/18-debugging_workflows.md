# 18 — Debugging workflows

[Guide index](README.md) · Previous: [Customization](17-customization.md) · Next: [Troubleshooting](19-troubleshooting.md)

These exercises combine the individual tools into complete investigations. The native examples use small programs supplied with the repository. Run their build commands in your Linux shell from the repository root, then launch the resulting executable through **Linux application**. Commands introduced as Console commands go in The Bugger's **Command** field. Expected numeric values assume the unmodified fixture and no intervening edits.

**On this page**

- [Follow a calculation through a call](#follow-a-calculation-through-a-call)
- [Stop only at an interesting call](#stop-only-at-an-interesting-call)
- [Track a changing value and find its writer](#track-a-changing-value-and-find-its-writer)
- [Investigate a suspicious buffer byte](#investigate-a-suspicious-buffer-byte)
- [Understand an unexpected call path](#understand-an-unexpected-call-path)
- [Connect raw Stack bytes with a named local](#connect-raw-stack-bytes-with-a-named-local)
- [Investigate low-level embedded execution](#investigate-low-level-embedded-execution)
- [Make a reusable investigation workspace](#make-a-reusable-investigation-workspace)
- [Recover after an interruption](#recover-after-an-interruption)

## Follow a calculation through a call

**Question:** why is the final result different from the function's intermediate value?

Build `make build/code-fixture`, launch it, and send Console `start`.

1. Focus Source and step Over until `code_counter` has been initialized and incremented. Read it in Locals; expect 3.
2. At the call to `code_callee`, choose Into. Step past `code_local = value + 3`, then Read `code_local`; expect 6.
3. Add a Watch for `code_result` and Read it after the assignment in the callee; it is 6 at that point.
4. Inspect Call Stack. Select `main` to compare the caller's local and source location, then return to the stopped context using Source **Stopped**.
5. Choose Out to return from the callee. The returned expression doubles 6, so the caller's assignment produces 12.
6. Step the final increment and Read the Watch again; it becomes 13.

The intermediate store, return expression and caller increment explain three distinct values. A retained Watch sample does not update automatically at each step; make each comparison after Read. Close the launched fixture when finished; its later loop does not exit itself.

## Stop only at an interesting call

**Question:** what happens when a repeatedly called function receives argument 3?

Build `make build/break-fixture`, launch it and send Console `start`.

1. Add a breakpoint at `m08_tick` in Breakpoints → Manage.
2. Edit its condition to `value == 3`.
3. Continue and inspect the actual stop. Explicitly Read `value`, then inspect the function's computation.
4. Use Details in Call Stack to see the full caller path. Opening Details does not change selection or fetch argument values by itself.
5. Disable the conditionally interesting breakpoint to continue without removing its definition, or remove it when the investigation ends.

For a one-off destination, select the desired source/instruction row and **Run to Selected** instead. That request can be interrupted by other stops; verify where it actually ended.

## Track a changing value and find its writer

**Question:** which instruction changes a global, and what are the before/after values?

Build `make build/watch-fixture`, launch it and send Console `start`.

1. Add `watched_value` to Watches and explicitly Read; initially it is 17.
2. From its Actions, create a **Value change** hardware data watchpoint. Review the expression and wait for hardware verification.
3. Continue. On a supporting target, execution stops at the change to 20. Read the stop reason and actual source/assembly location; refresh the Watch deliberately.
4. Focus Disassembly to inspect the low-level location and Registers to examine the current CPU state.
5. Continue to the change to 21, compare again, then remove the watchpoint through Breakpoints → Manage.

This workflow depends on hardware watchpoint support and resources. A failure to insert is not a successful software substitute. A local-variable watchpoint may expire with scope; choose the global for this reproducible exercise.

## Investigate a suspicious buffer byte

**Question:** did a buffer change, or am I looking at an old observation?

Build `make build/memory-fixture`, launch it and send Console `start`.

1. Read `&memory_buffer` in Memory. Reduce its byte count to 32 and Refresh; confirm `THE BUGGER MEMORY` in ASCII and `54 48 45 20` at the start.
2. Add a breakpoint at `memory_checkpoint` and Continue to the first stop.
3. Refresh and note the first byte. Continue to the second stop. Existing bytes should be stale until you Refresh.
4. Refresh the same range: the first byte is now `5a`, ASCII `Z`. You have a new observation of the same storage, not just a different view of an old sample.
5. Open another Memory pane at `&memory_other` to compare an independent object.

If editing is the purpose, choose a disposable interior row and follow [the complete reviewed byte-edit procedure](14-editing_values.md#edit-memory-bytes). Read/verify neighbors after applying. Do not use a guessed board address for a native buffer exercise.

## Understand an unexpected call path

Build `make build/context-fixture`, launch it and Continue to its deliberate SIGTRAP.

1. Open Threads alongside Call Stack. Notice waiting workers and the stopped main context.
2. Select a `recursive` frame. Read its arguments explicitly with Read Args and inspect full Details.
3. Move outward to another recursive invocation; compare `depth` and the caller line. Identical function names do not mean identical invocations.
4. Inspect the corresponding Locals after a deliberate Read in each scope.
5. Return to the stopped context and Continue. The workers join before the next trap; refresh and observe the changed thread list.

For a deeper call chain, use `--deep` in Launch arguments and page Frames. Missing values in optimized/stripped variants are limitations of the available debugging information, not evidence that the interface selected the wrong fixture.

## Connect raw Stack bytes with a named local

This exercise requires a C++ compiler for the fixture:

```sh
make build/stack-fixture
```

Launch `./build/stack-fixture`. Send Console `break stack_checkpoint`, then `run`.

1. Open Stack and explicitly Read the frame-0 SP window.
2. Select the `recurse` caller in Call Stack. The existing Stack anchor remains at frame 0.
3. Use Stack **Options → Query selected-frame SP point** to annotate that caller's reported SP separately.
4. Use **Query local/argument names, then select one address** and choose an eligible local. If required, read and explicitly accept the session-wide no-call review before querying.
5. Open complete row details/provenance to compare the address point with the sample. An address point does not establish the object's full extent or prove it belongs to your declared stack interval.
6. Leave the declaration unknown unless you actually know bounds; a guessed interval is not measured usage.

The fixture includes a C++ object and reference you can use as address annotations. Choose them through the names picker; there is no need to construct an address expression in Console. Adding a point does not sample the bytes at that address.

## Investigate low-level embedded execution

Use a matching, already-programmed fixture and its [complete OpenOCD configuration](04-openocd_sessions.md).

1. Connect, Halt and wait for fresh Stopped context.
2. Add a Require hardware breakpoint at `fixture_checkpoint` and Continue.
3. At the stop, inspect Call Stack to identify which checkpoint context occurred.
4. Focus Disassembly, enable Mixed, and compare PC with the reported CPU register. Step Into at instruction level and observe several instructions mapping to one source line.
5. Explicitly Read a known ordinary fixture buffer, not arbitrary peripheral storage. A pointer or symbol is not permission to poll MMIO.
6. If examining startup, deliberately Reset-and-halt and inspect actual PC before assuming where reset stopped. Break at `main` or `app_main` according to your image.
7. Close with a deliberate halted/resume policy and check any unconfirmed-disposition report.

Exact instructions, register names, breakpoint slots and stop locations depend on your board, GDB and server. If the hardware breakpoint cannot be installed or the stop differs, inspect Console and Call Stack before continuing the exercise.

## Make a reusable investigation workspace

1. Save the connection as a named profile.
2. Pin a caller/callee source file, keep Call Stack near Watches, and open Memory at a useful expression.
3. Resize columns and lower splits for the investigation. Add only the tabs you need; use Source fullscreen temporarily for long code.
4. Save through **Setup → Save setup** and read its scope.
5. Close and reopen the profile. Review restoration, confirm changed connection/image context where appropriate and wait for a stop if needed.
6. Expand the lower inspectors again. Read Watches and Memory deliberately; restored definitions are not samples or authorizations.
7. Resolve missing source pins or breakpoints in **Unresolved setup entries** before assuming the investigation is ready.

## Recover after an interruption

On a failed read, preserve the old sample's stale label and correct the cause before another explicit read. On disconnect, inspect target/server state, correct configuration and start a new reviewed session. On an uncertain mutation, follow [reconciliation/new-session requirements](14-editing_values.md#uncertain-writes-and-recovery), not repeated Apply.

Save Console logs while the relevant output is still retained. For an interface problem, record focus mode, active view, key/mouse sequence, pane size, terminal/tmux environment and what changed after reverse navigation or resize. Those details distinguish a target-data problem from a navigation or stale-display problem.
