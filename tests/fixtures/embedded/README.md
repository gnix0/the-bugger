# Embedded acceptance fixtures

These fixtures exercise ARM/Cortex-M, Xtensa and RISC-V debugging.
[Validation records](../../../docs/validation.md) list the tested configurations.
Build commands do not connect, reset or program a target. Programming replaces
existing firmware and is a separate deliberate operation. Record the selected
ELF's SHA-256, board revision, probe serial, tool versions and commands with each
physical result. Never count a cross-build as physical execution evidence.

## Shared workload

`workload.c` supplies `fixture_checkpoint`, `fixture_callee` and `fixture_cycle`,
a changing counter, a struct/array/pointer, a Flash string and an SRAM ASCII buffer.
Set a hardware breakpoint on `fixture_checkpoint` and a hardware value-change
watchpoint on `fixture_counter`. The checkpoint occurs both inside a callee and
after a cycle; use the actual call stack when checking frame selection/Step Out.
After a completed cycle, the counter advances, the sample is updated and only the
first buffer character alternates between `M` and `m`. Stop before comparing
hexadecimal and ASCII snapshots. Counter arithmetic is unsigned and wraps.

The workload does not intentionally access MMIO. Pointer/value reads remain
subject to frontend authorization; an ELF symbol is not automatic access proof.
Use explicit reads of these known fixture objects. Do not generalize a fixture's
safe SRAM to an entire unknown target address space.

## ARM Cortex-M fixture

Test device: STM32 NUCLEO-F446RE.

Requires `arm-none-eabi-gcc`, `arm-none-eabi-gdb` and OpenOCD/ST-LINK for physical
execution. The authored bare-metal startup initializes `.data` and `.bss`, uses
the reset clock, masks interrupts and supplies a default exception loop. There is
no HAL, RTOS, semihosting, watchdog configuration or floating-point dependency.
An exception loops in `Default_Handler`; that is a failure to investigate, not a
successful checkpoint. The linker reserves at least 4 KiB for the stack.

```sh
make fixture-m17-stm32
make test-m17-fixture
sha256sum build/m17/stm32f446re/firmware.elf
```

The offline test checks both default and LED ELF variants, reset vectors,
initialized/uninitialized SRAM sections, required symbols and DWARF. Actual ARM
GDB checks line lookup and disassembly without connecting to a target.

The default image never configures GPIO. The optional `make fixture-m17-stm32-led`
image configures PA5 as output and toggles Nucleo LD2. Use it only with PA5 free
of external circuitry. Its filename is `firmware-led.elf`, deliberately distinct
from the default image. Hardware timing/LED behavior remains a physical check.

Memory addresses/sizes are specific to this fixture, following the
[ST STM32F446RE linker map](https://github.com/STMicroelectronics/STM32CubeF4/blob/master/Projects/STM32446E-Nucleo/Examples/UART/UART_Printf/STM32CubeIDE/STM32F446RETX_FLASH.ld)
and [device register definitions](https://github.com/STMicroelectronics/cmsis-device-f4/blob/master/Include/stm32f446xx.h).
No vendor source is copied. These addresses are not generic viewer defaults.

After explicit approval to replace the board's existing application, programming
can be performed externally. This command programs/verifies and resets the board:

```sh
openocd -f board/st_nucleo_f4.cfg \
  -c 'program build/m17/stm32f446re/firmware.elf verify reset exit'
```

Then use The Bugger's OpenOCD flow, that ELF and `arm-none-eabi-gdb`. The supplied
`openocd/nucleo-f446re.cfg` uses direct ST-LINK DAP/SWD for classified watchpoint
stops. The installed board script already supplies adapter/target dependencies;
do not append another pair to it.
Connecting/loading symbols must not issue another programming command.

## Xtensa fixture

Test device: ESP32-S3.

`espidf/` is an authored ESP-IDF project using the same workload, with a short
FreeRTOS task delay between cycles. It does not configure LEDs, Wi-Fi, external
pins or debugging eFuses. SDK startup still initializes normal platform resources.
An application task is not a CPU core or necessarily a GDB thread.

The authored ESP component explicitly enables `-Wall -Wextra -Werror` and restores
errors for the SDK's unused-function, unused-variable, unused-but-set-variable,
deprecated-declarations and extra-warning exceptions. These private options apply
to `main.c` and the shared `workload.c`; third-party SDK components retain their
own warning policy. Native and STM32 builds already enforce `-Wall -Werror`
through the root Makefile, including release and sanitizer configurations.

Activate the installed ESP-IDF environment and build the selected target
from the repository root. The build tree and sdkconfig file stay in `build/`:

```sh
. /path/to/esp-idf/export.sh
idf.py -C tests/fixtures/embedded/espidf -B "$PWD/build/m17/esp32s3" \
  -DIDF_TARGET=esp32s3 -DSDKCONFIG="$PWD/build/m17/esp32s3/sdkconfig" build
```

Programming through `idf.py ... -p PORT flash` is a separate explicitly approved
step; never select a serial port by guessing. Record the SDK version and matching
ELF/bootloader/partition image identities. Do not reuse an image or build tree
for a different target. Do not change security/debug eFuses as part of these tests.

For core acceptance, `openocd/esp32s3.cfg` selects hardware-thread awareness to
retain coherent core contexts across reset. FreeRTOS task awareness is a separate
server capability and must not fabricate a current PC after tasks disappear.

Use the installed Espressif OpenOCD distribution and its matching script roots,
with `board/esp32s3-builtin.cfg` when the actual board
exposes native USB Serial/JTAG. A USB-to-UART connector alone is not that JTAG
connection. Select `xtensa-esp32s3-elf-gdb`.
Exact board/connectors and debug availability must be confirmed before access.
See the official [S3](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-guides/jtag-debugging/index.html)
JTAG guide. Actual physical evidence is recorded separately.

## RISC-V fixture

Test device: ESP32-C6FH4, exercising the RISC-V RV32 core workflow.
The same ESP-IDF project/workload also targets this device. Use a separate build
and sdkconfig; never flash the S3 image onto C6:

```sh
idf.py -C tests/fixtures/embedded/espidf -B "$PWD/build/m17/esp32c6" \
  -DIDF_TARGET=esp32c6 -DSDKCONFIG="$PWD/build/m17/esp32c6/sdkconfig" build
```

Activate the matching ESP-IDF environment first. The tested device is
ESP32-C6FH4 revision0.0 with built-in USB/JTAG. Use `riscv32-esp-elf-gdb` and
`openocd/esp32c6.cfg`. Like the S3 fixture configuration, this selects
hardware-thread awareness: reset removes FreeRTOS tasks, whose former contexts
cannot establish a fresh stopped CPU frame. FreeRTOS task inspection is separate.

The recorded fixture was flashed through the board's UART connector; JTAG tests
use its native USB connector. Confirm the actual port before programming and
record the ELF/binary identities. UART-only connectivity does not provide built-in
JTAG. BOOT/RESET download mode is for flashing when needed; debugger connection
normally uses the booted application. No security/eFuse changes are needed here.

The validated tool versions and the OpenOCD checksum/GDB ROM-symbol limitations
are in the [RISC-V procedure](../../../docs/testing.md#risc-v-hardware-validation).

## Physical acceptance

Follow the [core workflow](../../../docs/testing.md#physical-board-core-workflow-m17),
including code/source, register/PC/SP agreement, explicit SRAM/Flash reads,
locals/watches, hardware break/watch, source/instruction stepping, Halt/Reset,
profile reopen, terminal/tmux interaction, both disconnect choices and probe-loss
recovery. Preserve all unrelated breakpoints and external server processes.
Record each actual result and failure in the validation register; unavailable
capabilities remain explicit. Never substitute these offline checks for boards.
