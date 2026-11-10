# 01 — Getting started

[Guide index](README.md) · Next: [Interface and navigation](02-interface_and_navigation.md)

This first exercise stops a small Linux program at `main`, steps through a calculation and reads its result. You need no embedded hardware. Along the way you will meet the few debugging concepts needed for the rest of the guide.

**On this page**

- [What you need](#what-you-need)
- [Run a downloaded build](#run-a-downloaded-build)
- [Build from source](#build-from-source)
- [Configure the exercise](#configure-the-exercise)
- [Stop at main](#stop-at-main)
- [Step through a calculation](#step-through-a-calculation)
- [Read a result](#read-a-result)
- [Pause and close](#pause-and-close)
- [If your first session does not work](#if-your-first-session-does-not-work)

## What you need

The Bugger is a frontend: it presents debugging tools and asks GDB to carry out operations. GDB is the debugger that understands your executable and controls the debugged program, often called the **target** or **inferior**. For a local exercise, the target is another Linux process. Embedded targets add a server and probe, explained in [OpenOCD sessions](04-openocd_sessions.md).

Use Linux and a terminal at least **80 columns by 20 rows**. A larger terminal makes side-by-side inspectors more comfortable. Mouse use is optional. In tmux, mouse interaction requires `set -g mouse on` in your tmux configuration or command prompt.

For native Linux debugging, install GDB 17 or newer and ensure your environment permits process tracing. Some containers and restricted hosts prohibit this even when the application builds successfully. You do not need OpenOCD or an embedded compiler for this exercise.

The Bugger is written in C, but your target can be C or C++. To debug an existing C++ program, use the same [native launch/attach form](03-native_sessions.md) with its executable and matching debug symbols. For C++ firmware on a supported target, use the [OpenOCD](04-openocd_sessions.md) or [existing-server](05-remote_sessions.md) flow with a target-compatible GDB and the ELF matching the programmed image. There is no separate C++ session mode. Start with [ordinary object inspection](10-locals_and_watches.md#inspect-a-c-object); not every C++ expression or library representation is supported.

## Run a downloaded build

The project's [Releases page](https://github.com/gnix0/the-bugger/releases) provides Linux x86-64 distribution archives when published. Download the versioned Linux archive and matching `SHA256SUMS`, check the downloaded archive against the published checksum, extract it and run its `the-bugger` executable:

```sh
./the-bugger
```

Use the archive's `BUILD-INFO.txt` to check runtime-library requirements. GDB, OpenOCD and shared libraries are external dependencies. GitHub's automatic **Source code** downloads are source snapshots, not the separately packaged Linux executable.

The Linux distribution includes this guide at `docs/tutorial/README.md`, together with its screenshots and supporting documentation, so you can browse it offline.

The exercises below use repository fixtures, so obtain the corresponding repository source even if you use a downloaded frontend. Run fixture-building commands from that source directory.

## Build from source

Install a C17 compiler, GNU Make, `pkg-config`, and development packages for **ncursesw**, **libelf** and **json-c**. Distributions use different package names; the important distinction is that building needs headers and development metadata, not only runtime libraries. Building the frontend needs no C++ compiler; compiling C++ target programs or C++ fixtures does. Debugging an already-built C++ executable does not require recompiling it with the frontend.

From the repository root:

```sh
make
make build/code-fixture
./build/the-bugger
```

`make` builds `build/the-bugger`. The second command builds the small example program with debug symbols and optimizations disabled; it does not start it. If you use an alternate build directory, adjust both commands and configuration paths consistently.

The supported command-line forms are:

```sh
./build/the-bugger
./build/the-bugger --version
./build/the-bugger --help
./build/the-bugger --demo
```

No-argument launch opens connection choices. `--demo` uses simulated data and execution; it is useful for exploring navigation without GDB or hardware. Some descriptive text in `--help` predates live embedded/remote support. The current connection pages explain those implemented flows.

Demo values are examples rather than observations of a connected target. Its Profiles option is unavailable. An explicit theme Save can still persist your appearance preference; Preview alone does not save it.

## Configure the exercise

1. Choose **Linux application** on the launcher.
2. Set **Mode** to **Launch executable**.
3. Enter the following fields. Leave the optional fields empty.

   | Field | Value |
   |---|---|
   | Executable / ELF | `./build/code-fixture` |
   | Arguments (quoted) | empty |
   | Working directory | empty |
   | Environment (NAME=value) | empty |
   | GDB binary | `gdb`, or the path to your GDB 17+ executable |

4. Choose **Review session**. A floating review dialog appears over the retained form. Check its effective paths. **Back** or Escape returns to editing; opening review has not run anything.
5. Choose **Start native**.

The workspace should say **Loaded**, with **PID not started** or **Awaiting stop** where appropriate. This is expected: GDB has loaded the executable, but the program has not run. Continue would run normally; it does not automatically stop at `main`.

## Stop at main

1. Expand the bottom-right **Console** group by clicking its dock title or `^`.
2. Click **Command**, type the following GDB command, then press Enter or click **Send**:

   ```text
   start
   ```

3. Wait for **Stopped** and source from `code.c`.

`start` runs the program to a temporary stop at `main`. Enter it in Console, not the program's Arguments field. The source marker `>` identifies the actual stopped execution location. Debugging normally pauses before executing the indicated source line; exact placement follows the debug information supplied by GDB.

If Vim-like input was previously enabled, a newly focused field begins in Normal mode: press `i` before typing. It is off by default.

## Step through a calculation

Click the **Source** body to select source stepping. Press **F10**, or activate **Over**, a few times. The program does this:

```c
volatile int code_counter = 1;
code_counter += 2;
code_result = code_callee(code_counter);
code_result += 1;
```

After the increment, `code_counter` is 3. `code_callee` adds 3 to that argument and returns twice the result, so its returned value is 12. After the final increment, `code_result` is 13. Stop before entering the endless delay loop if you want to inspect that calculation conveniently.

**Over** executes a called function without stopping through each of its source lines. **Into** (F11) enters it when usable debugging information permits. To see the distinction, stop before the call on a fresh run and choose Into. [Execution control](06-execution_control.md) explains instruction stepping and Step Out.

## Read a result

1. Expand the lower-middle group, initially **Locals**.
2. Select **Watches**, or add it using that pane's `...` menu.
3. Choose **+ Watch** or **Actions → Add watch expression**.
4. Enter `code_result` and choose **Add**.
5. Select the new row and choose **Read**.

Adding a watch records an expression; it does not automatically read its value. After a successful read, the row shows a sample. If you have completed the calculation, expect 13. If you stopped earlier, its current value will reflect that earlier point. Continue or step again and the previous sample becomes stale; choose Read at the new stop to sample again.

You can also select `code_counter` in Locals while inspecting `main`. Before its initialization has executed, its storage must not be treated as the initialized value 1.

## Pause and close

Choose **Continue** (F5). The fixture remains running in its delay loop. Choose **Pause** (F6), then wait for a fresh stopped state. You may land inside a library delay routine; this is normal. Select a caller in [Call Stack](09-threads_and_call_stack.md) to inspect your application again.

Choose **Close Session** and confirm. Because this is a launched process, closing terminates it and closes its GDB. Cancel keeps it open. **Quit** or Ctrl+Q closes the application, with confirmation while a session is active. An attached process has different close behavior, covered in [native sessions](03-native_sessions.md).

## If your first session does not work

| Observation | First check |
|---|---|
| Build says a development dependency is missing | Check `pkg-config` can find `ncursesw`, `libelf` and `json-c`; install their development packages. |
| `cannot spawn debugger executable` | Check the **GDB binary** field and `command -v gdb`; use an actual executable path, not a command plus arguments. |
| Loaded, but no source | Enter `start` in Console and wait for a stop. |
| Permission error starting/attaching | Check host/container ptrace restrictions; the frontend does not change them. |
| Function key does nothing | Click the toolbar action; check terminal/tmux forwarding and saved keybindings. |
| Locals has names but no values | Select a row and explicitly Read it. |
| Terminal too small | Enlarge to at least 80×20; session state is retained while the warning is shown. |

For more detail, open [Troubleshooting](19-troubleshooting.md). Next, learn [how focus and navigation work](02-interface_and_navigation.md) before arranging a larger investigation.
