# Tests

Run `make test` from the repository root. All tests and support code are C,
built with the same strict compiler flags as the application. No Python or
external test framework is required.

Pane-membership unit and workspace PTY cases cover add/remove, active/inactive
selection, retained scroll, pane independence and last-tab protection. Synthetic
target cases establish architecture coverage, not physical-board support.
See [testing](../docs/testing.md) for procedures and prerequisites; execution
records remain in the [validation records](../docs/validation.md).

- `test-openocd` / `make test-m16-model`: metadata catalog and bounded literal
  argument planning, board/advanced/explicit script ordering, search paths and
  configuration errors. Included in `make test`.
- `make test-m16-server test-m16-slow test-m16-tui`: authored server/MI peers,
  owned lifecycle/failure/timeout, explicit target actions and stop reconciliation,
  script-filter/review/live terminal flow, disconnect choices and signal cleanup.
- `make test-m16-openocd`: actual installed OpenOCD, ordered Tcl files with spaces
  and deliberate failure before adapter initialization; no hardware is exercised.
  See [OpenOCD procedures](../docs/testing.md#openocd-configuration-and-lifecycle).

- `test-remote` / `make test-m15-model`: validated TCP endpoints, remote IDs,
  ARM/endian/units/width metadata, known RAM/Flash region browsing, unknown-policy
  denial, refusal/timeout/cancel/disconnect failure and symbol incompatibility.
  Included in `make test`; no real debugger/server required.
- `test-remote-native` / `make test-m15-native`: actual external gdbserver,
  explicit attach, bounded buffer read, both connection protocols, optional
  symbols, reconnect/survival, server loss/refusal and live remote PTY UI/cleanup.
  Requires GDB/gdbserver, localhost sockets and ptrace; separate from native-only
  suites. See [remote procedures](../docs/testing.md#existing-remote-server-sessions).

- `make test-m14-model`: names-only automatic Call Stack arguments, deliberate
  selected-frame values, errors/obsolete replies and strict native mapping policy.
- `test-acceptance-native` / `make test-m14-native`: repeated integrated C/C++
  sessions, simultaneous inspectors, stale-state transitions, Save Log, owned
  cleanup and reported idle CPU/pump timing. The focused target also runs native
  context and its terminal test. Integrated acceptance is included in
  `make test-native`; ptrace is required. See
  [M14 procedures](../docs/testing.md#native-integration-and-hardening).

- `test-profiles` and `test-profile-sources` / `make test-m13-model`:
  strict versioned JSON/XDG storage, all connection definitions, literal ordered
  arguments/scripts/paths, key validation, private atomic failures, stale writers,
  corrupt-file preservation, recents/deletion/bounds and asynchronous source
  restore order/failure/no-retry behavior.
- `test-profiles-tui` / `make test-m13-tui`: file-only live launcher/profile CRUD,
  review, source options, remaps and inert remote/OpenOCD definitions, using an
  isolated XDG directory and no inferior. Included in `make test`.
- `test-profiles-native` plus `test-profiles-tui --native` /
  `make test-m13-native`: actual GDB source restore, relocated duplicate-basename
  breakpoint resolution, execution interlock, saved native reopen/remapped actions,
  recents and cleanup. Included in `make test-native`; GDB/ptrace required. See
  [M13 procedures](../docs/testing.md#profiles-and-action-keys).

- `test-watchpoints` / `make test-m12-model`: hardware type reconciliation,
  change/read/access modes, width/overflow checks, hit/scope messages,
  fallback/downgrade cleanup, malformed tables, context changes and ambiguous
  ownership. `make test-m12-slow` adds timeout/late-reply recovery.
- `test-watchpoints-native` and `test-watchpoints-tui` / `make test-m12-native`:
  real GDB hardware stops, scope expiry, enable/disable/delete, forced software
  rejection and live Memory/variable creation with mode/width review. The model
  is included in `make test`; native/TUI tests in `make test-native`.
  `make test-m12-tui` reruns only the terminal test.
  The TUI helper identifies lower pane headers, selects the inline Memory field
  instead of its column heading, waits for live bytes and targets footer controls
  rather than matching explanatory body text. Readiness/safety assertions remain.
  See [M12 acceptance](../docs/testing.md#hardware-data-watchpoints)
  and the ledger for actual run evidence.

- `test-variables` / `make test-m11-model`: authorization before value-producing
  requests, complete-footprint/region rules, unknown-region denial, explicit
  pointer inspection, bounded children/pages, formats/change baselines,
  optimized/unavailable values, stale replies and owned-object cleanup.
- `test-variables-native` and `test-variables-tui` / `make test-m11-native`: actual
  GDB C/C++/optimized Locals/Watches and live TUI add/read/format/expand/page,
  invalid calls, resize and terminal cleanup. `make test-m11-tui` reruns only the
  terminal portion. The model is included in `make test`; both native checks
  are included in `make test-native`. See
  [M11 acceptance](../docs/testing.md#locals-and-watches).

- `test-memory` / `make test-m10-model`: bounded independent instances, byte order,
  ordinary/unknown-region browsing, partial/invalid/overlapping replies, expression
  restrictions, obsolete replies and no opening/idle/running reads.
- `test-memory-native` and `test-memory-tui` / `make test-m10-native`: real buffers,
  inline field/Go, keyboard and mouse browsing, hex/ASCII, independent settings,
  invalid input, stale refresh, unmapped boundary and resize/cleanup. Both are
  included in `make test-native`; the model is in `make test`. See
  [M10 manual acceptance](../docs/testing.md#memory-inspector).
  Set `TB_MEMORY_TEST_TMUX=1` when invoking `build/test-memory-tui` with the binary
  and fixture paths to repeat that live workflow in its own isolated tmux server.

- `test-registers`: x86/ARM target-discovered names, sparse IDs, a 600-name fixture exercising the 512-ID
  catalog bound, long/unavailable values, numeric formats and canonical change
  baselines, obsolete format replies, frame/thread changes, errors/malformed
  responses and no automatic idle/running/hidden reads. `make test-m09-model`
  runs this focused C peer test; `make test` includes it.
- `test-registers-native` and `test-registers-tui`: actual GDB register discovery,
  RIP/PC agreement, instruction stepping, numeric format controls, stable change
  markers, Refresh, caller baseline reset and compact rendering. Run with
  `make test-m09-native`; also included in `make test-native`/`sanitize-native`.
  The native test establishes a real code_callee/main stack before caller
  selection, respecting GDB's default backtrace boundary at main.

- `test-images`: ELF32/64, both byte orders, code/data and ARM mapping symbols,
  relocation overflow, reset and 1,000 malformed/truncated metadata mutations.
- `test-breakpoints`: structured breakpoint rows, multiple/pending/hardware
  state, resource errors, run-to races/timeouts/cleanup failures and ownership,
  bounded malformed tables, no retry spin and aborted-execution reconciliation.

- `test-code`: synthetic GDB-known files, bounded source/assembly windows and
  navigation, mixed fallback, raw-path/display escaping, stale local-view replies,
  source byte/line limits and no automatic invisible/running/idle reads.
- `test-native-words`: quoted whitespace/backslash parsing, empty arguments,
  literal metacharacters and input bounds, without a shell or GDB.
- `test-context`: bounded coherent frame/thread snapshots via a synthetic C
  peer, numeric thread IDs unrelated to row indices, simple/optimized arguments,
  32-frame pages and a 128-thread limit, delayed/stale query and selection
  replies, rapid selection, thread exit, malformed/missing unwind, session reset,
  and no automatic reads while running, invisible or idle. This is model evidence,
  not proof of target functionality.
- `test-controller`: nonblocking fake-peer command/reply correlation, reordered/
  late responses, argument byte round-trip, async state, session/stop/context
  epochs, bounded queues, timeouts/backpressure, floods, EOF/protocol errors,
  startup/negotiation/fallback, TERM/KILL/reaping, descriptor exhaustion and
  isolation, literal executable paths, closed stdio and unrelated child survival.
- `test-controller-demo`: actual interactive diagnostic harness in a PTY,
  commands, stale/timeout/flood behavior and signal/terminal cleanup.
- `test-mi`: owned MI tree semantics, all record kinds, token/escape handling,
  duplicate names, embedded bytes, malformed input, CR/LF framing, EOF/recovery,
  bounds, deterministic byte mutations and exhaustive corpus fragmentation.
- `test-mi-alloc`: link-time malloc/calloc/realloc failure injection for partial
  tree/buffer cleanup, fatal errors and destroy/reinitialization. Uses the Linux
  linker wrapping facility; no allocation hooks are added to production code.
- `test-unit`: demo state transitions, bounded logs, tab closing and per-view
  state preservation.
- `test-layout`: tree ownership, unique pane IDs, geometry bounds/non-overlap,
  gutter coverage, recursive minimum sizes, split/collapse, compact restoration,
  76,800 size/focus cases, 12,000 seeded operations, capacity and pool reuse.
- `test-render`: exact palette, semantic colors, selected rows and monochrome tabs.
- `test-cli`: help/version, non-TTY diagnostics and invalid/literal arguments.
- `test-tui`: starts the actual ncurses application in an isolated PTY, drives
  keyboard and SGR mouse input, and reads its ASCII screen labels. Covers all
  three explicit --demo session previews, editing/retention, toolbar and function keys, reset
  confirmation, tabs/view selection, compact/tiny resize, direct/256-color and
  monochrome rendering, and normal/SIGINT/SIGTERM/SIGHUP terminal restoration.
  Launcher regressions check both-axis centering at 140x44 and 200x60, whole-row
  description clicks, suffix-free version and all three choices at 48x18.
- `test-workspace`: real PTY mouse press/move/release, global column/row and
  internal dividers, nested seventh/eighth views, tab/split closing, compact
  pane access, keyboard resizing and monochrome keyboard-only splitting.
  P00 regressions inspect actual inactive-tab inverse styling after promotion,
  repeated mouse switching, subsequent Enter and keyboard focus in direct color,
  256-color and monochrome.

PTY expectations and shutdown have five-second deadlines. Raw output retains a
bounded diagnostic tail; sanitizer evidence is checked before old bytes are
discarded, independently of the parsed screen. Each child gets its
own controlling terminal; no user's terminal settings are changed by the tests.
The small VT reader supports ASCII labels plus ncurses line/scroll-region
operations used when panes move and inverse attributes (excluding extended-color
parameters); it is not a
general terminal emulator or a visual-fidelity oracle. UTF-8 input remains a
manual acceptance check. Color tests need the xterm-direct, xterm-256color and
vt100 terminfo entries installed with ncurses on the development machine.

`make sanitize` runs the same C tests with ASan/UBSan in `build/sanitize/`.
LeakSanitizer needs execution outside ptrace-based execution environments. No check
is disabled to obtain a pass. `make analyze` runs clang-tidy over application
sources. See `docs/testing.md` for commands and the manual/tmux checklist.

UI/layout tests alone establish presentation evidence, not live debugging or complete
FR-06/07/08/09/10/11/12/13/14/15/16/17 acceptance. M03's synthetic GDB/MI corpus
is in `fixtures/mi/` with a provenance README; it establishes NFR-04 parser
evidence. M04's C fake peer tests controller behavior without launching a target;
real native C/C++ fixtures are in `fixtures/native/`, with separate M05
`make test-native` and `make sanitize-native` checks. M14 records native
integration acceptance; real board evidence remains M17. Every release retests native
x86-64 and the STM32/ARM validation, ESP32/Xtensa and ESP32/RISC-V hardware;
QEMU ARM/RISC-V adds coverage where available and never replaces board evidence.
After v0.1.0, native tests remain the regression foundation while new feature
acceptance emphasizes embedded capabilities and physical failure/recovery paths.
The [future validation matrix](../docs/testing.md#post-release-embedded-validation)
separates corpus/native automation from per-feature hardware requirements.
Native maintenance and shared improvements continue; no parity quota is added.

## M08 native browsing and breakpoint checks

`make test-m08-native` builds/runs `test-break-native` and `test-break-tui`
against actual GDB and the breakpoint C/C++/shared-library fixtures. This is
also included in `make test-native` and `make sanitize-native`.

Coverage: section boundaries, explicit raw zeros, sorted 128-instruction caches,
long-function backward scans, 100 browsing sequences, 100 breakpoint lifecycle
cycles, conditions/counts, pending library resolution, C++ multiple locations,
hardware-resource exhaustion and temporary Run-to cleanup. Existing source tests
also cover quoted relocated filenames, duplicate basenames and source Run-to.
The code TUI test repeats selector roundtrips at several positions and compares
the restored listing/viewport, gaps and warning visibility. Renderer checks
verify aligned mnemonic/operand columns and bounded formatting.

libelf development files are required. These checks require ptrace/PTY support;
synthetic ARM metadata is not hardware evidence. See [testing](../docs/testing.md) for procedures and the
[validation records](../docs/validation.md) for recorded results and failures.

## M04 fake peer and startup probe

`make test-controller` runs both controller suites. `make controller-demo` starts
an interactive line-command harness using `tests/support/fake_mi.c`; it is not
the product TUI and never launches a debug target. This C peer authors synthetic
records, not captured hardware data. It supports held/reordered replies, fake
run/stop/exit events, malformed/truncated output, unknown tokens, delayed reads
and simultaneous 2 MiB stdout/stderr floods.

The test-only `TB_FAKE_MODE` environment setting selects normal/fragmented
startup, missing-MI3 fallback, failed fallback, post-prompt failure, setup
rejection, no prompt, TERM resistance, early exit/signal and inherited-descriptor
checks. Production controller code neither reads this setting nor recognizes
fake commands. Test deadlines and a separate host child verify bounded cleanup
and ownership. See `docs/testing.md` for reproducible manual steps.

`make test-gdb-startup` is optional and requires native GDB. The resulting
`./build/test-gdb-startup arm-none-eabi-gdb` also works with an installed ARM GDB.
It tests actual prompt/setup negotiation, feature enumeration and MI exit only:
no executable, inferior, remote server or hardware connection. It is separate
from the default deterministic suite. Use a directly quoted executable path
when testing paths with spaces.

The startup probe now queries actual mi-async/non-stop values. M05 exposed that
quoting a CLI-derived -gdb-set setting name may produce ^done without changing
it. Tests retain this real-GDB regression and reject newlines in raw setting
tails. Ordinary MI parameters use safe ASCII tokens or escaped C strings.

## M05 actual native tests

`make test-native` requires actual native GDB and permitted ptrace. It runs:

- `test-native`: C/C++ launch, literal argv/cwd/env, output isolation, real
  Continue/Pause, console errors, log creation/preservation/short-write failures,
  normal exit, allowed/denied attach, detached process resumption, bounded
  output floods, GDB crash and owned/attached process policy.
- `test-native-tui`: bare launcher, configuration retention after missing GDB,
  live status without sample values, mouse actions, command/log forms, launch/
  attach confirmations, cancellation and SIGINT/SIGTERM/SIGHUP terminal cleanup.
- `test-context-native`: actual GDB with -O0 recursive/paged, -O2 inline/tail-call
  and stripped multithread C fixtures, selected frame/thread consistency, stopped
  PC preservation, simple arguments, thread exit, running invalidation and clean
  closure. Actual GDB frame names/arguments are retained in command output.
- `test-context-tui`: actual native workspace with Call Stack and Threads visible
  together, mouse and keyboard caller/thread selection, caller label, worker
  exit, stale running state and quit cleanup.
- `test-code-native`: actual source/AT&T/mixed assembly, source/instruction
  stepping and Step Out, several PC advances per source line, caller inspection
  and return to execution, read-only symbol lookup including overloaded C++
  address-of, optimized/stripped fallback, relocated duplicate source identities,
  literal spaces/quotes/metacharacters and GDB mapping replacement/clear.
- `test-code-tui`: direct Console start without preliminary browsing, honest
  Awaiting stop labels for Loaded targets, browse loaded files, actual source/PC
  and F10 modes retained across inspector focus, file filtering, source find/line/
  return, symbol/mixed navigation, source-directory form and clean exit.
  It also drags Source/Console horizontal thumbs, checks independent positions,
  keyboard clamping, one-cell matching vertical tracks, unchanged PC and bar removal when
  an enlarged source pane fits its content.
  Instruction regressions verify surrounding-code retention, stable viewport
  and five-row top/bottom stepping margins, actual Shift+F11 function return, and bounded
  backward navigation after returning to the caller. Synthetic offsets verify
  four preceding decoded instructions without guessed x86 boundaries.
  The native target passes the original debug-fixture as a second target to
  reproduce wheel-up at main's initial boundary; multiple visible rows and PC
  survive upward/downward wheel events. Synthetic coverage includes a backward
  result containing exactly one instruction merged with the forward window.

Application/harness code remains C. Only the deliberately debugged target
fixtures use C++. Session fixtures build with -g -O0;
context/code fixtures also cover -g -O2 and -O2 -s, all with no
sanitizer instrumentation; `make sanitize-native` instruments the frontend and
test harnesses with ASan/UBSan/LeakSanitizer. Instrumenting a target with LSan
would conflict with its deliberate execution under GDB/ptrace.

Native tests are separate from default `make test` so restricted/no-GDB hosts
can still run deterministic checks. They must be run and recorded separately
for native milestone acceptance. ptrace/pidfd/LeakSanitizer restrictions are
reported as failures, not silently skipped or disabled. The fixture's optional
per-process attach permission does not modify Yama or system configuration.

`unit/test_render.c` checks direct-color palette pairs, semantic source/register
colors, full-width selected rows and the monochrome active-tab indicator using
an ncurses screen backed by temporary files. It needs `xterm-direct` terminfo,
as does the existing PTY color test. Visual acceptance remains manual.
The standalone Alacritty capability-selection case additionally requires the
installed `alacritty` and `alacritty-direct` terminfo entries.

`make test-tmux` separately runs `integration/test_tmux.c` through an isolated
tmux server. It verifies actual tmux input forwarding for mouse drag, pane menu
clicks, tab styling/focus, splitting, F10, compact resize and exit. It requires tmux, a usable local
socket and `tmux-256color` terminfo; it never uses an existing user server.
It removes its temporary config/server even on a test failure. This optional
environment test is separate from the default and sanitizer suites.
