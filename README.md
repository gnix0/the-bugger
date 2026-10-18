# The Bugger

A terminal-native GDB frontend written in C17 for embedded firmware development.
It combines source, disassembly, registers, memory, variables and debugger output
in a configurable tiled workspace with a Monokai Pro visual identity.

The Bugger is **embedded-first, not embedded-only**. Native Linux debugging is a
supported capability and the repeatable foundation for the current development
build. After v0.1.0, feature development focuses overwhelmingly on embedded
workflows; native fixes, compatibility and shared interface/infrastructure
improvements continue.

## Current capabilities and limitations

The current build supports native Linux x86-64 launch/attach, stepping, source
and mixed disassembly, threads/frames, code breakpoints and Run to Selected,
hardware-required data watchpoints, read-only registers/Memory/Locals/Watches,
GDB Console, program output, Save Log, named profiles and action-key remapping.
Availability depends on GDB and the target's capabilities.

Remote-server and OpenOCD configuration definitions can be saved, but execution
remains preview-only. Physical embedded compatibility and release acceptance are
not complete. STM32 NUCLEO-F446RE through GDB/OpenOCD is the first planned hardware
validation target; ESP32/Xtensa and ESP32/RISC-V are also required release targets,
not currently verified support claims. OpenOCD is optional, not universal.

`v0.1.0` is the release target, not a completed release. See the
[roadmap status](docs/roadmap.md#implementation-status) and
[recorded validation evidence](docs/validation.md).

## Build and first session

Build on Linux with GCC or Clang supporting C17, GNU Make, pkg-config, and the
ncursesw, libelf and json-c development libraries/headers. Interactive use needs
a TTY and usable TERM/terminfo. Native sessions require GDB and permitted ptrace;
Linux pidfds support owned-inferior crash recovery. A C++ compiler is used only
for optional debug-target fixtures. Dependencies are external, not bundled.

```sh
make
./build/the-bugger --help
./build/the-bugger --version
./build/the-bugger
```

For a reproducible first debug session:

```sh
make fixtures
./build/the-bugger
```

Choose **Linux application → Launch**, enter `./build/code-fixture`, then
**Review session → Start native**. In **Console → Command**, enter `start` to
ask GDB to stop at main. The fixture runs under GDB; do not pass `start` to it
as a shell argument. Ordinary Continue starts the loaded program without an
automatic main breakpoint.

Click Source for source stepping or Disassembly for instruction stepping.
Defaults are F5 Continue, F10 Over, F11 Into, Shift+F11 Out and F9 Breakpoint;
clickable actions remain available when a terminal intercepts keys. Outside
fields, `q` closes/backs out; `Ctrl+Q` quits, confirming active-session closure.
Memory has an inline address/expression field; variable values require deliberate
reads when automatic access safety cannot be established.

`./build/the-bugger --demo` opens the explicitly simulated visual reference.
It does not connect to hardware. In tmux, mouse support requires the opt-in
`set -g mouse on`; the application does not alter tmux configuration or bindings.

## Documentation

| Audience | Start here |
|---|---|
| Users | [Usage and configuration](docs/usage.md), including profiles, controls and limitations |
| Contributors | [Contributing](CONTRIBUTING.md), [architecture](docs/architecture.md), [testing](docs/testing.md) and [suite catalog](tests/README.md) |
| Product and design | [Requirements](docs/requirements.md), [roadmap](docs/roadmap.md), [interface specification](docs/requirements.md#interface-constraints) |
| Maintainers | [Maintenance/release procedures](docs/maintaining.md) and [validation records](docs/validation.md) |

The roadmap retains all M00–M18/F01–F47 gates. Post-release priorities progress
from safe editing and hardware/fault diagnosis to flashing/text channels, RTOS/
images and validated runtime analysis. Version assignments are planning targets;
advanced acquisition depends on actual hardware/backend evidence.

## Development checks

```sh
make test
make test-native
make test-tmux
```

These have different prerequisites and evidence boundaries. See
[testing](docs/testing.md#commands-and-prerequisites) for focused suites,
compiler/sanitizer/static-analysis checks and hardware validation. `build/` is
ignored/disposable; use a fresh `BUILD_DIR` for different compilers or flags.
