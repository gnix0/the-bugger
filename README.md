# the-bugger

A terminal-native C/C++ debugger frontend implemented in C, targeting Linux
x86-64 first and the STM32 NUCLEO-F446RE through OpenOCD second. GDB provides
the debugger engine through its structured MI interface.

**Current status: M00 foundation, awaiting manual acceptance.** The executable
supports help, target-version output, and a development-status message.
The launcher, TUI, debugging, profiles, and hardware support are not implemented.
`0.1.0` is the target version; it is not a declaration of release readiness.

## Build and run

M00 needs a C17 compiler (GCC or Clang) and GNU Make. Python 3 is needed for
tests; sanitizer support is needed for `make sanitize`; clang-tidy is needed
for `make analyze`. No debugger, TUI, or hardware dependency is needed yet.

```sh
make
./build/the-bugger --version
./build/the-bugger --help
./build/the-bugger
```

`--version` prints `the-bugger 0.1.0`. Bare invocation prints the M00 status
and exits successfully. Invalid arguments produce a diagnostic on stderr and
exit with status 1. These commands also work without a terminal.

```sh
make test
make sanitize
make analyze
make CC=clang BUILD_DIR=build/clang test
```

Artifacts are kept in ignored `build/`. `make clean` removes that directory.
The compiler defaults to `cc`; `CC`, `CPPFLAGS`, `CFLAGS`, `LDFLAGS`, `LDLIBS`,
`PYTHON`, and `BUILD_DIR` can be set on the Make command line. Use a fresh
build directory or `make clean` when changing compiler or build flags.
See [testing](docs/testing.md) for exact checks and manual acceptance.

## Development path

The approved workspace is a tiled six-pane layout: Source, Disassembly and
Registers above Memory, Locals and Console, with mouse interaction, tabs,
resizable splits and visible debugger controls. M01 introduces an explicitly
marked demo shell; real native debugging follows in M04 and inspection in M05.

Planned dependencies are ncursesw and pkg-config in M01, native GDB for M04,
then json-c, OpenOCD, compatible ARM GDB, and an ARM firmware toolchain in M06.
OpenOCD will remain optional for native debugging. Python is test tooling only.
Real STM32 validation is required before v0.1.0 can be accepted.

See [architecture](docs/architecture.md), [milestone ledger](docs/progress.md),
and the authoritative [project specification](AGENTS.md). Each milestone stops
for owner validation, followed by an owner-managed commit/push before further
implementation is authorized.

For the future TUI, tmux mouse handling will require the user's opt-in
`set -g mouse on`. The application will not change tmux configuration.

MIT licensing, contribution/security guides and a contributor-facing agent
guide are deferred to release finalization at the owner's request.
