# Changelog

## Unreleased — target 0.1.0

### Added

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

- Disassembly navigation can reach every instruction; view row counts are
  shared by rendering and input handling.
- Scrolling moves pane viewports directly, without a secondary row highlight.
  Source/disassembly highlight their current simulated execution positions.
- Remove the preloaded demo breakpoint; add clickable/draggable vertical
  scrollbars when pane content exceeds the viewport.
- Inactive tabs immediately lose the stale inverse highlight after switching.
  Focus follows the promoted active tab, mouse activation keeps normal styling,
  and subsequent keyboard activation addresses the intended tab.

No live debugger functionality or release is claimed by this entry.
