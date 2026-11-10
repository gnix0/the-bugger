# 06 — Execution control

[Guide index](README.md) · Previous: [Remote sessions](05-remote_sessions.md) · Next: [Source and disassembly](07-source_and_disassembly.md)

Debugging alternates between letting a program execute and stopping to inspect it. **Running** means the target is changing underneath you. **Stopped** means GDB reports a pause; the frontend must also obtain fresh context before context-dependent controls can proceed. Retained samples from an earlier stop are not current merely because the screen still contains them.

**On this page**

- [Toolbar actions](#toolbar-actions)
- [Establish a useful stopped state](#establish-a-useful-stopped-state)
- [Choose source or instruction stepping](#choose-source-or-instruction-stepping)
- [Run to a selected location](#run-to-a-selected-location)
- [Browsing is separate from execution](#browsing-is-separate-from-execution)
- [Debugging embedded startup code](#debugging-embedded-startup-code)
- [When execution is blocked](#when-execution-is-blocked)

## Toolbar actions

These are factory defaults; saved bindings and toolbar hints can differ.

| Control | Default | What it requests |
|---|---|---|
| Continue | F5 | Resume a stopped target; start a loaded native launch when appropriate. |
| Pause / Halt | F6 | Interrupt running execution; OpenOCD calls this Halt. |
| Over | F10 | Step over a source line or machine instruction, according to the current mode. |
| Into | F11 | Step into a source call or execute an individual instruction. |
| Out | Shift+F11 | Run until the current execution frame returns. |
| BP | F9 | Toggle a breakpoint at the selected Source/Disassembly location. |
| Reset | Unbound | Reviewed reset-and-halt for a supported OpenOCD target; simulated reset in demo. |

Use the visible controls if the terminal intercepts function keys. Ctrl+C interrupts The Bugger and runs cleanup; it is not a substitute for Pause/Halt.

## Establish a useful stopped state

A native Launch begins **Loaded**. Console `start` deliberately runs to `main`; ordinary Continue does not insert a breakpoint there. For an attached or remote target, wait for the connection's real stopped result. If connected without an inferior under extended-remote, explicitly attach/launch through the server's supported GDB operations.

When a toolbar action reports **Stepping needs a fresh stopped context**, wait for an outstanding transition and inspect the session status/Console. A recently sent halt/reset/Console command can require fresh context before stepping. Repeatedly pressing Step does not repair missing symbols or an unconfirmed execution state.

## Choose source or instruction stepping

Click/focus **Source** to choose source stepping, or **Disassembly** to choose instruction stepping. The session header reports the mode. It remains selected when you inspect Registers, Locals or another non-code pane, so toolbar Over/Into keeps its meaning.

In source mode, Over attempts the next source-level step without entering a called function; Into can enter it. Several instructions may belong to one source line, and optimized source mapping may skip, repeat or reorder lines.

In instruction mode, Into executes one machine instruction; Over handles a call without stepping through its implementation when supported. Disassembly is useful for startup code, optimized functions and locations without line information. A source-step failure such as **Cannot find bounds of current function** is a reason to inspect symbols/context and try instruction stepping, not to guess a source location.

### Example: enter and leave a call

Launch the [code fixture](01-getting_started.md), use Console `start`, and step Source until the selected execution line calls `code_callee(code_counter)`.

1. Choose **Into**. Expect to enter `code_callee` if GDB has usable line information.
2. Step past `code_local = value + 3` and explicitly Read that local. It should be 6 for argument 3.
3. Choose **Out**. Expect to return to the caller after the call completes.
4. Read `code_result` after the caller receives the return and performs its later increment; the final result is 13.

Out returns from a **function**, not from a loop. If the current function never returns, it may keep running indefinitely; Pause/Halt deliberately interrupts it. Selecting a caller for inspection does not itself move execution into that caller or change what “current execution frame” means.

## Run to a selected location

Select a source line or instruction, then choose **Actions → Run to Selected** in that code view. This runs toward a temporary stop without assigning a new PC. User breakpoints remain in place. Another breakpoint, signal or other intervening stop can finish the operation before the selected location is reached.

Check the actual stopped location afterward. Temporary cleanup failure blocks further execution and offers breakpoint **Manage → Refresh**; do not assume the selected destination was reached or delete unrelated breakpoints as a workaround.

## Browsing is separate from execution

Scrolling, searching, opening a file, changing a frame and navigating to an address do not set the PC. Source **Stopped** returns to the original stopped thread/frame. Disassembly **Return to Stop (this view)** only repositions that listing. These controls help recover orientation without accidentally changing program execution.

The execution marker remains meaningful even if you scroll away. Cached code can be inspected while running, but uncached requests are not silently deferred until a later stop. Inspectors show stale/read-required states until their appropriate fresh operations succeed.

## Debugging embedded startup code

Attaching to running firmware does not rewind initialization. To examine early startup, the hardware and server must be able to halt early enough, and you need symbols for that stage.

1. Use the firmware's matching ELF and the correct server/probe scripts. Loading it alone does not program the board.
2. In an OpenOCD session, Halt if needed, then deliberately confirm **Reset → Reset and halt**.
3. Wait for a fresh Stopped state. Inspect PC in Registers and Disassembly before assuming where reset stopped.
4. On the supplied Cortex-M fixture, startup is `Reset_Handler` in `startup.S`: it initializes `.data`, clears `.bss`, then calls `main`. Open that source if available and choose Disassembly for individual instructions.
5. To stop later, add a hardware breakpoint at `main` through Breakpoints with **Require hardware**, or deliberately send GDB `hbreak main`, then Continue.

ESP-IDF's application entry is commonly `app_main`; ROM/bootloader work can need different symbols and assembly inspection. Use symbols that exist in your actual image.

Reset-and-halt is not proof that no earlier instruction ran. Reset catch, wiring and connect-under-reset support are backend-specific; see [OpenOCD reset configuration](https://openocd.org/doc/html/Reset-Configuration.html) when your hardware requires it. Moving PC backward does not restore initialized memory or peripheral state. Before runtime initialization, globals, locals and unwinding may legitimately be unavailable or uninitialized.

## When execution is blocked

Some blocks are deliberate: an active saved-setup restoration, unresolved breakpoint cleanup, an uncertain mutation or an unconfirmed stop requires resolution before more execution. Read the visible reason and the relevant [restoration](16-profiles_and_saved_setups.md), [breakpoint](08-breakpoints_and_watchpoints.md) or [editing](14-editing_values.md) procedure.

A control being visible does not guarantee backend support. Reset is not a generic native restart button. A custom binding does not bypass its confirmation or target restrictions.
