# The Bugger

<p align="center">
  <img src="docs/assets/base_layout.png" alt="The Bugger debugging workspace">
</p>

A terminal-native GDB frontend written in C17 for embedded firmware development,
with native Linux debugging support.

## Current capabilities and limitations

Debug **ARM/Cortex-M**, **Xtensa** and **32-bit RISC-V** firmware, or launch/attach
to native Linux **x86-64** programs. Physical testing covers these embedded
architectures using STM32 NUCLEO-F446RE, ESP32-S3 and ESP32-C6FH4 respectively.
See the [validation records](docs/validation.md) for configurations and results.

The tiled workspace provides source and mixed disassembly, registers, memory,
locals/watches, threads/frames, code breakpoints, hardware watchpoints, stepping
and GDB Console. Lower panes start minimized and expand with independent heights.
Source tabs, fullscreen and cached browsing keep large programs navigable.

Configure themes, keybinds and optional Vim-like input under **Config**.
Catppuccin Macchiato is the default; RGB-capable tmux sessions retain theme colors.
Connection profiles are separate from personal preferences. The live Setup menu
saves workspace definitions separately. Reviewed workspace/Source restoration,
Memory settings and Watch definitions are available in v0.1.4 development;
saved code-breakpoint installation is pending. See [setup saving/restoration](docs/usage.md#saving-a-setup).

Memory and variable inspection are read-only. Explicit-width MMIO sampling is
available for the validated STM32/OpenOCD setup; navigation never samples MMIO.
See [Memory inspection](docs/usage.md#memory-inspection) for access controls.

The latest release is **v0.1.3**; development is on **v0.1.4**.

## Build and first session

On Linux, install a C17 compiler, GNU Make, pkg-config and the ncursesw, libelf
and json-c development libraries. Native launch requires GDB 17+ and ptrace
permission. Embedded sessions need a target-compatible GDB and debug server.
OpenOCD is supported but optional. A C++ compiler builds test fixtures only.

```sh
make
./build/the-bugger --help
./build/the-bugger --version
./build/the-bugger
```

To try a native session:

```sh
make fixtures
./build/the-bugger
```

Choose **Linux application → Launch**, enter `./build/code-fixture`, then
**Review session → Start native**. Expand Console with `^`, enter `start` in
its **Command** field and press Enter to stop at `main`.

F5 continues, F10 steps over, F11 steps into, Shift+F11 steps out and F9 toggles
a breakpoint. Focus Source for source stepping or Disassembly for instruction
stepping. Ctrl+Q quits; active sessions require confirmation.

For embedded sessions, see [OpenOCD setup](docs/usage.md#openocd-sessions) and
[startup debugging](docs/usage.md#debugging-embedded-startup-code).
In tmux, enable mouse forwarding with `set -g mouse on`.
`./build/the-bugger --demo` opens a simulated workspace.

## Linux x86-64 distribution

`make release` prepares a binary archive, corresponding source archive and
`SHA256SUMS` under `build/dist/v<version>/`. Extract the binary archive and run
`./the-bugger` in a terminal. GDB, OpenOCD and shared libraries remain external;
`BUILD-INFO.txt` records the build host and runtime requirements.
See [packaging and release procedures](docs/maintaining.md#linux-x86-64-release-artifacts).

## Documentation

| Topic | Reference |
|---|---|
| Controls, setup and troubleshooting | [Usage](docs/usage.md) |
| Development | [Contributing](CONTRIBUTING.md), [architecture](docs/architecture.md) |
| Product scope and planned work | [Requirements](docs/requirements.md), [roadmap](docs/roadmap.md) |
| Tests and results | [Testing](docs/testing.md), [suite catalog](tests/README.md), [validation records](docs/validation.md) |
| Maintenance and security | [Maintaining](docs/maintaining.md), [security policy](SECURITY.md) |

## Development checks

```sh
make test
make test-native
make test-tmux
```

[CI](.github/workflows/ci.yml) runs tests and static analysis on pull requests.
Use a fresh `BUILD_DIR` when changing compilers or flags. See
[testing](docs/testing.md#commands-and-prerequisites) for prerequisites and
hardware procedures.
