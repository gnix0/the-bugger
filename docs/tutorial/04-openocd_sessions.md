# 04 — OpenOCD sessions

[Guide index](README.md) · Previous: [Native sessions](03-native_sessions.md) · Next: [Remote sessions](05-remote_sessions.md)

OpenOCD connects a host debugger to a physical target through a debug probe. The practical chain is **The Bugger → target-compatible GDB → OpenOCD → probe → board**. A working USB connection is only one part of that chain: the GDB architecture, OpenOCD build, scripts, probe interface, board and symbol file must agree.

**On this page**

- [Prepare the connection](#prepare-the-connection)
- [Choose board mode or advanced mode](#choose-board-mode-or-advanced-mode)
- [Decide who starts the server](#decide-who-starts-the-server)
- [Reproducible STM32 fixture configuration](#reproducible-stm32-fixture-configuration)
- [ESP32-S3 and ESP32-C6 configurations](#esp32-s3-and-esp32-c6-configurations)
- [Review and connect](#review-and-connect)
- [Halt, inspect and reset](#halt-inspect-and-reset)
- [Disconnect deliberately](#disconnect-deliberately)

## Prepare the connection

Have these ready:

- A powered, debug-accessible board and the correct probe connection.
- OpenOCD and scripts supporting that board/probe combination.
- A GDB executable supporting the target architecture.
- The debug ELF matching the firmware actually programmed on the board, and its source files.
- A TCP endpoint for GDB, commonly `127.0.0.1:3333`.

Loading the ELF supplies debugging symbols. It does not flash the board, replace its application or establish that its bytes match. Programming is a separate operation with your existing tooling. Connection and initialization scripts can still change hardware state; inspect their contents before starting.

The recorded physical setups are STM32 NUCLEO-F446RE with ST-LINK, ESP32-S3 with native USB/JTAG, and ESP32-C6FH4 with native USB/JTAG. Consult the [validation records](../validation.md) for exact tested configurations. A discovered script is not evidence that its hardware has been tested with this frontend.

## Choose board mode or advanced mode

Choose **Embedded target - OpenOCD** on the launcher.

In **OpenOCD setup: Board script**, enter the board script or choose **Browse boards**. In the installed-script list, **Filter** matches a filename or exact installed path. Use arrows, Page Up/Down, wheel or scrollbar; select the entry and return to the form. Filtering and browsing inspect metadata; they do not execute scripts or probe hardware.

A board script normally supplies its own adapter and target dependencies. Do not append another pair merely because those fields exist in advanced mode.

Choose **Adapter + target (advanced)** when your configuration genuinely uses separate files. **Browse adapter** and **Browse target** select them. More elaborate setups belong in **Profile... → Ordered OpenOCD scripts**, with **OpenOCD search directories** when needed. An explicit ordered script list overrides the board/adapter fields. Each list entry is one path, not a shell command.

## Decide who starts the server

| Start OpenOCD | Meaning |
|---|---|
| Yes - managed process | The Bugger starts a local OpenOCD process and cleans up that owned process on close. |
| No - existing server | It connects through GDB to a server you already started; it never terminates that server. |

Managed endpoints accept **`localhost:3333`** and **`127.0.0.1:3333`**, using IPv4 loopback. Replace 3333 with a free port when necessary. For a server on another host or an IPv6 endpoint, use existing-server mode. Do not start a second server on a port already in use.

The **GDB binary** field is a single executable name/path. ARM's empty-field fallback is `arm-none-eabi-gdb`; explicitly select the matching Xtensa or RISC-V GDB for ESP targets. An empty OpenOCD executable uses `openocd`. The endpoint and firmware path must describe your actual session; fresh forms do not contain fabricated values.

## Reproducible STM32 fixture configuration

This example is for **NUCLEO-F446RE/ST-LINK**, with the repository's default STM32 fixture already programmed. Building it does not program anything:

```sh
make fixture-m17-stm32
```

From the repository root, use:

| Field | Value |
|---|---|
| OpenOCD setup | Board script |
| Board script | `./tests/fixtures/embedded/openocd/nucleo-f446re.cfg` |
| Symbols (ELF) | `./build/m17/stm32f446re/firmware.elf` |
| GDB binary | `arm-none-eabi-gdb` |
| GDB endpoint | `127.0.0.1:3333` or `localhost:3333` |
| OpenOCD binary | Your matching OpenOCD executable, commonly `openocd` |
| Start OpenOCD | Yes - managed process, unless you already run it separately |

The supplied script uses direct ST-LINK DAP/SWD. Your OpenOCD installation must provide its referenced `interface/stlink-dap.cfg` and `target/stm32f4x.cfg`. The installed `board/st_nucleo_f4.cfg` is another board configuration used by the fixture's external programming procedure; it is not identical to the direct-DAP debugging fixture script.

See the [embedded fixture instructions](../../tests/fixtures/embedded/README.md#arm-cortex-m-fixture) for tool requirements, the optional LED image and the separate firmware-replacement procedure. Use `firmware-led.elf` only with the corresponding LED image; do not mix symbols from the two builds.

## ESP32-S3 and ESP32-C6 configurations

Activate your installed ESP-IDF environment first. Its export script path is installation-specific:

```sh
. /path/to/esp-idf/export.sh
idf.py -C tests/fixtures/embedded/espidf -B "$PWD/build/m17/esp32s3" \
  -DIDF_TARGET=esp32s3 -DSDKCONFIG="$PWD/build/m17/esp32s3/sdkconfig" build
```

For C6, use a separate target/build directory:

```sh
idf.py -C tests/fixtures/embedded/espidf -B "$PWD/build/m17/esp32c6" \
  -DIDF_TARGET=esp32c6 -DSDKCONFIG="$PWD/build/m17/esp32c6/sdkconfig" build
```

These commands build only. Follow the [fixture instructions](../../tests/fixtures/embedded/README.md) for deliberate programming and actual connector selection. A USB-to-UART connector alone does not provide native USB/JTAG. Do not change security/debug eFuses to try an example.

| Field | ESP32-S3 | ESP32-C6FH4 |
|---|---|---|
| Board script | `./tests/fixtures/embedded/openocd/esp32s3.cfg` | `./tests/fixtures/embedded/openocd/esp32c6.cfg` |
| Symbols (ELF) | `./build/m17/esp32s3/the_bugger_m17.elf` | `./build/m17/esp32c6/the_bugger_m17.elf` |
| GDB binary | `xtensa-esp32s3-elf-gdb` | `riscv32-esp-elf-gdb` |
| OpenOCD binary | Installed Espressif OpenOCD | Installed Espressif OpenOCD |
| GDB endpoint | `127.0.0.1:3333` | `127.0.0.1:3333` |
| Start OpenOCD | Managed, or No for an existing instance | Managed, or No for an existing instance |

The custom scripts select hardware-thread awareness to retain coherent CPU contexts across reset. They do not add a FreeRTOS task inspector. On C6, this workflow concerns the RV32 debugging target; it is not a general promise of LP-core or peripheral support.

### Keep executable and scripts from the same distribution

An ESP-IDF shell can set `OPENOCD_SCRIPTS` to Espressif's scripts. If you switch to a system OpenOCD, do not accidentally keep incompatible scripts. For example, when intentionally using the system installation:

```sh
env -u OPENOCD_SCRIPTS ./build/the-bugger
```

Then select `/usr/bin/openocd` if that is your system executable. Alternatively configure its actual script root, often `/usr/share/openocd/scripts`, in **Profile... → OpenOCD search directories**. Explicit search directories take precedence over the environment. These paths are installation examples, not required locations on every host.

## Review and connect

1. Complete the form and choose **Review session**.
2. Read the floating review's endpoint, GDB, symbols and server ownership.
3. For managed OpenOCD, review the indexed argument vector. Each indexed item is one literal argument, so a path with spaces remains one argument. Use **Previous argv**/**Next argv** when shown; acceptance controls remain separate.
4. Choose **Back** or Escape to correct anything. Clicking outside does not accept or edit through the dialog.
5. Choose **Start session** for managed startup or **Connect** for an existing server.

Wait for the actual session result. A listening TCP port alone does not establish a usable target connection. Missing executables, unavailable scripts, busy ports, probe/permission failures, early server exit and startup timeout leave logs and retained configuration to investigate.

## Halt, inspect and reset

The workspace identifies **LIVE OPENOCD**. Use **Halt** to request a stopped target, then wait for fresh stopped context before stepping or reading. With matching symbols and sources, set a breakpoint such as `fixture_checkpoint` in the supplied workload and Continue to it. Inspect the actual Call Stack because checkpoints occur in more than one call context.

Source stepping needs line/function information. Without symbols, select Disassembly for instruction stepping; `Cannot find bounds of current function` commonly means source-oriented stepping cannot establish a function boundary. Loading the correct ELF and repairing source paths solves a different problem from halting the CPU.

**Reset** requires confirmation and requests reset-and-halt. It does not program firmware. Reset wiring, scripts and hardware determine where execution actually stops. Read the PC after completion; do not assume that seeing Stopped means the first reset instruction has not run. See [startup debugging](06-execution_control.md#debugging-embedded-startup-code).

## Disconnect deliberately

**Close Session** offers **Leave halted** or **Resume**. Only owned GDB/OpenOCD processes are cleaned up. An external server remains running. Target behavior is backend-dependent; custom detach events may override the requested disposition. If the application reports that stop, disconnect or cleanup could not be confirmed, inspect the physical target/server before assuming a state.

After probe loss or a server failure, retained values are historical. Correct the connection and start a new reviewed session. Do not treat reconnection as restoration of RAM contents or permission to repeat an uncertain write/read.

The ordinary [Memory](11-memory.md), [variable](10-locals_and_watches.md) and [editing](14-editing_values.md) restrictions still apply. The limited, explicitly confirmed STM32 MMIO workflow is separate from general board connectivity and from user-confirmed ordinary-RAM evidence.
