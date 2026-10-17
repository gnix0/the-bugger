# Changelog

## Unreleased — target 0.1.0

### Added

- M05 native Linux launch/attach through the launcher, real Continue/Pause and
  execution state, isolated program output, GDB command input and Save Log.
- Native-session ownership and confirmed exit policy, attached-process
  detach/resume, bounded escaped logs and Linux pidfd crash-cleanup checks.
- C real-GDB API/PTY tests and C/C++ debug-target fixtures, with separate
  native and native sanitizer targets.
- Explicit --demo for the preserved simulated workspace; live inspection panes
  report unavailable instead of presenting sample data.
- M04 nonblocking GDB/MI controller with literal argv spawning, bounded command/
  I/O queues, token matching, deadlines, stale-generation rejection and owned
  process cleanup. MI3 startup negotiates asynchronous/all-stop operation with
  a restricted startup-only MI2 fallback.
- C fake-peer lifecycle/pressure and interactive PTY tests, a separate fake
  controller demonstration, and optional actual-GDB startup checks without targets.
- M03 incremental GDB/MI parser with owned records, nested values, byte-string
  decoding, bounded buffering and line-boundary error recovery.
- C protocol/fragmentation/resource-limit and allocation-failure tests, synthetic
  fixture provenance, and a standalone diagnostic decoder via `make mi-demo`.
- M00 C17 foundation with help, target-version and development-status output.
- Make build, CLI integration checks, sanitizer and static-analysis targets.
- Architecture, testing and milestone acceptance documentation.
- M01 ncursesw demo launcher/configuration and six-pane Monokai workspace,
  with shared mouse/keyboard actions, view tabs/chooser and simulated controls.
- Capability-based color fallback, compact rendering and terminal cleanup.
- C unit/CLI/PTY tests replacing the Python CLI harness.
- Prototype-aligned Monokai surfaces, compact toolbar, styled tabs, semantic
  source/table colors and renderer palette/selection checks.
- M02 split-tree workspace with mouse-resizable shared/nested dividers,
  keyboard resizing, splits right/below, tab and nested pane closing, and
  compact access to all retained panes.
- C geometry stress tests, PTY workspace interactions and an optional isolated
  tmux mouse-forwarding integration test.
- Approved development roadmap through v1.0.0 with independent implementation
  and acceptance gates, documented deferred capabilities, and per-release
  native x86-64/STM32 ARM/ESP32 Xtensa/ESP32 RISC-V regression requirements.

### Fixed

- Serialize GDB's CLI-derived setting commands correctly; ^done alone did not
  prove settings were applied. Real probes now verify async/all-stop values,
  while native tests verify shell-free arguments and environment fidelity.
- Disassembly navigation can reach every instruction; view row counts are
  shared by rendering and input handling.
- Scrolling moves pane viewports directly, without a secondary row highlight.
  Source/disassembly highlight their current simulated execution positions.
- Remove the preloaded demo breakpoint; add clickable/draggable vertical
  scrollbars when pane content exceeds the viewport.
- Inactive tabs immediately lose the stale inverse highlight after switching.
  Focus follows the promoted active tab, mouse activation keeps normal styling,
  and subsequent keyboard activation addresses the intended tab.

Native launch/control/console are live; inspection, remote/OpenOCD workflows,
profiles, hardware acceptance and release readiness remain incomplete.
