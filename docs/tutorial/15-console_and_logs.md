# 15 — Console and logs

[Guide index](README.md) · Previous: [Editing values](14-editing_values.md) · Next: [Profiles and saved setups](16-profiles_and_saved_setups.md)

Console combines session/debugger messages and captured native-program output. Its inline **Command** field sends one command to **GDB**. It is not a shell, program stdin, UART console or full terminal emulator.

**On this page**

- [Send a GDB command](#send-a-gdb-command)
- [Useful examples](#useful-examples)
- [Expect invalidation after commands](#expect-invalidation-after-commands)
- [Browse output](#browse-output)
- [Save a log](#save-a-log)

## Send a GDB command

1. Expand Console using its dock title or `^`.
2. Click **Command**, or reach it with Tab.
3. Type the command and press Enter or click **Send**.
4. Read the resulting output/status. An accepted command can still fail later inside GDB.

The field clears when the session accepts the command to send to GDB. If submission is rejected, the text stays available for correction. Escape leaves the field without sending or discarding it. Each Console pane retains its own draft through tab changes, dialogs and terminal resizing; a new session or reused split starts empty.

Ordinary fields accept `q`, digits and movement aliases literally. Use Left/Right, Home/End, Backspace/Delete and Ctrl+U to edit; Ctrl+A/Ctrl+E reaches the beginning/end. With Vim-like input enabled, press `i` to type. Configured function-key execution shortcuts work in ordinary editing and Vim Normal mode. In Vim Insert, Escape first, then invoke the shortcut. Printable debugger aliases do not take over literal input.

There is no command history or multiline command editor. Paste normalizes line breaks to spaces rather than running a sequence of commands. Submit separate commands deliberately.

## Useful examples

Enter these in Console, not your Linux shell:

| GDB command | Purpose/context |
|---|---|
| `start` | For a loaded native executable, run to a temporary stop at `main`. |
| `info program` | Ask GDB about the current program state. |
| `echo console-ok\n` | Produce a simple output marker. |
| `break variables_ready` | Add a symbolic breakpoint in the variables fixture. |
| `run` | Deliberately start/restart according to the current GDB session; not a universal remote launch operation. |
| `hbreak main` | Request a hardware code breakpoint when that symbol/backend supports it. |
| `file ./build/m17/stm32f446re/firmware.elf` | Load that local matching symbol file into GDB; does not program firmware. |

Use the Breakpoints view and other inspectors when they cover your task. Console can also change debugger and target state outside those workflows. A GDB command is not inherently read-only. In particular, scripts, aliases, execution commands, assignments and `monitor` operations can have effects that depend on the backend.

`monitor ...` is a GDB route to the connected server's commands, not a portable The Bugger command language. OpenOCD commands do not automatically apply to another server. There is no universal reset/program sequence for every board; the [OpenOCD toolbar reset](06-execution_control.md#debugging-embedded-startup-code) provides a reviewed supported action where available.

## Expect invalidation after commands

Accepted Console commands invalidate MMIO setup confirmation and MMIO range consent before transmission, including apparently harmless commands. They also invalidate relevant remote RAM/image evidence and current observations as applicable. This is intentional because a command can invoke aliases or scripts whose effects are not obvious from its text.

If you need MMIO afterward, review the exact supported setup and renew range authorization. For a remote edit, renew eligible RAM evidence. A retained range, symbol or displayed sample does not renew permission. Console-created/changed breakpoints need explicit [setup review](16-profiles_and_saved_setups.md#review-breakpoints-created-outside-the-frontend) before supported intent can be saved.

## Browse output

Console retains up to **512 lines**, with a length limit on each line. Scroll up with the wheel, focused-body arrows/Page keys or scrollbar. Return to the end to follow new output. Pan horizontally for long rows. Save Log includes only retained text; it cannot recover older lines already removed, text beyond the line-length limit or output never captured.

Native launched stdout/stderr share captured output; attached processes retain their original output destinations. Control and non-ASCII bytes are escaped, so literal escape sequences can appear in diagnostics instead of coloring or controlling your terminal. This is display protection, not necessarily program corruption.

## Save a log

Choose **Save Log**, enter a new destination path and accept. The application creates a private file containing the retained escaped text. It preserves existing files rather than silently replacing them. On an open/write error, the input remains available for correction; choose another new path or fix the destination directory. Cancel/Escape closes the dialog without saving.

For a useful problem report, save the log soon after the failure and include session type, target, tool versions, matching ELF and steps. Review logs for sensitive paths, source, memory or credentials before sharing them.
