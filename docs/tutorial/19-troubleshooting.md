# 19 — Troubleshooting

[Guide index](README.md) · Previous: [Debugging workflows](18-debugging_workflows.md) · Next: [Controls and glossary](20-controls_and_glossary.md)

Start with the session state, pane header and most recent Console message. A connection error, missing symbols, stale sample and unsupported operation require different remedies. Preserve the actual error before changing settings; repeated clicks can obscure which operation first failed.

**On this page**

- [Build and launch](#build-and-launch)
- [Connection problems](#connection-problems)
- [Execution and source](#execution-and-source)
- [Breakpoints and watchpoints](#breakpoints-and-watchpoints)
- [Variables and registers](#variables-and-registers)
- [Memory and MMIO](#memory-and-mmio)
- [Interface, settings and restoration](#interface-settings-and-restoration)
- [Know the current limits](#know-the-current-limits)
- [Report a reproducible problem](#report-a-reproducible-problem)

## Build and launch

### A development dependency is missing

The build requires a C17 compiler, GNU Make, `pkg-config` and development files for ncursesw, libelf and json-c. Runtime libraries alone are insufficient. Check discovery:

```sh
pkg-config --modversion ncursesw libelf json-c
```

Install the missing development package using your distribution's normal tooling, then build again. Fixture-specific C++/pthread/cross-toolchain requirements are separate from the main application.

### A downloaded executable will not start

Check that it is the Linux x86-64 distribution for your host, has executable permission and has the runtime dependencies listed in its `BUILD-INFO.txt`. An automatic GitHub source archive does not include a separately packaged executable. For an incompatible runtime, build from source on the host instead of assuming a universal static binary.

### `cannot spawn debugger executable: No such file or directory`

Inspect **GDB binary**, not just the program's Executable/ELF field. Verify the chosen debugger exists:

```sh
command -v gdb
command -v arm-none-eabi-gdb
```

Use the command relevant to your session; missing ARM GDB does not prevent a native `gdb` session. Set the field to the actual executable name/path without appended flags. A file that exists but cannot execute can also have a missing interpreter/loader or incompatible runtime. Correct the retained form and review again. Building The Bugger does not install GDB.

### The terminal is too small or looks wrong

Enlarge it to at least 80×20. Below that size, state is retained and the target is not automatically paused. Unexpected line drawing/colors can reflect TERM, locale, font or terminfo mismatch. Use a terminal description actually supported by the emulator; compare with `NO_COLOR=1 ./build/the-bugger` for a monochrome diagnostic. See [terminal color guidance](17-customization.md#terminal-color-and-tmux).

## Connection problems

### Native attach is denied

Check the current PID, ownership and host/container ptrace policy. The frontend does not alter system security. For the documented disposable test, start `debug-fixture --allow-attach`; that fixture explicitly permits tracing of itself. Do not generalize it to disabling host-wide protection. An Attach profile intentionally omits its previous PID.

### A remote endpoint is refused

Verify the server is listening at the entered TCP host/port and that the selected GDB supports the target. `localhost:3333` is accepted for managed OpenOCD and normalized to IPv4 loopback; remote IPv6 uses `[address]:port` in existing-server mode. A shell command, device path or port number without the required endpoint format is not that field's input.

For extended-remote, **Connected** without an inferior can be normal. Use the server's supported explicit attach/launch procedure; Continue does not start a local executable just because an ELF path is present.

### OpenOCD cannot start, exits early, or reports probe errors

Separate these causes:

1. **Executable missing:** check OpenOCD binary/PATH and permissions.
2. **Script not found/incompatible:** match the executable to its script distribution. An ESP-IDF `OPENOCD_SCRIPTS` can conflict with `/usr/bin/openocd`; use the [documented environment/search-path choices](04-openocd_sessions.md#keep-executable-and-scripts-from-the-same-distribution).
3. **Port occupied:** choose the intended existing-server mode or another free port. Do not start a second managed instance on the same endpoint.
4. **Probe inaccessible:** inspect USB connection, power, driver/udev permissions and the board's actual JTAG/SWD connector.
5. **Target initialization failed:** review the actual board or adapter/target scripts and server log. A listening server is not proof of a usable CPU connection.

The board browser discovers files, not hardware. Loading a board script that happens to be installed does not verify your wiring or target model.

### The target or probe disconnected

Treat retained inspector values as historical. Inspect the external server and physical target, fix the link and start a new reviewed session. Existing servers are not terminated by frontend cleanup. An unconfirmed stop/disconnect or requested Leave halted/Resume policy is not proof of final hardware state.

Save retained Console output when possible. Do not repeat an uncertain mutation or MMIO request merely because a new connection appears available.

## Execution and source

### Loaded / PID not started / Awaiting stop

A native executable is loaded but has not run to a stop. Console `start` runs to `main`; Continue runs normally. Put `start` in **Console → Command**, not target Arguments or the Linux shell.

### Stepping needs a fresh stopped context

Wait for pending execution/halt/reset work to settle. Read session status and Console. Pause/Halt if running. If a backend operation could not verify a fresh stop, reconnect as the error requests. A stopped label without usable symbols cannot create source function boundaries.

### `Cannot find bounds of current function`

GDB cannot establish the function boundary needed by that source-oriented operation. Common causes are no symbol ELF, a mismatched image, stripped/debugless code or a stop in ROM/library code. Select Disassembly for instruction Into/Over. Supply the correct matching symbols and source mapping for source-level work. Source paths alone cannot recreate symbols absent from the ELF.

### There is no source mapping, or the source file cannot be opened

Check the actual stopped frame and whether it belongs to your application. Verify the ELF matches the target and contains debug information. Use **Source lookup settings** for relocated build paths, then refresh the GDB-known source chooser. Full paths distinguish duplicate filenames; the frontend does not guess by basename. Assembly remains useful when source is legitimately unavailable.

### A line repeats, jumps, or a local looks uninitialized

Multiple instructions can map to one source line. Optimization can reorder/remove lines and variables. A breakpoint can stop before initialization. Compare Disassembly and actual frame, then use a debug `-O0` fixture for a predictable learning example. Never interpret an uninitialized storage sample as the source initializer's intended result.

### Step Out keeps running

Out waits for the execution frame to return. It does not exit a loop, and an infinite-looping function may never return. Pause/Halt, then choose a breakpoint or instruction/source step appropriate to the task.

### Disassembly shows repeated instructions or an edge notice

Raw data and padding can decode into repeated instructions. Check whether you explicitly paged, navigated to raw bytes or enabled **Decode bytes (raw inspection)**. Ordinary scrolling should stay within established code boundaries and skip labeled gaps.

**ELF:?** means the image mapping is unverified; it does not by itself cause a **Code range unknown** startup banner. **Start/End of available instructions** appears after you try to scroll past the available instructions, then clears when you scroll back, load a new listing or resize. If a banner appears on opening the view, persists after reverse scrolling, or ordinary scrolling reaches bytes outside established code boundaries, save the Console log and [report the complete sequence](#report-a-reproducible-problem). Use [Go to a known symbol or Return to Stop](07-source_and_disassembly.md) to regain orientation; do not assume repeated decoded bytes are valid code or accept unrelated evidence to suppress a warning.

## Breakpoints and watchpoints

### A breakpoint is pending or resolves elsewhere

The symbol/module may not be loaded, the selected line may not be executable, or one symbol can resolve to several locations. Inspect Breakpoints metadata and matching symbols. A pending enabled intention is not a confirmed installed stop. On restoration, inspect unresolved entries and explicit acknowledgment requirements.

### Hardware insertion fails on Continue

Slots, width, alignment and modes depend on the target/server. Creation/configuration can succeed before physical insertion fails on resume. Remove or disable only entries you understand, then inspect the resulting state. The frontend does not retain a software data-watchpoint fallback to satisfy a hardware requirement. A cleanup/verification failure can deliberately block execution.

### Run to Selected stopped early

Another breakpoint, watchpoint or signal can intervene. Read the actual stop reason/location. The temporary operation is canceled/cleaned up on an intervening stop; user breakpoints remain.

## Variables and registers

### Locals/Watch shows Read required, stale, or unavailable

Read required asks for an explicit sample. Stale means an old observation survived execution/context changes. Select the intended frame and Read again. Unavailable can mean optimized-out storage, a name outside scope, unsupported type/location or a backend error. Refresh local names updates names, not all values.

Calls/mutations/casts in Watch/Memory expression fields are intentionally rejected. Use supported names/members/constant indices/simple offsets. Expanding a pointer is deliberate inspection and does not authorize background polling of its referent.

### Changing a frame shows old values briefly

Old panes keep their original labels while replacement context loads; longer switches say **Switching to frame #…**. Wait for the new frame labels. If data is attributed to the wrong frame after completion, report the selection and execution sequence, not just the final value.

### A CPU register cannot be edited

Select frame 0 and obtain a fresh complete scalar sample. Caller/unwound, unavailable, truncated, wide/compound registers and unsupported backend write permissions are refused. The frontend never enables GDB `may-write-registers` implicitly. A mismatch after Apply can reflect rejected/normalized hardware bits; Verify/Refresh establishes only what was observed. See [register editing](14-editing_values.md#edit-a-cpu-register).

## Memory and MMIO

### Scrolling stops at an unknown region

Scrolling reads adjacent bytes only when the application has evidence that they are ordinary readable memory. To inspect a known location deliberately, use Go or an explicitly labeled Previous/Next range read. Do not relabel unknown peripheral space as ordinary memory just to scroll through it.

### Bytes are `??` or a read fails

Check address, requested extent and target units/width. A range can cross an unmapped/unreadable boundary. Reduce it to the known object and inspect the status; missing bytes are not zeros. Old bytes retained after a failed read remain stale. Unknown byte order blocks multi-byte grouping; it does not justify assuming the host order.

### MMIO requires setup/range confirmation again

Setup identity and per-view range authorization are separate. Every accepted Console command expires both MMIO setup confirmation and consent. Resume/context changes expire range consent. Definitions remaining in the list are not permission. Confirm only the exact documented validated setup and deliberate range; other configurations are not covered by a generic STM32 label.

### A MMIO request timed out or became uncertain

Hardware may already have been accessed. The frontend does not automatically retry. Further reads remain blocked until the exact outstanding operation reconciles or the session is reconnected; a completed untrustworthy response requires reconnection. Preserve Console diagnostics and inspect the actual device before another deliberate request.

### A Memory or variable edit is refused

Fresh ordinary writable-region evidence is required in addition to a supported value/destination. Read consent alone does not prove writable RAM. Remote ELF sections are not write permission; the limited STM32 user-confirmed evidence flow is separate. Check [the editor prerequisites](14-editing_values.md) rather than bypassing restrictions through guessed declarations.

### A write result is uncertain

Stop applying more edits. A completed uncertain reply may be reconciled only after explicit inspection of the original destination. Timed-out/canceled/lost completion requires a new session before any of the three editors can write again. Reconnecting does not undo a possible write. See [uncertain-write recovery](14-editing_values.md#uncertain-writes-and-recovery).

## Interface, settings and restoration

### Arrows move buttons instead of scrolling

Click the view body or scrollbar for **CONTENT FOCUS**; its header shows `*`. Wheel scroll does not acquire keyboard focus. Escape/Tab leaves content. In a text field, arrows move the cursor. See [focus navigation](02-interface_and_navigation.md).

### A key or mouse action does not arrive

Try its visible toolbar/menu alternative, inspect **Config → Keybinds**, and check terminal/tmux interception. In tmux enable mouse forwarding. Vim Insert protects text from execution keys; Escape first. Keybind capture deliberately intercepts keys until completed/canceled. Fixed keys remain even after personal assignments change.

### A lower inspector seems to replace or shrink another view

It shares space with the primary pane in its column. Drag their horizontal separator, minimize it with `_`, or enlarge the terminal. Other columns keep independent heights. Compact rendering hides retained panes when space is insufficient; expanding the terminal restores their arrangement. Source fullscreen intentionally hides other panes until Restore.

### A paste is cut off or did not execute

Bracketed paste stays in one field, normalizes newlines/tabs and discards excess beyond 255 characters with a notice. It deliberately does not submit. Review and press Enter/Send separately. Unbracketed terminals cannot reliably separate pasted key bytes from ordinary typing.

### Profiles/preferences cannot be saved

Read the error. If another instance saved newer settings, reload profiles or restart before saving preferences. An invalid or unsupported file is preserved. Read-only profile recovery may still offer usable connections while blocking saves and recents updates. Back up the file before repairing it or restoring a known-good copy, then choose **Config → Profiles → Reload file**, or restart for preferences. See [configuration recovery](16-profiles_and_saved_setups.md#where-files-live-and-how-recovery-works).

### A setup restored the layout but not values

Expected: setup saves definitions, not sampled state, grants, PID, stopped context or lower expansion. Expand inspectors and Read deliberately. Missing pins and unsupported definitions appear in **Unresolved setup entries**. A running target remains running while restoration awaits a stop; Pause/Halt explicitly. An enabled saved breakpoint failure needs review/acknowledgment before assuming protection.

### Save Log will not replace a file

Existing files are intentionally preserved. Choose a new destination filename. Only retained output is exported; Save Log cannot recover older lines already removed or attached-program output that never entered Console.

## Know the current limits

There is no automatic Flash programming, interactive program stdin, UART terminal, functional Symbols/Peripherals inspector, specialized RTOS/task/trace view, Memory search/export/compare tool, full Vim editor or command history. CPU-register and supported integer/bool/RAM editing do exist, within their documented limits. Architecture compatibility does not establish every optional feature on every board.

## Report a reproducible problem

Include application version/revision, Linux/terminal/tmux details and dimensions, session type, GDB/server versions, target/probe and matching fixture/firmware where relevant. Give exact actions from connection onward, including focus/view, forward and reverse scrolling, context switches and resize. State expected and observed results and whether a fresh session reproduces them.

Save retained Console output and include relevant screenshots when they clarify the result. Remove sensitive paths/data before sharing. Say which target you used and which steps you completed so others can reproduce the problem. Use the [security policy](../../SECURITY.md) for vulnerabilities rather than public exploit details.
