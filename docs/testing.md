# Testing and validation

## Commands and prerequisites

Run from the repository root on Linux. Required build tools are a C17 compiler,
GNU Make, pkg-config, ncursesw/libelf/json-c development files and usable terminfo.
A C++ compiler builds debug-target fixtures only. Native tests additionally need
GDB 17+ for literal native launch arguments, Linux PTYs/pidfds and ptrace permission;
tmux checks need tmux and a usable
local socket. Tests use C without an external test framework.

```sh
make all fixtures
make test
make test-native
make test-tmux
make CC=clang CXX=clang++ BUILD_DIR=build/clang test test-native
make sanitize sanitize-native
make analyze
git diff --check
```

Use a fresh `BUILD_DIR` when changing compilers/flags. `make clean` removes ignored
`build/` artifacts. Strict C17 warnings include `-Wall -Wextra -Wpedantic -Werror`,
format, shadow and prototype checks. Project-owned ESP-IDF fixture sources also
explicitly use `-Wall -Wextra -Werror`, with private category overrides for SDK
warning-as-error exceptions; SDK components keep their own policy. See the
[fixture build instructions](../tests/fixtures/embedded/README.md#xtensa-fixture).
Sanitizer targets instrument the frontend
and C harnesses; targets debugged by GDB stay uninstrumented because LeakSanitizer
cannot operate in a deliberately ptraced target. Sanitizers run with leak checking.

`make analyze` enables clang-analyzer and bugprone checks and fails on warnings.
The Annex K replacement recommendation is excluded because Linux libc does not
provide those optional `_s` functions. Record missing tools and ptrace/socket restrictions.

| Layer | Entry points | Evidence boundary |
|---|---|---|
| Deterministic models and PTY UI | `make test`, focused `test-mNN-model` targets | Authored MI, owned-file/profile and terminal behavior; no real target required |
| Native GDB/API/TUI | `make test-native`, focused `test-mNN-native` | Actual Linux debugger/fixture behavior, not embedded support |
| Real tmux forwarding | `make test-tmux` | Isolated server, click/drag/keys/resize; physical appearance remains manual |
| Existing remote / OpenOCD | `make test-m15-model test-m15-native`, `test-m16-*` below | Actual server negotiation/ownership plus offline failure cases |
| Physical embedded | M17 and per-feature fixtures | Named board/probe/server/toolchain, capability and recovery evidence |

The [suite catalog](../tests/README.md) lists individual binaries; fixture READMEs
specify authored data and target behavior. Record results in the [validation register](validation.md) and raw logs under
ignored `build/`.

## Rendering stability stress

The native TUI and localhost-remote harnesses provide an opt-in, bounded stress
mode. `TB_RENDER_STRESS_SECONDS` accepts 1–300 seconds; ordinary `test-ci` runs
remain unchanged. The native fixture's `--render-stress` mode produces sustained
long output lines and hostile control-byte samples while the harness scrolls,
steps, resizes and checks fixed pane/control labels. The remote harness repeatedly
continues/pauses an owned localhost fixture, switches tabs and exercises compact
and tiled rendering. The tmux variant uses its own server and configuration.

Build the application, fixtures and harnesses, then run the three separately:

```sh
make BUILD_DIR=build/render-stress CPPFLAGS=-D_FORTIFY_SOURCE=3 all fixtures \
  build/render-stress/test-native-tui build/render-stress/test-remote-native
TB_RENDER_STRESS_SECONDS=300 TERM=xterm-256color build/render-stress/test-native-tui \
  "$PWD/build/render-stress/the-bugger" "$PWD/build/render-stress/debug-fixture"
TB_RENDER_STRESS_SECONDS=300 TERM=xterm-256color build/render-stress/test-remote-native \
  "$PWD/build/render-stress/remote-fixture" --tui "$PWD/build/render-stress/the-bugger"
TB_RENDER_STRESS_SECONDS=300 TB_CONSOLE_TEST_TMUX=1 TERM=xterm-256color \
  build/render-stress/test-native-tui "$PWD/build/render-stress/the-bugger" \
  "$PWD/build/render-stress/debug-fixture"
```

These are additional stress checks, not substitutes for the fresh fortified
baseline. They need working PTYs, ptrace and local sockets; they do not connect
to physical hardware. Record durations, rounds, results and full logs. The test
VT reader checks ASCII labels and fixed geometry, not every terminal feature or
physical appearance. Renderer unit tests inspect ncurses cells for control-byte,
invalid UTF-8, combining/wide-character and screen-edge clipping. Direct-terminal
and tmux visual review, plus the relevant real-board workflow, remain separate
acceptance evidence.

## Hosted CI

[CI](../.github/workflows/ci.yml) defines **Tests** on Ubuntu 26.04 with GDB 17+
and **Static analysis** on Ubuntu 24.04. They run `make test-ci` and `make analyze`
respectively. `test-ci` shares the complete host target list between local
preflight and CI: `test test-native test-tmux test-m15-native test-m16-server
test-m16-tui test-m16-openocd`.
GDB/ptrace, PTYs and localhost sockets must work on the runner. Preserve failing assertions during diagnosis. Test and
analysis logs are retained as workflow artifacts for seven days. Both jobs compile
test code with `_FORTIFY_SOURCE=3`; the Ubuntu 24.04 job compiles every unit,
integration and support C test source in addition to analysis. Local reproduction
of fortified tests uses `CPPFLAGS=-D_FORTIFY_SOURCE=3` and a fresh `BUILD_DIR`.

Before a code/test/build handoff, use an unused build directory and run the
complete Tests baseline, retaining the full output and exit status:

```sh
set -o pipefail
TERM=xterm-256color make BUILD_DIR=build/ci-preflight CPPFLAGS=-D_FORTIFY_SOURCE=3 test-ci \
  2>&1 | tee /tmp/the-bugger-ci-preflight.log
```

Choose another directory if `build/ci-preflight` already exists. This includes
native GDB, localhost servers, tmux and offline OpenOCD checks; it does not program
hardware. Record missing prerequisites or host restrictions as blocked checks.
Repeat the relevant checks after fixes, then complete the baseline; report the
actual local OS/tool versions separately from hosted Ubuntu results. For input
changes, preserve existing Enter, Backspace, field, frame/thread-selection and
terminal-restoration assertions as well as tests of the new shortcut.

Triggers cover pull requests and manual dispatch. Automatic push runs are
disabled, so each PR update schedules one Tests job and one Static analysis job.
There are no path filters that would leave required checks absent on docs-only
pull requests. Hardware access, sanitizers, slow-recovery cases and artifact
packaging remain separately documented checks. Hosted OpenOCD tests stop before adapter initialization.

## Validation strategy

Use units for parsing/layout/storage, deterministic MI for async races, stale
replies, resource limits and policy denial before dispatch, then actual GDB
fixtures for integration. Repeat with optimized/stripped binaries and literal
paths. Hardware tests add architecture/backend correctness, probe loss, reset and
recovery. A target's readable metadata is not proof of side-effect-free memory.

Test requests before `-var-create`, `-var-update`, evaluation or child expansion;
unknown is not unsafe, but is unauthorized for automatic reads without complete
safe-footprint evidence. Deliberate pointer inspection uses the same policy.
Use only known-safe disposable storage for destructive or MMIO-sensitive tests.
No arbitrary peripheral scanning or implicit flash/reset is a validation step.

Test terminal behavior directly and in tmux with opt-in mouse support. Record
TERM/terminal/tmux versions and intercepted keys. Fixtures never rewrite existing
tmux bindings or security settings. PTY screen parsing is an ASCII-label oracle,
not a general terminal emulator or proof of UTF-8/physical visual fidelity.

## Menu footer context

`make test-themes` includes PTY coverage of launcher, Config, Themes/preview,
Keybinds/capture, Profiles/options/entries/text, all three session forms/review,
and installed OpenOCD script browsing/filtering. It checks that navigation clears
stale notices while current errors and save results remain visible, including
DEMO, compact and NO_COLOR cases. Existing live tests retain operation-result,
field-editing and terminal-restoration coverage through `make test-ci`.

Manual review:

1. Open Config, Themes, Keybinds and Profiles from the launcher. Each footer
   describes the current menu; session-selection guidance appears only at the
   launcher.
2. Start and cancel key capture, then return to Config and open Themes. Capture
   messages must not follow into another menu. Save a theme or binding: its
   result remains visible on Config, then clears when another menu opens.
3. Open a session form, review it, return, and browse profile/script settings.
   Verify the guidance fits each page. Repeat at 48x18 and with NO_COLOR.

The same PTY checks assert right-aligned help and compact centered session forms.
`make test-keybinds` checks the short-terminal scrollbar, track paging, thumb drag
to the final rows and disappearance when all bindings fit. The render unit test
checks footer backgrounds against the workspace color for all 37 palettes;
existing direct/256/8-color/monochrome and real-tmux checks remain in the baseline.
The native Console TUI suite also checks centered close-confirmation controls.

For layout review, compare a tall terminal and 48x18: setup cards should fit their
fields/actions rather than fill unused height; menus and confirmations should be
centered. Status belongs at the left and help at the right, on separate rows when
needed, with no colored text backing. In a short Keybinds menu, page the track,
drag the thumb, and verify the final rows remain reachable. Widen/heighten the
terminal until all rows fit: the scrollbar disappears. Repeat with NO_COLOR and
inside tmux; verify field carets and click targets still match their controls.

## Source fullscreen

```sh
make test-fullscreen
make test-fullscreen-native
```

The first focused PTY test needs no debugger and is included in `make test`. It
checks Source-only content with header/footer, nested split and resized-divider
restoration, hidden hit regions, pane-switch suppression, compact resizing and
mouse/keyboard/Escape exit. The native target is included in `make test-native`;
it needs GDB 17+, ptrace/pidfd support and the ordinary code fixture. It enters
through live Source Actions, checks that toggling preserves the stopped source
line, steps with F10, restores the workspace and closes the owned session.

Manual review:

1. Stop a debug session and resize/split panes. In Source choose **Actions →
   Fullscreen Source**, or **... → Pane actions → Fullscreen Source**. Source
   fills the work area; only its controls and application header/footer remain.
2. Scroll, use arrows (or h/j/k/l with Vim-like input enabled) and step from Source. Normal source/PC markers and toolbar
   actions must remain usable; clicking an old divider position or pressing a
   pane-switch shortcut must not activate hidden panes.
3. Resize small and large. Restore with the visible button, Tab/Enter or Escape:
   the original split/tab structure and proportions return. Repeat directly and
   through tmux; retain existing input and terminal restoration checks.

These shared UI tests do not replace the physical release matrix or imply new
board support.

## Spatial control and content navigation

`make test-navigation` explicitly enables Vim-like input in an isolated personal preference fixture, then compares arrows/hjkl and checks geometry, toolbar edges,
control-only Tab traversal, content entry/exit, fullscreen focus recovery and
identity retention across compact resizing. It also compares Ctrl+arrow and
Ctrl+hjkl pane transfers across the six-pane layout and a nested split, with
edge stopping, viewport retention and fullscreen isolation.
`make test-fullscreen` and the
workspace PTY checks retain layout/restoration coverage. These require no target.
Live per-view checks remain necessary for horizontal scrolling and context rows.

Enable Vim-like input before comparing aliases in these steps; use Insert for
literal field entry.

1. Open a session: Continue is selected. Right/l selects Pause; Left/h returns.
   Down/j moves below the toolbar. At a row edge, focus remains reachable.
2. Traverse controls with Tab/Shift+Tab. Content, scrollbar tracks, dividers and
   hidden panes are never selected. Focus a neighboring tab with arrows and use
   Enter to activate it; verify promotion retains focus on that tab.
3. Click Source content and use arrows/hjkl/Page keys to browse. Expect the header
   `*` and CONTENT FOCUS help. Escape returns to the previous visible control.
   Repeat with Disassembly, Console and selectable context/variable rows.
   While content remains focused, use Ctrl+arrows and Ctrl+h/j/k/l to move to
   neighboring panes. The `*` follows visible geometry; ordinary arrows/hjkl
   still browse the destination. Existing scroll positions and active view tabs
   remain intact. Moving beyond an edge keeps the same pane, and Source versus
   Disassembly focus selects source versus instruction stepping without moving PC.
4. Reenter content, then click a toolbar control or blank space outside the pane.
   Arrow navigation returns to controls; wheel scrolling alone does not acquire
   content focus. Type literal hjkl into configuration and Memory fields.
5. Enter Source fullscreen, navigate past Quit and back, and cycle Tab. Restore
   remains reachable and focus never enters a hidden pane. With content active,
   the first Escape leaves content and the second restores the tiled workspace.
6. Resize compact/large, split panes and repeat in color, NO_COLOR and tmux.
   Focus follows visible control identity; viewports and PC remain independent.
   Repeat Ctrl+direction transfer with nested splits. In fullscreen or compact
   rendering, hidden panes must remain unreachable. Check Backspace and Enter in
   text fields: legacy Ctrl+H/J bytes act as pane movement only in content focus.

## Letter-key navigation

```sh
make test-navigation
```

This focused PTY test is included in `make test`; it needs no GDB or hardware.
Two demo sessions compare screen text and inverse selection after real arrow
sequences versus h/j/k/l. Cases cover the launcher, boolean controls, literal
field editing, theme/variant selection, workspace tabs and bodies, divider
resizing, confirmation controls and compact resize. The harness waits through
Escape recognition and drains redraws before comparing; a partial screen is
not a completed frame.

Manual review: enable Vim-like input in Config → Keybinds and Save. Navigate
menus with h/j/k/l, focus a tab with h/l and activate it with Enter; scroll
content with j/k and resize a divider along its axis. In fields, press i and
type `hjkl`: the characters must remain literal in Insert. Escape enters Normal,
and Escape again backs out. Actual arrows, Tab, mouse and function keys remain
available. Full native/tmux and hardware evidence require their respective runs.

## Themes and configuration

Coverage: F19; shared TUI/persistence behavior. No GDB, probe or target is required.

```sh
make test-themes
make test-m13-tui
```

`test-themes` builds the frontend and runs `test-preferences`, `test-render` and
`test-themes-tui`; it is also included in `make test`. Terminfo must provide
`xterm-direct`, `xterm-256color`, `xterm` and `vt100`. The profile regression
additionally builds its ordinary native fixture but does not start GDB unless
using the separate native target.

Automated checks cover all 37 palettes and their identifiers, RGB text contrast
of at least 4.5:1 and muted/accent contrast of at least 3:1 against catalog
surfaces, direct/256/8-color/monochrome pairs, semantic source/register rendering,
execution-only highlighting and scrollbars. Preference tests cover default,
private atomic persistence, stale writers, invalid schema/variants, mismatched
families and preservation of invalid files. PTY checks exercise centered Config,
dependent variants, every preview, cancellation without writes, save/restart,
mouse/keyboard controls and compact 48×18 monochrome rendering. These numerical
and screen-label checks do not establish physical visual quality or new hardware
compatibility.

Manual review:

1. Start `./build/the-bugger` with no existing appearance preference (use a
   temporary `XDG_CONFIG_HOME` to preserve personal settings). Open **Config →
   Themes**: expect centered menus and Catppuccin / macchiato.
2. Change Theme and Variant with keyboard and mouse. Each family exposes only
   its listed variants. Preview dark and light choices: sample data is labeled
   simulated; semantic highlights and text remain legible.
3. Back out of Preview: the saved colors return and the draft remains in Themes.
   Back to Config, reopen Themes: the unsaved draft is discarded. No preference
   file is created by previewing alone.
4. Save a choice: expect Config and a success message. Restart: the saved choice
   returns. Open **Config → Profiles** and load a configuration for review;
   appearance changes must not launch a session or alter profile contents.
5. Repeat directly and in tmux with mouse forwarding enabled; inspect each
   variant, compact/large resizing, monochrome selection indicators and an
   actual stopped debugging workspace. Record terminal/TERM/tmux details.

Existing native/remote and physical-board evidence remains separate. This gate's
recorded focused checks do not imply a full regression/sanitizer or hardware run.

## Existing remote server sessions

Coverage: M15; FR-05/16, shared context/inspection and external ownership.

```sh
make test-m15-model
make test-m15-native
```

The model suite requires no server or ptrace. It covers TCP endpoint validation,
opaque remote IDs, ARM/big-endian/unknown/non-octet metadata, target-width bounds,
known ordinary RAM/Flash browsing, refusal/timeout/cancellation, incompatible
symbols, dropped debugger transport and disconnect failure. Its peer rejects
unexpected target evaluations, automatic variable requests and unapproved reads.

The actual suite requires `gdb`, `gdbserver`, localhost sockets and ptrace. It
starts a harness-owned fixture/server, attaches explicitly over extended-remote,
reads a bounded known buffer, reconnects in both protocols with/without configured
symbols and checks survival after stopped and running frontend disconnect. It also kills its own
server to exercise loss/revoked reads and refused reconnect. The PTY portion
checks remote configuration/review/live panes, resize, confirmed disconnect and
terminal restoration. Server/fixture cleanup belongs to the test harness; this
is not frontend ownership or physical embedded evidence. The remote suite is
separate from `test-native` so native-only checks do not require gdbserver; model
coverage is included in `make test`.

Manual review with disposable Linux fixtures:

1. Run `make all fixtures`, then in another terminal run
   `gdbserver --once 127.0.0.1:3333 ./build/remote-fixture`. Choose Connect to GDB
   server, endpoint `127.0.0.1:3333`, ELF `./build/remote-fixture` and `gdb`.
   Review must identify an externally owned server and no programming action.
2. Connect. Expect LIVE REMOTE and a stopped target identifier, not a local PID.
   Use source/disassembly, registers, frames and Continue/Pause. No source mapping
   is an explicit fallback when symbols are unavailable. Resize and switch tabs.
3. Enter `&remote_buffer` in Memory and Go. Bytes/ASCII must match
   `REMOTE bounded memory`. Deliberately read another range through Options;
   unknown-region wheel browsing must not silently issue target reads.
4. Close and confirm disconnect. The frontend must never send a local kill to
   the target/server ID. `--once` can itself end the server on disconnect; for a
   persistent-server survival/reconnect check use `gdbserver --multi` and explicit
   Console `attach PID` to a separately started disposable remote fixture.
5. Try a refused port, incorrect GDB path and incompatible symbol ELF; expect
   retained inputs, actionable diagnostics and no local process cleanup against
   remote IDs. Connect without a configured ELF and retain honest symbol limits.
6. Stop the disposable server during the session; request a deliberate read if
   needed for GDB to notice the broken link. Old snapshots must not become fresh,
   and further Memory reads must be blocked until a new target is established.
7. Repeat keyboard/mouse entry and exit in tmux with opt-in mouse support.
   Cross-target RAM/Flash, target endian, real probe loss and server-specific
   halt/resume behavior require M17's named physical setup; no board passes here.

M15 regression commands (separate build directories):

```sh
make test test-native test-tmux test-m15-native
make CC=clang CXX=clang++ BUILD_DIR=build/clang-m15 test test-native test-tmux test-m15-native
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 \
  make CC=clang CXX=clang++ BUILD_DIR=build/m15-sanitize \
  CFLAGS='-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer' \
  LDFLAGS='-fsanitize=address,undefined' test test-native test-m15-native
make analyze
```

These are required checks, not execution claims. Record actual results in the
[validation register](validation.md#m15--existing-remote-server).

## OpenOCD configuration and lifecycle

Coverage: M16; FR-04/18/30. No hardware support follows from these offline checks.

```sh
make test-m16-model
make test-m16-server test-m16-slow test-m16-tui
make test-m16-openocd
```

The model suite uses authored temporary script metadata and installed catalog
paths, with no Tcl evaluation or server launch. It checks board dependency
exclusion, advanced and explicit ordering, literal arguments/search paths, missing
and duplicate scripts and managed/external configuration. It is included in
`make test`. Catalog results depend on installed files; custom search roots remain
supported when a distribution or installation prefix differs.

Server and terminal tests require localhost sockets, POSIX children and PTYs.
`fake-openocd` and `fake-mi` are authored protocol peers, not actual target/server
implementations. Cases include readiness, early/later exit, cancellation, flood
fairness/escaping, ignored TERM/KILL/reaping, port collision, missing tools, target
operations/errors, fresh stop/PC reconciliation and halted/resume disconnect.
The slow target adds the real five-second startup deadline. The terminal suite
checks browse/filter selection, argument review, explicit start, Reset confirmation,
Halt, close policy, compact resize and signal cleanup. No board is accessed.

`test-m16-openocd` requires an actual installed `openocd`. Two temporary Tcl files
verify ordered processing through a search directory and filename containing
spaces, then deliberately raise an error before adapter initialization. The test
requires failure and cleanup, never listener readiness or a usable target. This
is actual OpenOCD argument/error-path evidence, not hardware validation.

Manual configuration review:

1. Open Embedded target – OpenOCD and Browse boards. Filter for an installed board
   (the reference is `st_nucleo_f4.cfg` where installed). Select it and review:
   exactly that board `-f` must appear, without appended adapter/target scripts.
2. Back out and choose Advanced. Select adapter and target, then review their
   order. Add ordered custom scripts/search paths through Profile options; these
   override board/adapter fields and remain inspectable as literal arguments.
3. Test nonexistent scripts/tools and an occupied local port. Expect retained
   fields and an actionable diagnostic. With no probe connected, inspect actual
   OpenOCD probe failure logs; a listening port must not become target readiness.
4. Choose Start OpenOCD → No. Review must identify an external server and no
   owned process; choosing/loading a profile alone must launch nothing.
5. Repeat keyboard/mouse navigation, filter, review pagination and compact resize
   directly/in tmux. Existing native and remote suites must remain passing.
6. With M17's named physical setup, confirm real Halt/reset-and-halt, fresh PC and
   register display, no unsolicited programming, halt/resume disconnect choices,
   owned cleanup, external-server survival, probe loss and recovery. Record actual
   backend/script behavior; mark unavailable hardware UNVERIFIED.

## Personal keybinds

```sh
make test-keybinds test-themes test-m13-model
make all fixtures build/test-keybinds-tui
build/test-keybinds-tui "$PWD/build/the-bugger" "$PWD/build/debug-fixture"
TB_KEYBINDS_TEST_TMUX=1 build/test-keybinds-tui "$PWD/build/the-bugger" "$PWD/build/debug-fixture"
```

The first command checks parsing/defaults/reserved/conflicting keys, global
persistence, compatibility and editor interactions. The latter two require GDB,
ptrace and (for the second) tmux; they exercise remapped Continue/Pause, literal
Console input and spatial content transfer after loading a connection profile.
They are included in `test`/`test-native` and therefore `test-ci`.

1. Open Config → Keybinds. At ≥112 columns expect two Action/Key tables;
   at 111 columns expect one. Scroll or use Tab/Home/End/Page Up/Down to reach
   all 30 actions; repeat at 48×18 without inaccessible bottom controls.
2. Click Continue's Key cell or press Enter on it. Its old value disappears;
   only its border has capture emphasis. Press F11: a conflict keeps capture
   active. Ctrl+Q is reserved during capture, not a quit request. Escape restores
   the previous value. Capture F6: its edited border differs from capture.
3. Undo restores F5. With no history, click Undo: no screen/status/file change.
   Save is inert with no differences; Restore Defaults is inert at factory keys.
   Capture a binding and Delete to clear it; Undo must recover that binding.
4. Edit again. Back/Escape/configured Back and Ctrl+Q ask before discarding;
   Cancel retains the draft, confirmation discards it. Restore Defaults similarly
   asks before changing the draft, remains unsaved and can be undone.
5. Save F6 and restart. Load any connection profile: Continue still shows F6.
   Change theme: bindings remain. Test remapped navigation and content transfer,
   fixed arrows/Ctrl+arrows and literal aliases in Console/Memory fields.
6. Repeat direct capture, inactive buttons, confirmations and resize while
   capturing with NO_COLOR and through tmux. Only Save changes preferences.
   Connection profiles contain no keys, including after saving a legacy store.

The terminal screen reader recognizes single-cell UTF-8 box-drawing borders
forwarded by tmux, retaining their positions and attributes for border assertions.
The terminal harness isolates preferences in a temporary config directory by
default. Tests intentionally exercising persistence opt into their owned fixture
with `TB_TEST_CONFIG_HOME`; they do not use the developer's personal settings.

## Profiles and source settings

Coverage: M13; FR-01/06/14/28 and NFR-03/05/06/09/12.

```sh
make test-m13-model
make test-m13-tui
make test-m13-native
```

Model tests cover all connection kinds, literal ordered argv/scripts/paths,
XDG fallback, strict/corrupt schema, limits, ignored legacy keys, key-free writes, private
atomic writes, lock/write/stale-writer failures, recents/deletion and source
restore sequencing. File-only TUI tests use an isolated XDG directory and start
no target. Native tests require GDB/ptrace: restored relocated sources, duplicate
basenames, breakpoint resolution and execution interlock. Both are in
the full `make test`/`make test-native` path as appropriate.

Manual reproduction (use a temporary XDG directory to avoid personal profiles):

1. `make all fixtures`, then `profile_test_config=$(mktemp -d)` and
   `XDG_CONFIG_HOME="$profile_test_config" ./build/the-bugger` in the same shell.
   Keep that variable for inspecting/removing the test directory later;
   the application does not clean user profile storage. Choose Linux application,
   set `build/code-fixture`, and enter literal quoted arguments with spaces.
2. **Save profile**, name it, then **Profile...**. Add source directories and
   ordered build→local mappings; personal keybinds have no profile editor.
   Save again and verify replacement requires confirmation.
3. Return to launcher, choose **Config → Profiles**, load that name. Configuration is
   shown for review; no GDB or inferior starts on selection. Review/Start native,
   use Console `start`, then Continue or its clickable toolbar action. The
   successful saved session becomes recent. Close normally and reopen the profile.
4. For moved sources use the existing relocated fixture/mapping procedure below;
   save applied Source lookup settings and reopen. Execution waits for source
   restore; intended duplicate-basename source and breakpoint must resolve.
5. Confirm Attach reload does not retain a PID and environment overrides are not
   saved. Remote definitions retain endpoint/protocol and require Review/Connect;
   OpenOCD definitions retain script order and require explicit startup review.
   Loading either definition must not launch servers or connect.
6. Delete a profile: Cancel preserves it; Confirm removes it and its recent entry.
   In a disposable profile file, malformed JSON must show an error and preserve
   that file. Restore valid contents and reload/restart; do not silently overwrite
   corrupt data. Storage/concurrent-writer failures have deterministic model tests.
7. Repeat keyboard/mouse navigation and profile reopen
   directly and in tmux; inspect compact/monochrome controls. Never rewrite user
   tmux bindings. A terminal that intercepts function keys still has clickable actions.

No remote/hardware compatibility follows this checklist. Persisted definitions
are not permissions to perform target operations; M15/M16 must test their actual
application and lifecycle separately. F01 owns workspace/watch/breakpoint restore.

### F01 setup-definition storage

`make test-m13-model` includes definition/store foundation checks: setup-local
identities, Source role/identity selection, tree cycles, round trips, connection
snapshot separation, independent saved duplication, profile replacement/deletion,
schema-3 read-only recovery and stale-writer detection after a setup transaction.
It encodes a maximum 256-breakpoint definition containing escaped long locations,
checks that its actual encoding exceeds 1 MiB, and verifies save failure preserves
both disk bytes and the prior in-memory setup. Legacy schema checks remain in
the same suite. These checks do not establish workspace restoration.

Manual storage recovery review, using a disposable config directory:

1. Start with a temporary `XDG_CONFIG_HOME`, save a native connection profile,
   then close the application. Keep the profile file for recovery testing.
2. In that file, change `version` to `3` and add `"setups": [{}]` at the root,
   preserving `profiles` and `recent`. Start again. Config → Profiles must show
   **READ ONLY** and still list the saved connection. Loading it must reach
   configuration; Save profile must fail without replacing the file.
3. Change `setups` to an empty array and select Reload file. The read-only
   indication must disappear; profile saves work again. Merely loading must not
   downgrade version 3. Repeat at 48×18 and through tmux.

`make test-setup-capture` checks metadata-only capture, stable pane/Watch/Source
identities, explicit removal, reused pane slots, transient exclusions, oversized
drafts, skipped-definition replacement and active connection comparison.
`make test-m13-native` adds live association/save review, pinned Source selection,
profile preservation and independent saved duplication. `make test-setup-tmux`
repeats the profile workflow through an isolated tmux server; `test-tmux` includes
it. Both direct and tmux runs check widths 48, 80, 123, 124, 152 and 160 with long
custom shortcut labels, preserving action names and the former toolbar row count.
`make test-m08-native` verifies configured breakpoint intent across actual hits,
remaining-ignore decline and frontend edits, excluding temporary run-to entries.

Manual capture/save review with a disposable configuration:

1. Start a native session without a saved profile. Change pane splits/tabs, pin a
   Source file, add a Watch and edit Memory settings without reading. Save setup
   must offer Save profile. Save the profile, then review/save the setup. The
   asterisk must clear; no Memory/Watch read is granted.
2. Create a code breakpoint using the frontend. Set a condition and explicit
   ignore count, then run through hits. Saving must retain the original configured
   count and location. Temporary run-to and Console-created breakpoints are excluded.
3. Close/reopen the profile. Expect Restore saved setup selected in the additional
   session review, showing scope/connection differences and any ELF/context change.
   Start and verify split/tab/Source selection, Memory settings and Watch formats;
   lower views must remain minimized, with no Memory/Watch read permission.
   Repeat with restoration unchecked. Cancel Replace setup: saved definitions
   remain. Confirm replacement: hidden old definitions disappear; the connection
   profile remains unchanged. Duplication excludes unsaved edits.
4. Repeat in a direct terminal and tmux at the widths above. Inspect Setup,
   Session/Quit, action names, focus, text entry, Escape and shortcut operation.

`make test-setup-restore` uses an actual native fixture to check local split/tab
restoration, selected missing pins without prior contents, stable partial saves,
Memory/Watch definitions without permissions, retained enabled breakpoint intent,
capacity reservation, acknowledgment, an in-flight catalog cancellation and
running-target deferral until explicit Pause. It is included in `test-native`.
The direct/tmux profile workflows also cover default restoration consent, local
ELF metadata renewal, partial saves and explicit unresolved Source removal.
Definition/store checks retain legacy schema-3 records without metadata and reject
malformed metadata into read-only recovery.

Additional manual restoration checks:

1. Save a setup with two Source pins, selecting the second. Move/remove that file
   before reopening. The selected missing pin must remain in order, with `!`, no
   prior contents and explicit Retry/Remove under Setup. Repair its source lookup,
   refresh the source catalog and Retry; the same entry must resolve without a
   duplicate. Remove it and save: reopening must not resurrect it.
2. Save enabled code-breakpoint intent, then reopen. Installation is pending in
   this checkpoint: expect unresolved/awaiting stop, never installed. Execution
   must require acknowledgment. Acknowledging and later stopping again must not
   reprompt for the unchanged set. Partial saving retains its original definition.
3. While restoration is active, try execution, Reset, Console and thread/frame or
   source-setting changes. They must be blocked; local navigation/Pause/close
   remain available. Cancel during in-flight work: expect Cancel requested until
   settlement, then canceled, without rollback or further coordinator dispatch.
4. With an explicitly authorized disposable running target, reopen a saved setup.
   It must stay running and await a stop. Pause explicitly; pending work must run
   before execution/configuration changes become available. Native host tests do
   not establish this behavior on physical ARM/Xtensa/RV32 setups.

Code-breakpoint installation/attribute verification, address approval/provenance,
explicit import/reconciliation and physical restoration acceptance remain pending.

## Hardware data watchpoints

Coverage: M12; FR-25 and NFR-03/04/05/06/07/09/10. Native results do not establish
ARM/Xtensa/RISC-V hardware support.

```sh
make test-m12-model
make test-m12-native test-m12-slow
# Terminal-only rerun:
make test-m12-tui
```

The quick C peer covers all three backend types, width/overflow guards, hit
values, scope expiry, fallback/downgrade removal, unsupported hardware, context
changes, malformed tables, ambiguous ownership and cleanup failure without retry
spin. The slow variant adds timeout and late-reply recovery. Native tests cover
actual change/read/access stops, enable/disable/remove, local scope expiry and
forced software fallback. The TUI test covers variable/Memory entry points,
mode/width review, hits, rejected calls, resize and terminal cleanup. These are
test descriptions, not evidence that an unreported run passed. Run full native,
unit and tmux regressions separately; sanitizers/static analysis remain required
at integration/release gates with actual results recorded.

1. Build `make all fixtures`, launch `build/watch-fixture`, then Console `start`.
2. Add `watched_value` in Watches. Actions → Hardware data watchpoint opens an
   editable expression and mode review; Create explicitly permits GDB evaluation.
   Confirm hardware verification before Continue is allowed.
3. Continue: the value-change watch should stop at the write from `17` to `20`.
   Inspect the reported reason/value and the Breakpoints hit count.
4. In Breakpoints → Manage, disable, enable and remove it. Re-enable must verify
   its type again; removing it must preserve unrelated code breakpoints.
5. Read `&watched_value` in Memory. Options → Hardware watchpoint at selected row
   reviews the row's first-group address; choose the width and read/access mode
   explicitly. No watchpoint covers the full displayed range by default.
6. Reject a call such as `watch_checkpoint(0)` without running it. Unsupported
   modes/widths/resources must report backend errors rather than retain software
   fallback. A failed cleanup blocks execution and offers explicit Refresh.
7. Resize, navigate by keyboard, and repeat clicks/keys in mouse-enabled tmux.
   Exit normally and confirm terminal restoration.

M17: repeat supported modes and slot/width/alignment failures on real STM32/ARM
and ESP32/Xtensa/RISC-V targets. Only deliberately chosen safe fixture storage
is used; GDB may sample watched memory on creation/stops. Peripheral side effects
are not ruled out by using hardware watchpoints. Hardware rows remain UNVERIFIED
until real evidence is recorded.

## Locals and Watches

Coverage: M11; FR-10/27 and NFR-03/04/05/07/09/10/11. Name discovery does not grant value reads. Test
authorization before value-producing MI dispatch, not just before rendering.
Unknown and potentially side-effecting storage require explicit inspection;
complete read-free footprint proof and ordinary-region evidence are required
for automatic reads. Explicit pointer inspection follows the same policy.

Commands:

```sh
make test-m11-model
make test-m11-native
# Rerun only the terminal portion when diagnosing its interaction:
make test-m11-tui
```

Model cases cover complete/incomplete access proofs, unknown/side-effect region
overlays, span/byte limits, explicit pointer inspection, lazy bounded trees/pages,
formats/baselines, malformed and obsolete replies, context cleanup and no hidden,
running or unknown-region automatic value reads. Native cases exercise C, C++
and optimized fixtures. TUI coverage includes live launcher entry, add/read/format,
selected-row actions, expansion/pages, rejected calls, resize and cleanup.
The generic tmux suite tests forwarding; live M11 tmux acceptance remains a
separate manual procedure. Synthetic authorization tests are not board evidence.

1. Run `make all fixtures`, launch `build/variables-fixture` from the native
   launcher. In Console, enter `break variables_ready`, then `run`.
2. Open Watches, add `variables_global`. It should say Read required until
   **Read**; then show `17`. Cycle its numeric format and confirm the value agrees.
3. Add/read `variables_sample`, expand it, then expand/page `values`. Inspect
   `count`, `text` and array children; removing a watch removes its tree.
4. Add `variables_ready(0)`: the call/mutation restriction rejects it without
   executing the function. Invalid names give explicit errors on inspection.
5. Select frame 1 in Call Stack. Locals names belong to `variables_checkpoint`;
   explicitly read/expand scalar, aggregate, pointer and array values. Expanding
   a pointer is deliberate; a null/unavailable pointer must not fabricate children.
6. Continue to the second breakpoint. Values become stale and need a new
   deliberate read; `variables_global` is now `20`. Compare changed indicators,
   then Refresh in the same stop and confirm the baseline is not lost.
7. Switch thread/frame, tabs and pane sizes; confirm scope changes reset
   comparisons and old replies do not populate the new context. Close/relaunch
   without retaining old live values or authorization. Repeat keys/clicks/resize
   in mouse-enabled tmux; verify normal terminal restoration on exit.

Native checks do not prove MMIO safety. The M14 [request-policy audit](architecture.md#value-producing-request-audit)
covers automatic value paths and records corrected analyzer findings. M15 validates remote
authorization/unknown metadata before embedded use. M17 checks real target
variables and deliberately inspected RAM/peripherals on the release boards.
No arbitrary peripheral read is part of this native checklist.

## Memory inspector

Contract: persistent Address/expression + Go in the pane; no address-entry page,
fixed target address or mandatory SP default. No automatic reads on opening a
tab/stop. Inspect exact deliberate ranges; browse adjacent bounded blocks only
inside established ordinary-readable regions. Unknown/MMIO boundaries require
explicit Go. Metadata and byte order belong to session adapters.

Automated commands: `make test-m10-model`, `make test-m10-native`, `make test`,
`make test-native`, `make test-tmux`; focused ASan/UBSan model/native/TUI tests in
an isolated build directory. Strict warnings apply. Record actual results and
tool/environment failures. Coverage: M10; FR-09/20/26 and inspection safety.
Run the live Memory workflow in real tmux with
`TB_MEMORY_TEST_TMUX=1 build/test-memory-tui "$PWD/build/the-bugger" "$PWD/build/memory-fixture"`.
This creates and cleans its own mouse-enabled server; it does not change user tmux
configuration.

1. Build `make all fixtures`, run `./build/the-bugger`, launch
   `build/memory-fixture`, then Console `start`.
2. Restore Memory using its lower dock title or `^`; inline controls and the
   empty data area appear. Click the address field, type `&memory_buffer`, press Enter; repeat using Go.
3. Verify `THE BUGGER MEMORY` in ASCII and bytes `54 48 45 20`; horizontal scroll
   reveals long rows without hiding the toolbar. Change grouping/count and Refresh.
4. Wheel/arrow/Page keys reach the cached block edges, then fetch earlier/later
   bounded blocks within the process mapping. No automatic scans; unknown bounds
   give a readable navigation notice and keep the previous data.
5. Add Memory to another pane; inspect `&memory_other`. Addresses, drafts,
   grouping and scroll stay independent through tab switching and resizing.
6. Enter `0x1`, an unresolved symbol and `memory_checkpoint()`: readable errors,
   retained previous bytes labelled stale where appropriate, and no inferior call.
7. Console `break memory_checkpoint`, then Continue: inspect `memory_edge` for
   known prefix/partial-read warning. GDB may report a boundary byte differently;
   never invent missing bytes or claim exact hardware readability from this test.
8. Step/continue/pause: retained memory becomes stale; Refresh fetches explicitly.
   Close/relaunch: no bytes, region authorization or pending replies cross sessions.
9. Verify source/instruction stepping, breakpoints, Registers, Console, splits,
   q/Ctrl+Q, keyboard entry and terminal restoration; repeat mouse/key/resize in tmux.

M17 hardware-only: real STM32 RAM/Flash and safe target-region metadata, ARM
register/context agreement, OpenOCD errors/disconnect and controlled peripheral
behavior; repeat applicable workflows on ESP32 Xtensa/RISC-V. All are
UNVERIFIED until actual recorded evidence, regardless of simulated MI success.

## CPU registers

Coverage: M09; FR-08/20 and selected-context correctness.

1. Launch `build/code-fixture`; Console > Command > `start`. Registers discovers
   actual target names and stopped values. No fabricated Cortex-M sample values
   appear in a native session. All (ungrouped) is the current MI fallback.
2. Focus Disassembly and step F11. Verify changed registers, especially rip,
   have a `*` and changed-value color. Unavailable/bounded values stay explicit.
3. Click the format control through Hex, Decimal, Octal, Binary, Raw and Natural.
   These are read-only display operations; formatting alone must not create
   changes or clear a valid changed marker. Refresh rereads while stopped.
4. Add a code_callee breakpoint, Continue and select main in Call Stack. Registers
   follows the inspected frame and resets comparison baselines. Do not assume
   a caller above main: GDB normally ends its backtrace at main by default.
5. Continue/Pause and verify stale retained values while running, then fresh
   stopped values. Exercise scrollbars, long values, compact resize and a
   Registers-only visible workspace. Hidden/running/idle views must not poll.

Focused commands: `make test-m09-model` for synthetic model coverage,
`make test-m09-native` for actual GDB and PTY. They are also included in `make test`
and `make test-native` respectively; sanitizer targets inherit those suites.

## Disassembly browsing and code breakpoints

Coverage: M08 and pane-tab regression. Open Registers' `...` menu.
Registers and Peripherals alone start marked. Add Threads, reopen and remove it;
add it again and remove Peripherals instead. Threads stays active and Registers
remains available. Another pane's markers reflect only its own tabs. Check
active/inactive removal, keyboard access, compact menus and last-tab protection.
Unselected options have no empty-circle marker. Run `make test` for the added
unit and workspace PTY cases.

Release acceptance and real-board coverage require separate evidence.

1. Run `make fixtures`, then `./build/the-bugger`. Choose Linux Launch and
   `build/break-fixture`. Review/Start native; Console > Command > `start`
   stops at main. The fixture is intentionally silent and runs under GDB.
2. Disassembly > Actions > Go to address/symbol: enter `m08_long`. Scroll
   forward and backward through its long body. PC must not move. Return to
   Stopped restores execution context and the five-row follow margins.
3. Browse `_init`, then forward/backward across its section boundary. The
   five-byte .init/.plt gap in these Linux fixtures is labeled as bytes not
   decoded. Ordinary browsing must not decode that gap as application code.
4. At several scroll positions open/close `...`; addresses, cache and viewport
   must survive. Warnings describe only visible unknown/raw ranges. Columns
   align and horizontal scrolling retains complete bytes/operands.
5. Go to `m08_zeros`: normal inspection identifies data; Actions > Decode bytes
   explicitly exposes raw decoding. Repeated instructions are legitimate raw
   output, not filtered based on their appearance.
6. Toggle Source breakpoints with gutter/F9 and inspect the live Breakpoints
   view. Manage adds/removes/enables/disables entries, edits conditions and ignore
   counts, selects Default/Require hardware for new entries and refreshes state.
7. Add `m08_tick`, condition `value == 3`, Continue and verify the actual stop.
   Invalid conditions/resource errors must be readable and preserve prior state.
   Hardware availability is target-dependent; never infer a silent fallback.
8. Select the function's return line and Actions > Run to Selected. It stops
   there or reports an intervening stop. Existing user breakpoints survive;
   temporary state is cleaned up. Manage > Refresh explicitly retries failed
   cleanup; ambiguous ownership blocks execution rather than deleting guesses.
9. Check resize, narrow panes, tmux with opt-in mouse, and clean session exit.
   Check existing Registers, Memory, Locals/Watches and data-watchpoint controls for regressions.

Focused native command: `make test-m08-native`; broad commands remain
`make test test-native`, `make sanitize sanitize-native`, `make analyze` and
`make test-tmux`.

## Source and instruction stepping

Coverage: M07/M08; source/instruction mapping, navigation and non-mutating browsing.

1. Run `make fixtures` and `./build/the-bugger`. Choose Linux application / Launch,
   executable `./build/code-fixture`, no arguments. Review and Start native:
   expect Loaded. In Source click **Actions → Choose source file**. Filter with
   `code.c` and select its full path. Source is browsable before execution, with
   no invented execution marker. Disassembly explains that no stopped context
   exists. Escape returns from menus; fields accept ordinary text including q.
   Switch between **Current** and the opened file tab: Current is empty before
   execution, while the chosen file remains browsable. Reopening the same file
   selects its existing tab.
2. Console **Command** → type `start` → click **Send** (Enter also submits).
   Loaded / PID not started means GDB has not run the target; empty code panes
   say **Awaiting stop**, not STALE. Expect main's actual GDB line
   (the brace or initialization, depending on compiler), live source, assembly
   addresses/opcodes and matching execution markers. `>` marks actual stopped
   execution; `~` marks an inspected caller. No preloaded breakpoint appears.
3. Click Source, then use **Step over/F10** to reach the `code_callee` call.
   Use **Step into/F11**, then click Disassembly and step instructions with F10
   and F11. The mode indicator changes; Source may remain on one line while PC
   advances several instructions. Check x86 operands use AT&T `%register` / `$`
   syntax. Inspector/toolbar clicks preserve the last code-pane stepping mode.
   **Step out** (or reported Shift+F11) returns from the current function;
   it does not leave a loop inside that function. An infinite-looping function
   may therefore keep running until Pause. Buttons are available if
   the terminal intercepts a key. Let GDB report optimized/inlined mappings.
4. In Disassembly click **Mixed** (or Actions → Toggle mixed source/assembly).
   Expect interleaved source where available and assembly-only fallback elsewhere.
   Wheel/scrollbar and Up/Down/Page keys scroll; Left/Right pans long code lines.
   `<` / `>` in its footer page bounded instruction windows. No browsing action
   resumes execution or changes PC; no secondary execution-line selection appears.
5. In Source Actions, **Find in current file** → `CODE_FIND_MARKER`, then **Go to
   line** → `1`. The viewport moves without moving execution. In Disassembly
   Actions use **Go to address / symbol** → `code_callee` or a displayed `0x...`
   address. Calls such as `code_callee(1)` are rejected. Click **Stopped** to
   restore the original stopped thread/frame and synchronized code context.
6. Stop inside the callee again and activate Call Stack. Select a caller, then
   return to Disassembly: code follows the inspected frame while the real stopped
   PC stays separate. **Stopped** returns to execution. Continue/Pause and check
   stale running code becomes fresh only after the actual new stop.
7. Close the session and launch `./build/code-fixture-relocated`. Console `start`
   reports missing source. Source Actions → **Source lookup settings** → Add
   mapping: build prefix `/the bugger build tree`, local prefix the absolute
   repository path plus `/tests/fixtures/native/relocated`. After refresh expect
   actual source. Choose each `left/duplicate.c` and `right/duplicate.c` in Files:
   LEFT/RIGHT markers must match their full identities. Add directory and Clear
   overrides also report successful GDB application or a useful error. Settings
   can be saved through M13 profiles. Directory order is newest first; mappings show
   GDB's first-match order (replacing a prefix moves that rule to the end).
   With both duplicate files open, switch tabs and verify their distinct markers.
   Continue to a different stopped file: the opened tab stays on its selected
   source, while **Current** follows the new frame. Scroll each tab separately,
   close one with **x**, and check the other and Current remain available. In a
   narrow pane use **<**/**>** to reach a tab that does not fit on the strip.
8. Repeat `start` with `code-fixture-optimized`; mappings may skip/repeat lines.
   For `code-fixture-stripped` use Console `starti`: assembly/instruction steps
   remain usable, with explicit missing-source/symbol states. Resize, split an
   extra code view, and repeat the core clicks/stepping/scrolling inside tmux
   (`set -g mouse on`). Check the approved theme/tabs/layout remain intact.
   Quit and verify terminal restoration.
9. Keep a narrow Source/Disassembly/Console pane. Drag its horizontal thumb to
   reveal clipped text, click the track to page, and use Left/Right with its body
   focused. Source line numbers remain fixed; PC and pane size never change.
   Switch tabs/panes: horizontal positions remain independent. Vertical tracks
   and thumbs use one-cell targets matching the horizontal style. Enlarge a pane until its rows/columns
   fit: the corresponding bar disappears. Shrink it again and verify clamped,
   usable bars without covering content or footer actions. Repeat in tmux.
10. Relaunch without arguments and inspect the startup card:
    centered on both axes, theme-accented title, right-aligned version, subtle separators,
    three described choices and selected-row indicator. Click either a title or
    its description; both open that flow. Check arrows/Tab/Enter, Quit/Ctrl+Q,
    140x44, a larger terminal and 48x18 compact presentation. OpenOCD and existing-server execution require explicit review; recent saved native profiles are available after M13. The workspace
    after launch must retain its existing layout/theme.
11. Step instructions in Disassembly. Earlier instructions must remain browsable,
    with roughly four before the first PC where available. The viewport stays
    still through the middle, then scrolls to keep about five rows above and
    below PC where context permits (reduced in short panes). Browse until PC is
    near the top, then step: the top margin should return. Primary panes use the area above the lower dock. Restore a lower group, drag
    its top grip and confirm its height survives terminal shrink/restore.
    Wheel/Up/Down at a window edge can load a known earlier/next window; `<` / `>`
    footer controls remain available. Browse away and confirm PC never changes;
    stepping/Stopped returns to execution context. Test **Out Shift+F11** inside
    code_callee: the real Shift+F11 key or button returns to main. Terminal
    interception is distinct from a function that never returns.
12. Launch `./build/debug-fixture`, send Console `start`, and focus Disassembly.
    Wheel upward at the initial top boundary several times: the forward listing
    and current-PC marker must remain visible, including when only one earlier
    instruction was missing. Wheel down again: no blank/one-row collapse. Check
    the narrow vertical bar uses a vertical track while the bottom bar keeps its
    horizontal orientation. Known code/data boundaries and explicit raw decoding follow the browsing
    checks above; repeated byte patterns alone do not classify instructions.

Automated coverage: `make test` exercises synthetic code windows, mixed fallback,
caps, escaping, obsolete replies and invisible/running/idle reads. `make test-native`
adds actual -O0/-O2/stripped/C++ GDB code/stepping, caller inspection, safe symbol
lookup, relocated duplicate paths with spaces/quotes/metacharacters, mapping
replacement/clear, and the real TUI browse/find/line/mixed/settings workflow.
Full visual fidelity and physical terminal/tmux input require manual validation.
No remote, board or release acceptance is inferred from native tests.

## Native integration and hardening

M14 is accepted; automated results and evidence limits are recorded in the
[validation register](validation.md#m14--native-integration-acceptance). Commands
and manual checks below are reproducible procedures, not additional execution claims.

```sh
make test-m14-model
make test-m14-native
make test test-native test-tmux test-m12-slow
make analyze
make CC=clang CXX=clang++ BUILD_DIR=build/m14-clang test test-native test-tmux
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 make CC=clang CXX=clang++ BUILD_DIR=build/m14-sanitize CFLAGS='-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer' LDFLAGS='-fsanitize=address,undefined' test test-native
```

`test-m14-model` covers names-only automatic argument requests, both MI argument
name encodings, unsolicited values, selected-frame reads, rejection before ready/
while running/busy, errors and obsolete replies. Native mapping cases include
permissions, unsigned overflow, truncation, device and unknown special mappings.
`test-m14-native` adds repeated C/C++ sessions with simultaneous code, registers,
Memory and Watches; explicit frame values, step invalidation, Save Log and owned
cleanup. It also runs the recursive/optimized/stripped context and terminal tests.
Existing full suites retain attach, output-flood, crash and profile/watchpoint
coverage. Run in a host permitting ptrace; a sandbox denial is not a PASS.

The combined test reports host/kernel/architecture, one-second settled idle wall
and frontend process CPU time, and maximum pump duration with a 10 ms requested
wait. Inspector revisions must remain stable at settled idle. These measurements
do not include GDB CPU or measure terminal input-to-redraw latency. Record CPU,
compiler/instrumentation and terminal details with results; timing values are
observations, not a claim that every platform meets the NFR-02 response target.

Manual acceptance, directly and inside tmux:

1. Launch the context fixture, open Call Stack/Threads, select callers/threads,
   read arguments deliberately, refresh and resume. Names remain available;
   previous values never become fresh in another context.
2. Launch the variables fixture with Source/Disassembly/Registers/Memory/Watches.
   Inspect known globals explicitly, step, navigate Memory independently and
   create/remove a hardware watchpoint. Confirm stale indicators and current PC.
3. Split/resize panes, toggle their tabs, browse long listings and resize the
   terminal. Verify mouse/function keys and roughly 100 ms local interaction
   response while backend work is pending; record method and observed delays.
4. Save Log to a new file, reopen a saved native profile, close/restart sessions,
   then interrupt/terminate the TUI. Check terminal restoration and owned-process
   cleanup. Preserve externally attached processes.

Physical embedded compatibility and managed reset recovery remain
M16–M17 work; native tests do not establish those results.

## Call Stack and Threads

Coverage: M06; FR-10 and context coherence. Native tests are not remote or board evidence.

1. Run `make fixtures`, then `./build/the-bugger` without --demo. Choose Linux
   application / Launch. Set executable to `./build/context-fixture`; clear
   arguments and environment for this run. Review and Start native. Expect
   Loaded. This fixture deliberately raises SIGTRAP under GDB to provide stops
   without needing a configured breakpoint; do not run it standalone.
2. Activate **Call Stack**, the existing tab beside Disassembly. Click Continue
   or press F5. The fixture creates two workers and stops in its main-thread
   recursive call. Expect Stopped, three real threads in the context, and live
   frames including `context_checkpoint`, several `recursive` calls and `main`.
   libc frames may lack symbols/source; their names/addresses follow local GDB.
   Arguments initially show `<read required>`. Select a recursive frame and
   click Read Args: values such as `depth=0, worker=0` come from GDB. Refresh or
   select another frame: argument values require another deliberate read.
3. Click a caller's row. Expect its selection marker `>` and an **inspected
   caller** label. `*` continues to identify frame zero of the original stopped
   thread. Selecting frames never resumes or moves execution. With row focus,
   Up/Down browse and Enter selects; the underline is keyboard navigation, not
   another execution highlight. Scroll with wheel/scrollbar, resize the pane
   if needed, and check readable metadata and stable selected context.
4. In another pane open `...` and select **Threads** (wheel/Down to reach it if
   necessary). Expect three GDB thread IDs, names where reported, stopped state
   and top-frame information. Select a worker: the Call Stack changes to that
   thread and selected frame zero. The original stopped thread/PC remains
   separate from inspected context. Switch back to the original main thread.
   A seventh split view is optional; the six root groups and retained state remain.
5. Click Continue. Workers exit and the main thread stops again after joining
   them. Expect one thread; old workers must disappear. Continue once more:
   expect Running and explicit stale context, with selection unavailable until
   Pause. Pause again; expect fresh stack/thread data. Refresh while running
   must not halt the program or continuously read it.
6. Close the launch through `q` and its confirmation. Repeat with arguments
   `--deep`: use **Frames >** / **< Frames** to inspect 32-frame pages, select a
   caller on another page, then return without changing the selected frame.
   Pages have one lookahead, a 4096-level display cap and bounded work. At most
   128 threads are displayed; a limit notice appears if GDB reports more.
7. Repeat with `./build/context-fixture-optimized`, then
   `./build/context-fixture-stripped`. GDB may report inline/tail frames,
   optimized-out arguments, fewer frames or unknown functions/source. Those
   results must remain honest; do not require the -O0 stack to be reconstructed.
   With missing unwind information, any available thread/selected-frame context
   remains usable and errors are visible with a Refresh action.
8. Repeat the ordinary Call Stack/Threads click, keyboard, scroll, resize and
   quit workflow inside tmux with opt-in mouse support. Check `--demo` still
   shows the simulated reference separately.

Automated scope: a synthetic C peer supplies bounded pages/arguments, nonordinal
thread IDs, missing/malformed unwind data, 128-thread truncation, delayed query
and selection replies, rapid changes, thread exit, reconnect, and idle/running
read checks. Actual GDB runs recursive/multithread -O0, -O2 and stripped C targets,
checks stopped-PC preservation, selected caller/thread consistency, arguments,
worker exit, pause/resume and cleanup. A real TUI PTY test exercises row clicks,
keyboard selection, caller labels, two simultaneous context views and stale
state. The peer is not hardware evidence; the VT reader is not visual acceptance.

## Native launch, attach and cleanup

The native TUI runner includes inline Console command-entry checks with real
GDB. `make test-native` also runs its focused Console checks through an isolated
tmux server. To repeat only that tmux case after building the native runner and
fixture (substitute your build directory if different):

```sh
TB_CONSOLE_TEST_TMUX=1 build/test-native-tui "$PWD/build/the-bugger" "$PWD/build/debug-fixture"
```

Run `make fixtures` and `make test-native` first. These separate real-GDB tests
require native GDB, a C++ compiler for the target fixture, Linux PTYs/pidfds and
ptrace permission. Default `make test` remains deterministic and board/GDB-free.
`make sanitize-native` instruments the frontend and harnesses, keeping target
fixtures uninstrumented so LeakSanitizer does not run inside the ptraced target.
In restricted execution environments, real-GDB tests may need execution outside the
ptrace sandbox. No global permission/sysctl setting is changed.

1. Run `./build/the-bugger` without --demo. Choose Linux application / Launch.
   Use `./build/debug-fixture` as the executable, `/tmp` as working directory,
   `"two words" "" '$HOME;$(false)'` as arguments and
   `THE_BUGGER_TEST="hello world"` as environment. Review session, then Start
   native. Expect Loaded, no program PID yet, live Console and code panes
   awaiting the first stop. Memory controls remain accessible without a location;
   inspectors must not fabricate target values. No Cortex-M sample data,
   simulated registers, instructions or execution PC may appear.
2. Click Continue or press F5. Expect Running and an actual PID. Console should
   show the program's directory, literal arguments/environment and stderr marker.
   To explicitly test control-byte capture, add `--control-bytes` to Launch
   arguments: escape/OSC bytes must appear as text, not terminal commands. Do
   not use that test-only flag for a standalone fixture. Attached stdout is
   deliberately different: it stays with that process's existing terminal.
3. Click Pause. Expect an actual GDB stop and Stopped. In Console focus the inline Command field,
   enter `info program`, Send. Try `this-command-does-not-exist` and verify a
   readable error with a usable session. Then `echo console-ok\n` should work.
   Input uses ordinary arrows/Home/End/Delete/Backspace/Ctrl+U and Tab/Enter.
   Verify that output and the other panes remain visible during entry. Send
   `set mi-async off`: the frontend must reject it and retain the draft. Clear
   with Ctrl+U, send `echo hjklq123[]\n` with Send, and verify literal output and
   an empty field. Type an unsent draft, Escape, switch tabs/open and cancel Save
   Log, then resize to compact and back: the draft must survive. A second Console
   pane must have an independent draft. Tab must reach the field; Home/End should
   keep the visible caret and long draft aligned. Repeat with a live log flood
   and inside tmux; scrolling output must not move the command field.
4. Choose Save Log and enter a new writable path. Expect success, retained
   readable log text and mode 0600. Repeat that existing path and try an invalid
   directory: expect errors with input retained and no overwrite. Pick a new
   path to retry. Logs contain bounded retained history, not all past output.
5. Scroll Console using wheel, scrollbar and keyboard; return to its end to
   follow new output. Resize/split panes and switch views. The established tiled
   workspace remains usable, and unavailable views never display demo values.
   Stepping and breakpoints operate after a stop; Reset remains unavailable in native sessions.
6. Click Session or Quit with the native session active. Verify the launch
   termination confirmation; Cancel preserves the session. Confirm closes GDB
   and the frontend-launched program, restoring the terminal. Repeat Ctrl+C,
   SIGTERM or SIGHUP cleanup. A running program must not remain orphaned.
   Also verify `q` closes the current screen outside text fields: from the
   workspace it asks to close the session and returns to the launcher after
   confirmation. In a confirmation, `q` cancels. At the launcher it stays open.
   `Ctrl+Q` quits the application, asking before closing an active session.
   Commands and filenames containing `q` remain editable; Escape leaves inline
   command editing with its draft retained, or cancels a separate input dialog.
   The footer must make the distinction visible.
7. In a second terminal run `./build/debug-fixture --allow-attach` and note its
   printed PID. In the launcher select Attach, enter that PID, optionally choose
   its ELF, review and start. Expect Stopped. Quit now asks to detach/resume;
   after confirmation the fixture should survive and print new ticks. Stop the
   fixture in its own terminal afterward. --allow-attach changes only that
   fixture's ptrace permission; --deny-attach is the real denial fixture.
8. Repeat with a nonexistent GDB path or bad ELF/PID, then correct it. Expect
   actionable errors and retained configuration. Try a legitimate permission
   denial without changing ptrace settings. Repeat the native mouse/keyboard
   workflow directly/in tmux with opt-in mouse support.
9. Run `./build/the-bugger --demo` for the unchanged visual reference. It must
   remain explicitly simulated and separate from live state. OpenOCD and existing-server live flows are tested separately.

Automated scope: actual C/C++ launch, argv/cwd/env fidelity, output/control-byte
isolation, pause/continue, console errors, Save Log open/short-write failures,
normal exit, permitted/denied attach, external survival/resumption, bounded
output floods, GDB crash cleanup, live TUI actions/confirmations, input retention
and signal terminal restoration. Use the code/context/inspector procedures for stepping and optimized/stripped
inspection; the M14 integration procedure exercises the models together.

## MI controller diagnostic harness

This harness exercises transport/controller foundations, not live native debugging.
The standalone harness does not change the TUI and labels its peer/state FAKE.

1. Run `make test-controller`. Expect protocol/lifecycle/pressure checks and
   the interactive PTY harness suite to print PASS. No GDB or board is needed.
2. Run `./build/controller-demo ./build/fake-mi`. Wait for
   `state: ready (no target)`. Type `stop` and Enter, then `run` and Enter.
   Expect FAKE stopped/running states and correlated replies. Type `error`:
   expect a valid `class=error` reply while the session stays usable.
3. Type `hold` and wait two seconds: expect a timeout. Type `release`:
   expect its normal reply and an unmatched/stale late reply for the old token,
   rather than completing the timed-out request again.
4. Type `stop`, then `hold-context`, `context` and `release`, each followed by
   Enter, within ten seconds. Expect a stale completion for the held context.
   `hold-stop` followed by `run` and `release` similarly invalidates stop-scoped
   work. The ordinary release request still succeeds.
5. Type `flood` and then `error`. Expect input to remain usable and the error
   reply to arrive. Only the first four stderr chunks are printed, to keep the
   harness readable; this is not the application log-retention policy.
   Type `quit`. Expect closing, owned-child reaping and
   `cleanup: disconnected, no owned child`, then normal terminal behavior.
6. Run `./build/controller-demo ./build/fake-mi fallback`. Expect one MI2 retry
   before READY; quit. Run `./build/controller-demo ./build/fake-mi stubborn`:
   expect startup timeout and bounded TERM/KILL cleanup, or press Ctrl+C to
   request cleanup immediately (signal exit status 130). Repeat with a missing
   executable path: expect an actionable spawn error and exit 1.
7. Optionally run `make test-gdb-startup` with installed native GDB, then
   `./build/test-gdb-startup arm-none-eabi-gdb` with ARM GDB. Expect actual
   startup/async/all-stop/features/MI-exit PASS; no inferior, server, board or
   hardware connection is made. This optional tool is outside `make test` so
   the deterministic gate does not require external debugger installations.

Coverage: FR-16 lifecycle foundations; NFR-02/03/04/06/08/09/12. Tests exercise
token reordering, escaped arguments, unknown/late replies, queued and in-flight
generation changes, bounded queues, timeouts during backpressure, stderr/stdout
floods, EOF/malformed MI, startup/setup/fallback failures, EINTR, externally
reaped children, missing executables, literal paths with spaces/metacharacters,
closed stdio, partial descriptor exhaustion, repeated cleanup and unrelated
child survival. PTY tests cover harness commands and SIGINT/SIGTERM/SIGHUP
cleanup with unchanged terminal attributes.

The pressure test reports measured maximum pump wall time and checks per-turn
byte bounds and actual idle waiting. These are fake-peer transport observations,
not proof of whole-TUI latency or release performance. M14 adds integrated native
and terminal/tmux coverage; M18 retains the release performance/evidence gate. No new mouse/visual behavior is
introduced by this harness; workspace PTY regressions run separately.

## MI parser diagnostic tools

These tools test protocol parsing only. No GDB connection, process controller or
live TUI data is expected. Coverage is NFR-04 and parser ownership/error-path
foundations for NFR-03/09 and FR-16; complete session cleanup is not claimed.

1. Run `make test-mi`. Expect both corpus/semantic and allocation-failure suites
   to print PASS. Run `make mi-demo`: expect 22 decoded records, exit 0, retained
   token digits, nested frames/lists and byte lengths. Control bytes are printed
   as octal escapes rather than executed by the terminal.
2. Run `./build/mi-dump --chunk 1 tests/fixtures/mi/invalid.mi`, then `echo $?`.
   Expect six diagnostics with record byte offsets, recovered records 7 and 8
   (`20^done` and the prompt), and exit 1. stdout/stderr ordering may differ
   when redirected; each diagnostic identifies its record.
3. Run `printf '7^done' | ./build/mi-dump`, then `echo $?`. Expect an
   unterminated-record-at-EOF diagnostic and exit 1. Add a newline with
   `printf '7^done\n' | ./build/mi-dump`; expect one result record and exit 0.
4. Run `./build/mi-dump --chunk 0`, then `echo $?`. Expect usage and exit 2.
   A nonexistent input path also reports an I/O diagnostic and exits 2.
5. Optionally run the existing `./build/the-bugger --demo` demo: presentation and
   interactions should be unchanged. No board, probe or tmux configuration is
   required for parsing.

The corpus is replayed across every two-chunk split and chunk sizes 1–71.
Unit tests also cover CR/LF/CRLF, malformed and raw-NUL input, unknown classes,
duplicate fields, source paths with spaces, embedded decoded NUL/UTF-8 bytes,
all resource limits, deterministic byte mutations, recovery and EOF.
Link-time malloc/calloc/realloc fault injection walks every allocation on
representative nested/stream records and checks fatal state, cleanup and reuse;
ASan/UBSan/LeakSanitizer exercise those same paths.

For an optional actual-output compatibility probe, without a target:

```sh
printf '1-list-features\n2-not-a-command\n3-gdb-exit\n' |
  gdb --quiet --nx --interpreter=mi3 > build/mi-probe.mi
./build/mi-dump --chunk 1 build/mi-probe.mi
```

Repeat with MI2 or an installed ARM GDB. A GDB `^error` result is valid protocol
and must parse successfully. These probes are grammar evidence only, not native
or hardware debugging acceptance.

## Demo and visual regression

1. Run `make clean && make && make test`. Expect strict compilation and passing
   C unit, CLI and PTY suites. `--help` and `--version` also work without a TTY.
2. In a terminal at least 112x28 (140x44 recommended), run
   `./build/the-bugger --demo`. Expect the three clickable startup choices and a DEMO
   notice. Use arrows/Tab/Enter and clicks to select each session type.
3. Edit paths with spaces and UTF-8 text. Exercise Left/Right, Home/End,
   Backspace/Delete, Ctrl+U, Tab and Shift+Tab. Switch native Launch/Attach and
   embedded Board/Adapter+target using clicks or Left/Right/Enter. Inputs are
   preview text only; there is no tool/script discovery or validation yet.
4. Choose Review preview; check that fields retain your edits. Back returns to
   editing. Preview workspace opens the six default panes, with DEMO in each.
   Every preview uses the explicitly identified Cortex-M4F visual sample.
5. Compare palette, borders, tabs, toolbar and status with
   the [interface constraints](requirements.md#interface-constraints). There is no layout selector or named layout.
   All menus/configuration use pages or pane contents, without floating panes.
6. Click Continue/Pause, use F5/F10/F11/F9, and try Shift+F11 if your terminal
   reports it. Visible Out provides the alternative. Expect only sample changes,
   explicit simulated run state, PC markers, `*` breakpoints and `+` changes.
   Reset asks before resetting the sample; cancel preserves it.
7. Click secondary tabs and `...`; choose Watches, Call Stack or Threads and
   return to Source. Use Tab/arrows/Enter for the same changes. Symbols,
   Peripherals and Stack say NOT IMPLEMENTED rather than inventing values.
   Select source rows or click the gutter to toggle sample breakpoints. Wheel
   scrolls the pane under the pointer; a focused body supports arrows/Page keys.
8. Minimize the lower groups and resize to compact dimensions. Expect a single
   primary view and retained pane navigation. Restore two lower groups and verify
   that focusing an exposed group or its dock title raises it. Keys 1–9 and [/] reach panes. Below 48x18 expect a resize
   notice; expanding restores tab/view state. Test dragging/splitting below.
9. Quit using the visible button or Ctrl+Q. Repeat with Ctrl+C. Expect normal
   shell echo/cursor/mouse behavior afterward. Run `NO_COLOR=1 ./build/the-bugger --demo`
   and verify that text labels, selection and markers remain understandable.
10. Repeat startup, clicks, wheel, tabs, function keys and resize in tmux with
    the user's opt-in `set -g mouse on`. Do not change existing key bindings.
    Record terminal type, tmux version and any intercepted keys for the ledger.

Coverage: startup/demo, FR-13/17 and visual/input foundations. Physical mouse,
UTF-8 and visual fidelity require manual validation; PTY simulation alone does
not establish those experiences.

### Visual inspection checklist

Compare the renderer directly
with the [interface constraints](requirements.md#interface-constraints), using the component checklist in
[the terminal mapping](requirements.md#interface-constraints). Six panes or passing automated
checks alone do not satisfy the visual gate.

1. Run `make` and `./build/the-bugger --demo` at 140x44 or larger. Inspect launcher
   surfaces, descriptions, aligned configuration fields and retained editing.
2. Enter the workspace. Compare toolbar density, panel backgrounds/gutters,
   active/inactive tabs, table headings and panel footers against the HTML.
3. Click tabs and view menus. Check keyboard focus remains distinguishable
   from the active tab and that all controls still work.
4. Step with F10 and toolbar actions. Inspect source token colors, PC row,
   register names/values and yellow `+ changed` markers. All data remains DEMO.
5. Resize to 112x28, then 80x24, then below 48x18 and back. Check clipping,
   focused-pane navigation and retained state. Repeat inside tmux with mouse on.
6. Run `NO_COLOR=1 ./build/the-bugger --demo` and verify labels, underline, focus,
   breakpoints and change markers remain readable.

`tests/unit/test_render.c` inspects ncurses cells and exact direct-color pairs.
It does not substitute for inspecting the rendered interface.

## Lower dock and independent overlays

1. Start a demo or live session at 140x44. Expect three primary groups and three
   minimized lower title bars directly above the footer. No lower inspector body
   should become visible or request values merely because its dock is present.
2. Restore Memory, Locals and Console with their titles or `^` controls. Each
   should cover its column, leaving primary geometry and browsing state intact.
   Drag Memory's top grip; Locals and Console must keep their own heights.
3. Minimize/restore a group with `_`/`^`. Confirm tabs, selection, drafts and
   preferred height survive. Try keyboard pane IDs/cycling and resize mode;
   Tab selects visible dividers, arrows resize, Enter/Escape finishes.
4. Shrink to 80x24. Open groups overlap across the workspace. The most recently
   focused one is on top. Click an exposed part of another group or its dock
   title to raise it. Covered controls must not receive clicks or wheel input.
5. Restore 140x44 and confirm independent preferred heights return. Enter/leave
   Source fullscreen; restore must retain dock visibility, tabs and split trees.
6. Close/start another session. Expect all lower groups minimized and default
   heights on first restore; no values/permissions or pending requests carry over.
7. Repeat mouse, keyboard, resize and Console output in an isolated tmux session.
   These manual UX checks remain distinct from automated terminal-cell tests.

## Tiling, tabs and terminal resizing

1. Run `make test`, then `./build/the-bugger --demo` in a 140x44 or larger terminal.
   Choose Linux application, Review preview, Preview workspace. Compare the
   three primary panes and initially minimized lower dock; verify the six root
   groups retain their default tabs. Restore each group with its dock caret.
2. Drag each vertical column gutter and each independent lower top grip with the
   left button held. Expect continuous constrained resizing; release ends it.
   Move beyond a pane's minimum size and verify neighboring panes remain usable.
3. In Source, click `...`, then **Pane actions >**, then **Split right**.
   Expect a seventh visible pane with Disassembly, focused independently.
   Open its menu and choose another view. The original Source tabs/state remain.
4. Split a sufficiently tall pane **below**. Drag that internal horizontal
   divider, then an internal vertical divider. The rest of the grid stays tiled.
   If a split cannot fit, expect a visible diagnostic and unchanged workspace.
5. Add/switch tabs through Show views. Use **Close selected tab** in Pane actions.
   Other tabs retain their per-view scroll/selection. Closing the final tab is
   refused with a diagnostic. **Close split pane** expands its sibling; an
   unsplit root cannot close, preserving six root tiles. A nested original pane
   may close too, matching the HTML behavior.
6. Choose **Resize dividers** from Pane actions. Tab/Shift+Tab selects a gutter;
   Left/Right moves a vertical gutter, Up/Down moves a horizontal one.
   Enter/Escape finishes with the resulting sizes. Repeat without a mouse.
7. With seven or more panes, shrink to 80x24. Expect one focused pane, its ID
   and total pane count, and clickable **Prev/Next** controls. Use `[`/`]` to
   reach every pane; `1`–`9` selects a retained pane ID directly. Below 48x18,
   expect a resize message. Restore the larger terminal: splits, ratios, tabs
   and selection remain. Resize during a drag: it cancels safely.
8. Repeat inside tmux with opt-in `set -g mouse on`, and repeat in
   monochrome. Check wheel/menu navigation and F5/F9/F10/F11 after splitting.
   Quit and verify normal shell echo, cursor and mouse selection afterward.

The tree has a documented 32-pane capacity; each tiled leaf needs 20x7 cells.
Requested ratios survive terminal shrinking, while actual sizes are constrained
to fit all subtree minimums. Basic profiles are M13; persisted workspace is F01.
No layout selector or presets may appear. All debugger values remain DEMO.

`make test-tmux` is an additional C integration test requiring installed tmux.
It owns a uniquely named local server and temporary config, enables mouse only
there, sends real outer-terminal input through tmux, and cleans up that server.
It verifies tab styling/focus after promotion, clicks, held-button motion, splitting, F10, compact resizing and
terminal restoration. It does not modify an existing server or user config.
Restricted execution environments may prohibit the local socket.
Output goes to `build/m02-tmux-workspace.txt`. Physical visual/input validation
remains manual.

### Scrolling and execution markers

1. Open the default workspace. No breakpoint marker should appear until F9 or
   a gutter click. The current demo execution line is initially line 30
   (`gpio_init();`). F10 moves its highlight and execution marker to line 31;
   line 30 must lose the background highlight.
2. Scroll Source using wheel, body arrows or PageUp/PageDown. The viewport moves
   directly without another highlighted row. Try Memory, Registers and Console.
3. On overflowing panes, click the scrollbar track to page and drag its thumb
   between endpoints. Shrink/restore the terminal while browsing; keep clipping
   and scrollbar bounds valid. Repeat inside tmux and with `NO_COLOR=1`.
4. At 80x18, choose Disassembly and scroll to `0800043A` (the ninth sample
   instruction), then back to the first. Wheel and keyboard must reach both.

The C tests cover empty initial breakpoints, execution-highlight movement,
scrollbar geometry, track/drag interaction and disassembly endpoints. The test
screen reader handles cursor save/restore around ncurses scrolling regions.

### Tab focus and promotion

1. Run `make` and `./build/the-bugger --demo` in a 140x44 terminal. Choose Linux
   application → Review preview → Preview workspace. All values remain DEMO.
2. In the upper-middle pane, click Call Stack while Disassembly is active.
   Call Stack moves left and has normal active styling; Disassembly immediately
   has normal inactive styling, without a gray inverse highlight.
3. Alternate those two tabs repeatedly without clicking outside the pane.
   Each old tab immediately becomes inactive. Click Call Stack, press Enter:
   it must stay active rather than activating Disassembly at the old control index.
4. From that active tab, press Tab to focus the next tab. Keyboard focus is
   visible. Enter activates it; Left/Right focus neighboring controls, with Enter
   activating a tab and keeping focus on the promoted tab. Existing view content/scroll, splits and resizing still work.
5. Repeat with `NO_COLOR=1 ./build/the-bugger --demo` and inside tmux with
   opt-in mouse support. Monochrome active tabs retain their selection marker;
   inactive tabs have no stale marker after mouse activation. Quit cleanly.
C regression checks inspect actual PTY inverse attributes, not just label text,
in direct/256-color and monochrome. It fails against the pre-fix binary at the
old inactive-tab highlight. Additional checks verify leftward promotion, Enter
remaining on the clicked tab and visible keyboard focus. The VT reader skips
extended-color parameters when interpreting inverse attributes. Physical appearance requires manual inspection.

## Physical-board core workflow (M17)

M17 validates ARM/Cortex-M and Xtensa debugging on real hardware, using
NUCLEO-F446RE with ST-LINK and ESP32-S3 with native USB/JTAG respectively.
Task2.1 adds the [RISC-V core workflow](#risc-v-hardware-validation).
 Build instructions and non-destructive defaults are in the
[embedded fixture guide](../tests/fixtures/embedded/README.md).

Build offline without connecting or programming:

```sh
make test-m17-offline
```

The authored firmware provides a counter, SRAM buffer, Flash string, aggregates,
locals and nested calls. The optional STM32 LED image is separate; the default
fixture does not configure GPIO. ESP-IDF builds the S3 platform startup and the
same C workload. Firmware programming is external and deliberate. Record image
hashes; loading ELF symbols in The Bugger must never download firmware.

Start one explicit server per board. Use the matching installed script roots:

```sh
/usr/bin/openocd -f tests/fixtures/embedded/openocd/nucleo-f446re.cfg
# In an activated ESP-IDF environment, use its Espressif OpenOCD:
openocd -f tests/fixtures/embedded/openocd/esp32s3.cfg
```

The Nucleo configuration uses direct ST-LINK DAP/SWD for watchpoint stop reasons.
The installed HLA board script remains useful for connection testing but did not
supply a classified watchpoint stop in the recorded setup. S3 uses hardware-thread
awareness for stable core contexts through reset; RTOS task awareness is a separate
server capability. Never identify a task with a core by inference.

Build `build/test-board` and `build/test-board-tui` with `make test-m17-offline`.
An already-programmed target and an externally running OpenOCD are required:

```sh
TB_BOARD_LOG=build/m17/board-session-new.log build/test-board \
  arm-none-eabi-gdb 127.0.0.1:3333 \
  build/m17/stm32f446re/firmware.elf arm --reset-halt
build/test-board-tui "$PWD/build/the-bugger" arm-none-eabi-gdb \
  127.0.0.1:3333 "$PWD/build/m17/stm32f446re/firmware.elf"
```

For the S3, substitute `xtensa-esp32s3-elf-gdb`, the S3 ELF, and `xtensa` as the
architecture substring. Set `TB_BOARD_TMUX=1` for the terminal runner to use its
own isolated tmux server. Use a fresh `TB_BOARD_LOG` path for each backend run;
Save Log deliberately refuses to overwrite an existing file.

The backend runner explicitly executes halt/continue/step, hardware break/watch
and selected fixture reads. It never programs, guesses broad safe regions or
terminates an external server. `--reset-halt` additionally requests reset. It
checks source/disassembly, discovered PC/SP, explicit locals/aggregate reads,
caller selection without moving execution and independent SRAM/Flash snapshots.
Resume/leave-halted behavior is checked by reconnecting and comparing the actual
fixture counter after a bounded interval. A stopped flag without a current frame
and numeric PC must not complete reset verification.

Manual review on each named board:

1. Program the matching fixture deliberately; select its ELF and compatible GDB
   in the OpenOCD wizard. Review exact scripts, ownership and arguments.
2. Halt and compare source, disassembly, PC/SP and explicit Memory/variable reads
   with the actual fixture. Hex and ASCII must match. Select a caller and return
   without moving execution; exercise source/instruction stepping and Step Out.
3. Set/hit/disable/remove code and data hardware breakpoints. Preserve backend
   slot/width errors; do not silently downgrade to software watchpoints.
4. Inspect two independent Memory panes, then resize/split/switch views directly
   and inside tmux. Target values must become stale on execution transitions.
5. Confirm Reset deliberately. A fresh stopped PC is required; unavailable context
   must produce an error and block value reads, not manufacture a location.
6. Exercise both disconnect choices, profile reopen and externally owned server
   survival. Check logs and terminal restoration. Probe removal/recovery and
   firmware/symbol identity require separately recorded evidence.

Actual execution evidence is in [validation records](validation.md#m17--physical-board-validation).
Unperformed manual steps are not implied by automated terminal checks.

Subject to the explicit v0.1.0 physical RISC-V deferral, exercise the same applicable core workflow on the ESP32 validation/Xtensa and ESP32/RISC-V boards through the appropriate GDB/server. Native x86-64 is always retested. Exact board/probe details are recorded with actual evidence, not required during planning. QEMU ARM/RISC-V guest/native tests are supplementary when available. This is validation coverage, not an in-product simulator or a promise that hardware-specific features work on every target. See the [release matrix](#every-release-regression-matrix).

## RISC-V hardware validation

Task2.1 validates the shared RISC-V RV32 workflow on physical hardware, using
ESP32-C6FH4 as the test device and the existing board runners.
The [fixture guide](../tests/fixtures/embedded/README.md#risc-v-fixture) covers building
and the UART/native-USB distinction. In the matching activated ESP-IDF environment:

```sh
make all build/test-board build/test-board-tui
mkdir -p build/m17
openocd -f tests/fixtures/embedded/openocd/esp32c6.cfg
# In a second terminal, also with the ESP-IDF environment activated:
TB_BOARD_LOG=build/m17/c6-session-new.log build/test-board \
  riscv32-esp-elf-gdb 127.0.0.1:3333 \
  "$PWD/build/m17/esp32c6/the_bugger_m17.elf" riscv --reset-halt
build/test-board-tui "$PWD/build/the-bugger" riscv32-esp-elf-gdb \
  127.0.0.1:3333 "$PWD/build/m17/esp32c6/the_bugger_m17.elf"
```

Build the host runners as in the core workflow, create the log directory and
use a fresh log path. Set `TB_BOARD_TMUX=1` for the isolated tmux TUI run.
Tests deliberately control execution/reset and read known fixture objects; they
never flash. The checkpoint must have a decoded instruction and opcode bytes at the stopped
PC; the RISC-V fixture checks 2/4-byte instruction alignment. Manually check
profile reopen, visual appearance and physical unplug/recovery.

Known tool issues for this setup:

- OpenOCD `v0.12.0-esp32-20260703` crashes on the C6 `qCRC` path used by
  `compare-sections`. Verify image identity with GDB
  `dump binary memory FILE START END`, then compare each dumped section with
  its ELF bytes. Obtain exact bounds from that ELF.
- RISC-V GDB `17.1_20260402` displays application instructions as `.word` after
  loading `esp32c6_rev0_rom.elf` from ROM package20241011. Application and
  bootloader symbols decode correctly; avoid adding that ROM file for this setup.
- Reset destroys FreeRTOS task contexts. The fixture's `ESP_RTOS hwthread`
  setting keeps a CPU context available for fresh-stop verification.

[Validation records](validation.md#v014-task-21--risc-v-hardware-validation)
contain the reproductions and results.

## Embedded startup debugging check

Use the [startup walkthrough](usage.md#debugging-embedded-startup-code) with an
already-programmed disposable fixture and matching ELF. Record board/core, reset
wiring, probe, GDB/OpenOCD versions, scripts/arguments and firmware identity.

1. Connect without automatic programming/reset/resume. Deliberately confirm
   Reset-and-halt; record its fresh numeric PC and disassembly. On the STM32
   fixture, compare that PC with `Reset_Handler` in `startup.S`. Record whether
   it stopped before the first instruction or only later in startup. A server
   listening, a reset acknowledgement or a breakpoint later in startup is not
   evidence of first-instruction capture.
2. Where reset catch is supported and configured, step instructions through the
   actual startup code. For the STM32 fixture, follow `.data` copying and `.bss`
   clearing into `main`; check PC progression and explicit safe SRAM reads against
   linker symbols and ELF contents. Do not treat pre-initialization values as
   initialized C objects or read peripheral ranges speculatively.
3. Set a hardware breakpoint at the firmware's application entry, Continue and
   verify the actual stop. Remove the test breakpoint. Repeat reset-and-halt to
   distinguish repeatable early capture from an ordinary attach after startup.
   Cover Source navigation with assembly line information and instruction-only
   stepping when symbols/line information are absent.
4. For ESP32 and other backends, identify the actual ROM/bootloader/application
   boundary and available symbols. Record the earliest verified stop and stages
   already executed; do not apply Cortex-M reset-vector assumptions to them.
   If the backend cannot halt early enough, record that limit explicitly.
5. Close with the intended target disposition, confirm owned-process cleanup and
   external-server survival, and retain the session/server logs. Reset-catch or
   connect-under-reset setup changes need separate reviewed configuration; no
   generic startup-debugging check may silently add flash or reset scripts.

## Release artifacts (M18)

Prepare the Linux x86-64 host distribution from a reviewed checkout:

```sh
make release
(cd build/dist/v0.1.3 && sha256sum --check SHA256SUMS)
```

The [packaging procedure](maintaining.md#linux-x86-64-release-artifacts) describes
contents, extra packaging tools and dynamic-library boundaries. Verify both
archives outside the development tree:

1. Extract the binary archive into a temporary directory outside the repository.
   Run `./the-bugger --version`, `--help`, and interactive `--demo` there.
   Check launcher/workspace, input, terminal restoration and tmux behavior.
2. Run the existing CLI/PTY/workspace tests against the extracted executable,
   using absolute paths. Native, remote and model matrices remain separate;
   launcher success alone is not a live-debugging test.
3. Extract the source archive elsewhere and follow `make` from its own directory.
   It must build without files or generated objects from the original checkout.
   Compare the source-built and packaged version output.
4. Inspect ELF architecture, PIE, non-executable stack, RELRO/now flags and dynamic
   libraries with binutils/`ldd` on this trusted build. Record actual host and
   GLIBC symbol requirements, without claiming universal Linux compatibility.
5. Verify checksums, GPLv3 license, matching source identity, instructions and
   source completeness. Verify invalid packaging inputs fail. Preserve any
   unperformed hardware/manual acceptance checks as outstanding evidence.

An ESP-IDF shell can export `OPENOCD_SCRIPTS` for Espressif's installation. With
`/usr/bin/openocd`, clear an incompatible override before launching:
`env -u OPENOCD_SCRIPTS ./build/the-bugger`. The executable and scripts must match;
a listening socket does not establish successful target examination.

## Every-release regression matrix

Apply the explicit [version-specific physical RISC-V deferrals](requirements.md#release-acceptance):
v0.1.0 deferred it, and the maintainer separately deferred v0.1.3 RISC-V validation to
v0.1.4. Neither is a hardware support claim or a general waiver for future
releases. Record actual commands, results and release acceptance for the
applicable matrix below. Exact ESP/probe setup is not a
P00 prerequisite; fill it in when conducting the tests. Tool/server paths stay
editable; OpenOCD is not a requirement for every architecture.

| Environment | Minimum evidence | Current live evidence |
|---|---|---|
| Native Linux x86-64 | Launch/attach, stepping both modes, AT&T, code/data breaks, registers, RAM, variables/frames/threads, output/log/profile, cleanup | M05–M14 accepted; GCC/Clang native and tmux regressions plus full default/native sanitizers passed at M14. Per-release evidence and release acceptance remain required |
| ARM / Cortex-M | NUCLEO-F446RE fixture via GDB/OpenOCD; halt/step/break/watch, real code/register/RAM/variables, reset/disconnect/cleanup | M17 physical core/API/TUI/tmux PASS; exact setup and exclusions in validation records |
| Xtensa | Validation board via appropriate GDB/server; applicable core workflow and cleanup | M17 ESP32-S3 physical core/API/TUI/tmux PASS; other boards require evidence |
| RISC-V RV32 | Validation board via appropriate GDB/server; applicable core workflow and cleanup | Accepted ESP32-C6FH4 core/API/TUI evidence in Task2.1. Historical release deferrals remain recorded; other targets need evidence |
| QEMU ARM / RISC-V | Supplementary reproducible native/guest runs where available | UNVERIFIED; not a physical-board substitute |

For each release, capture the actual setup/version, relevant fixture/source
identity, core-workflow results, capability-specific exclusions, new-feature
regression and cleanup. Unavailable hardware remains UNVERIFIED and is brought
for resolution before release acceptance. Tests are not waived because a release
only changes UI. QEMU test infrastructure does not add simulator support to the
product. Do not claim unsupported target-specific features or untested boards.

## Post-release embedded validation

After v0.1.0, native fixtures remain a required, repeatable regression foundation;
feature acceptance increasingly requires real embedded evidence. No native-only
feature parity goal is imposed. M14's complete native acceptance and M15–M18's
remote/hardware/release criteria are unchanged. The F ordering is in
[the roadmap](roadmap.md#release-progression-and-priority-changes).

Every hardware-dependent gate plans both automation and physical acceptance
before implementation, recording exact board/core, probe, GDB/server versions,
firmware/build identity, configuration/argv, capability limits and logs. Captured
MI is labelled captured with provenance; synthetic exchanges are labelled authored.
Test unsupported capabilities, obsolete replies, disconnect/reset/reconnect,
ownership, responsive cancellation and stale-value invalidation. No corpus,
QEMU run or successful native test substitutes for the named physical check.

| Gates / planning target | Automated validation available without feature hardware | Physical acceptance and required setup |
|---|---|---|
| F01/F02, v0.1.x | Schema/state migration, unknown stack bounds and bounded/partial reads; native stack fixtures. | Release boards for restored definitions and stack context; known bounds from actual firmware metadata where used. No invented usage/high-water result. |
| F03–F05, v0.2.0 | Write authorization before dispatch, widths/endian, partial failures/read-only/context changes, no collateral writes. | Disposable RAM/CPU-register fixtures on STM32 and applicable ESP32 setups via tested GDB/server. MMIO writes require deliberate documented safe access; Flash remains F22. |
| F11/F34/F35, v0.3.x | Register/description variants, malformed SVD/inheritance/arrays/access modes, side-effect-policy denial before reads; fault combinations/corrupt exception frames. | NUCLEO-F446RE/GDB/OpenOCD for safe peripheral inspection and injected Cortex-M faults; check metadata against actual device/firmware. ESP32 metadata only with tested descriptions/backends; Cortex-M fault decoder unavailable there. |
| F07–F09/F12, v0.3.x | Bounded symbol/duplicate-name queries, no automatic unknown reads, relocation/import validation, exclude personal/runtime state. | Reopen/share an actual embedded setup and inspect intended symbols without programming/resetting on load; existing release boards/tools suffice. |
| F13, v0.4.0 | PTY RX/TX, line endings/echo, configuration failures, fragmented/control bytes, queue/history bounds, backpressure, unplug/reconnect and ownership. | Known firmware output and command/response through a named UART/USB-serial endpoint; verify framing/flow control and reset-line behavior actually claimed. See the F13 procedure below. |
| F14–F16/F18, v0.4.x | Stream fragmentation/loss/routing, clocks, bounds, file errors and reconnect/ownership. | Each channel needs suitable firmware plus an RTT-capable backend, semihosting configuration or SWO/ITM-capable core/probe/server. Do not assume every board/probe supports every channel. |
| F22, v0.4.x before text-channel integrations | Programming sequence/confirmation/timeout/cancel/error/reset policy; no dependency on UART/RTT/semihosting/SWO. | Expendable firmware on each claimed programming backend; program/verify and test recovery on approved validation boards using established M16/M17 tools. Preserve independent attach and untouched unrelated memory. |
| F20/F21, v0.5.x | Bounded region-safe search, export and diff identity; no broad unknown/MMIO scan. | Known RAM/Flash fixture ranges on tested release boards; verify exact bytes, gaps and snapshot identity without collateral access. |
| F31/F33/F23/F24/F26, v0.6.x | Kernel version/port layouts, corrupt lists, image overlap/relocation, task-ID lifetime, unsupported stops/logpoint intrusion. | One named RTOS/version/port per supported setup and a bootloader/application fixture where images are claimed. Probe/server must expose valid task/context/stop operations. |
| F29/F41/F32/F27/F28, v0.7.x | No hidden halt/resume, region authorization, rates/gaps/units/overflow, plot bounds, explicit slow-watchpoint/PC-change failures. | Proven non-halting acquisition/counter source for claimed target; suitable text telemetry can feed plots independently. Run disruptive execution overrides only on disposable fixtures; verify actual backend behavior. |
| F10/F30, v0.8.x | Printer absent/error/raw fallback, authorization for complete value footprint before requests and bounded automatic discovery. | Supported embedded C++/debug-info/printer configuration before claiming convenience; native regressions remain useful. No printer availability promise across toolchains. |
| F36–F40, v0.8.x–v0.9.5 | Decoder corpus, timestamps/synchronization/loss, attribution/statistics, coverage completeness and measurement units. | A proven trace/event/sampling source per consumer; F37 needs instruction-trace-capable hardware, F40 an actual measurement device. Current board ownership alone establishes none of these capabilities. |
| F42/F43/F45/F46, late v0.9.x | Hook order/failure/cancel, bounded timers, per-core/inferior context/address-space/ownership isolation. | Supported script/lifecycle target; multicore needs a demonstrated multicore board/backend, multiple inferiors a justified real workflow. No general native process-feature expansion implied. |

F06/F19/F44 remain shared TUI/persistence gates: automate geometry/input/parse
and fallback tests, then manually inspect directly/in tmux with color and small
terminals. They do not need special probes, but their releases still need the
every-release hardware regression matrix. F17/F25 are late scope-review native
candidates; do not create new native suites for unapproved implementation.
F47 requires actual supported-workflow evidence and critical-defect resolution,
not unsupported hardware-dependent feature counts. Absent equipment is an
UNVERIFIED dependency for technical rescoping, not PASS or silent feature removal.

### F13 serial-console acceptance (planned)

No serial implementation or pass is implied by this checklist. Apply it to the
[F13 contract](roadmap.md#f13--uartserial-console-planned) at its implementation
gate, preserving F06/F22 ordering and the separate F18 logging scope.

1. **Automated endpoint tests:** use controlled PTY pairs to verify exact RX/TX
   bytes, none/LF/CR/CRLF sending and local echo. Test empty input, long and
   unterminated lines, split encodings, invalid bytes, NUL and terminal escape
   sequences. Navigation, GDB commands and serial sends must remain isolated;
   input focus changes must not send a draft accidentally.
2. **Bounded operation:** force partial writes, interrupted/would-block I/O,
   sustained RX floods and a peer that stops reading TX. Assert finite queues,
   retained history and per-pump work; cancellation, redraw and GDB operations
   remain responsive. Verify visible truncation/drop reporting and distinguish
   host-accepted bytes from firmware acknowledgement. A disconnected peer must
   not cause a retry spin, stale replay or descriptor leak.
3. **Configuration/lifecycle:** cover invalid paths/settings, permissions, busy
   ports, connection cancellation and cleanup. Use controlled transport failures
   for errors a PTY cannot reproduce. Verify supported settings are applied and
   restored where possible, failures remain visible, and reconnect revalidates
   identity without selecting another device or replaying queued commands.
4. **Physical RX and TX:** record board, firmware/build, UART pins/voltage or USB
   CDC interface, adapter/driver, device identity and all connection settings.
   Use sequence-numbered output plus a known echo/command-response fixture to
   compare actual received and sent data. Exercise every advertised framing and
   flow-control mode with suitable wiring; unsupported modes stay explicit.
5. **Physical coexistence/recovery:** capture output before and during ordinary
   debugging, including target running, halted and resumed; halt may stop the
   producer but must not disable host reception. Connect serial before a deliberate
   target reset and verify boot output/history, identifying any USB re-enumeration
   gap instead of claiming complete capture. Observe DTR/RTS/reset/boot effects
   on connect, disconnect and application exit. Unplug/replug during RX and TX,
   test changed device identity and retry deliberately. Verify no implicit reset,
   flash or commands are requested, and document any driver-induced transitions.
6. **Evidence and UI review:** test ordinary/compact navigation and Source
   fullscreen while serial traffic continues, plus
   direct/tmux terminal cleanup under traffic. Retain bounded output samples,
   settings, loss/recovery observations and logs. PTYs cannot prove electrical
   framing, flow-control wiring or reset-line behavior; physical results apply
   only to named configurations, not every MCU/probe or serial adapter.

### Opt-in Vim-like input

`make test-editor test-keybinds test-navigation` checks bounded Normal commands,
factory-off defaults, alias conflicts/preservation, preference migration and
mode-aware navigation. The existing keybind TUI suite additionally exercises the
toggle, Undo, unsaved confirmation, Save/reload, Normal-on-entry, Insert literal
text, cursor edits, focus changes and compact resize. Its native and isolated
real-tmux invocations in `test-ci` cover Console submit, Memory drafts, Save Log
fields and session cleanup. Existing default-off text-entry assertions remain.

1. With fresh preferences, open Config → Keybinds: Vim-like input is Off. Verify
   arrows work and ordinary fields immediately accept literal `hjkl` and Backspace.
2. Turn it On; Undo returns Off without saving. Turn it On, Back and Cancel the
   unsaved warning, then Save. Reopen the app: the option remains On.
3. Focus a configuration field: expect cyan edges and VIM NORMAL. Press i and
   type `alpha beta`; expect green edges and VIM INSERT. Escape, 0, w, x, i, X
   produces `alpha Xeta`. Tab to another field: expect Normal there.
4. In a real session, repeat in Console and Memory. Mode changes alone must not
   send/read. Enter/Send deliberately submits a Console draft. Escape from Insert
   preserves it and enters Normal; another Escape leaves editing. Repeat in tmux
   and monochrome, resize, and verify footer/focus remain consistent.
5. Restore Defaults with confirmation; it remains a draft until Save. Confirm
   the option becomes Off and profiles never carry personal input settings.

### Exact colors through tmux

`make test-terminal-description test-tmux-colors` checks advertised capability
selection and actual RGB escape sequences through an isolated real tmux server.
The tmux check uses `tmux-256color` plus `*:Tc`, compares exact Macchiato workspace,
panel, raised-surface, text and accent RGB values with standalone Alacritty output,
and switches to Rose Pine Dawn to check both channels of a light palette.
It also exercises the 256-color path with COLORTERM removed only in the fixture's
app process, and NO_COLOR. This explicit removal matters because current tmux
sets COLORTERM itself. The new checks are included in `test` and `test-tmux`, so
`test-ci` covers them along with existing input, mouse, resize and cleanup tests.

Manual review: open Config → Themes → Preview both outside and inside your RGB
configured tmux session. Macchiato's panel should retain its blue-gray color in
both; also compare a light palette's background and text. Keep the same terminal
font/settings. Check NO_COLOR and existing mouse/function-key controls. These
checks do not establish RGB forwarding through an unconfigured outer terminal.

### Footer and button visual follow-up

1. Open Config → Keybinds in a short terminal. Wheel through the rows, page by
   clicking the scrollbar track and drag its thumb to both ends. Cell outlines
   must remain clean, including the selected cell, with no extra underline strokes.
2. At a wide size, status and help share the last terminal row with left/right
   alignment. At narrow sizes they occupy the last two rows without overlapping.
3. Inspect launcher Config/Quit, native session Review/Save profile/Profile/Delete,
   and Memory Refresh/Options. Labels must fit with centered padding and separate
   hit regions. Resize the session form to 48×18; profile actions must not overlap.
   Repeat the scrolling and button inspection in tmux and monochrome.

4. Open each session setup: native Linux, Embedded target and GDB server.
   Read the entire explanatory note through its final sentence. At ordinary
   heights, one blank row separates that note from Review/Back and another
   separates the primary row from profile actions. At 48×18, notes still wrap
   completely while field scrolling and all actions remain reachable.

5. Open Config → Profiles. At 78 columns or wider, New native, New OpenOCD,
   New remote and Reload file share the bottom row with visible gaps between
   button backgrounds; Back stays aligned right. Below 78 columns the four left
   actions wrap into two rows without overlapping Back. Activate Reload file
   and each New action; check that they remain separate hit targets.

6. In Native and Embedded setup, check one empty row between the first selector
   and its first text field. In Embedded setup, Browse boards (or adapter/target)
   should have one empty row above and below, centered between the last field and
   the explanatory note. At compact heights the gaps collapse; Tab, Enter and
   text editing must still target the visible fields correctly.

7. In Embedded setup, confirm one empty row between OpenOCD binary and Start
   OpenOCD, matching the initial selector's separation. Keep exactly one empty
   row between Start OpenOCD and Browse boards, as before. Repeat with the
   adapter/target setup; compact rendering should keep all actions reachable.

8. In a live Console pane at least nine rows high, confirm one blank row between
   Command/Send and Save Log. Enter/send commands, retain a draft across resize,
   drag the horizontal scrollbar and open Save Log; the gap must stay empty and
   controls/caret must follow their displayed positions. Small panes collapse
   the gap. Repeat through tmux.

## OpenOCD MMIO protocol gate (Task 1.3)

The opt-in text-only probe uses The Bugger's actual GDB/MI controller:

```sh
make BUILD_DIR=build/mmio-protocol build/mmio-protocol/test-monitor-hardware
build/mmio-protocol/test-monitor-hardware arm-none-eabi-gdb 127.0.0.1:3333
```

Start the reviewed OpenOCD setup separately before running it. Connecting may
halt firmware; disconnect disposition depends on server configuration. The probe
formats constants, checks target association, injects framing errors and an
expected Tcl error, and delays one response to exercise exact late-token
reconciliation. It sends no peripheral read, write, flash, reset or resume.
It is never executed by `test-ci`. Success proves transport behavior on that
setup, not supported hardware access widths.

The captured `tests/fixtures/mi/openocd-monitor.mi` and `openocd-widths.mi` are
actual observations, replayed by `test-monitor`. Replay validates parser behavior;
it does not repeat physical validation. The width capture came from three
separately authorized GPIOA_ODR reads at 0x40020014, count one, widths 8/16/32,
on STM32F446 with ST-LINK V2J48M35 and the recorded OpenOCD configuration.
RM0390 sections 7.4/7.4.6 were reviewed before those reads. Debug logs show
explicit adapter widths 1/2/4 bytes and count one, without frontend fallback.
This is neither an independent bus trace nor proof of exactly-once physical
access. Other firmware/driver paths need their own validation; do not infer
access widths from displayed grouping or a successful generic byte read.

Product-level validation must additionally exercise user-confirmed target,
server-configuration, firmware and width bindings; expiration and revocation;
zero reads during navigation/management; one explicit frontend read; malformed,
failed, timed-out and late responses without automatic retry; and independent
Memory views. These remain separate from the transport gate and parser replay.

The opt-in production-transport width probe is separate from the text-only gate:

```sh
make BUILD_DIR=build/mmio-physical build/mmio-physical/test-mmio-hardware
build/mmio-physical/test-mmio-hardware arm-none-eabi-gdb 127.0.0.1:3333 --stm32f446-gpioa-odr
```

Run only with explicit hardware authorization and the documented STM32F446,
ST-LINK V2J48M35, OpenOCD 0.12.0-01004-g9ea7f3d64-dirty setup using
`openocd -d3 -f board/st_nucleo_f4.cfg`. It sends exactly three frontend
GPIOA_ODR operations at 0x40020014, count one, widths 8/16/32, through the actual
production adapter. No write/flash/reset/resume or retry command is issued.
GDB/OpenOCD connection itself can halt firmware and perform core/debug/flash
metadata or PC accesses; three frontend operations does not mean three total
physical accesses. Preserve client frames and OpenOCD adapter logs. This probe
is never executed by `test-ci`, and does not establish interactive consent/UI
acceptance or support for another target, firmware or server configuration.

Interactive MMIO review on that setup:

1. Stop the remote target. Open Memory Options → Temporary ranges, add a range,
   and set inclusive begin/exclusive end, MMIO policy, supported width and risks.
   Each edit must revoke consent; managing the definition must not access MMIO.
2. Review Setup. Verify the exact target, device, firmware, server configuration,
   endpoint and widths. Confirmation must be labeled a user assertion and cause
   no read. A stale review after a configuration change must fail.
3. Authorize this view's range, then Browse declared MMIO units. Navigation,
   selection, scrolling, resize, focus and cached inspection must cause no reads.
   Alignment must not imply a verified hardware-register identity.
4. Select a known approved unit and choose Read unit. Verify one frontend
   operation at its declared width/count/address. Refresh selected sample is a
   separate explicit hardware operation; there is no whole-window refresh.
5. Submit a Console command. Before transmission, setup confirmation and MMIO
   range consent must both expire; definitions remain. Reconfirming setup alone
   must not restore consent, and granting consent cannot bypass missing setup.
   Rejected Console submissions must preserve both permissions.
6. Verify ordinary stepping preserves setup confirmation but expires range
   consent. Repeat with a second Memory view to prove consent isolation.
7. Exercise timeout, malformed/late responses and close during a request using
   the authored integration fixtures. Uncertainty must remain visible in Console,
   block further MMIO and cause no automatic retry. Reconnection or an exact late
   completion is required as documented; do not inject arbitrary faults on hardware.

## ELF mapping trust and bounded reconstruction

`make test` includes image-model revision/identity checks, authored Code-model
reconstruction checks and `test-image-tui`. The latter creates a local ELF32 ARM
big-endian metadata fixture with a path containing spaces, then connects to an
authored MI peer. It verifies the remote review, 18x48 pagination, user-confirmed
label and Console invalidation. It is not a physical ARM validation.

1. Connect a stopped remote session with its configured ELF. In Disassembly
   Actions, choose Review ELF/load address mapping. Inspect the path, additive
   bias and all allocated-section ranges before confirming. Expect
   USER-CONFIRMED, no debugger request, no movement of PC or selected frame and
   no Memory authorization. At 18x48, page to the final confirmation.
2. Reopen the review; the current trust must be visible. A new confirmed bias
   supersedes the previous declaration for that ELF. Rejected/expired reviews
   cannot restore trust. Replacing the local ELF or accepting a Console command
   makes old declarations unusable for fresh decoding; old listings remain.
3. Use ordinary and optimized/stripped native fixtures. Native mapping evidence
   must match the backing device/inode and executable load segments. A native
   mapping label must not imply that every live code byte has been compared.
4. Browse more than sixteen windows in both directions. Retained windows need
   no request. Under actual cache eviction, a retained fresh boundary may support
   backward reconstruction; absent/stale boundaries must not cause guessed
   decoding. The authored model exercises forty forward expansions and verifies
   that the early full listing was evicted before reconstructing its boundary.
5. Exhaust the 128-decoded-row reconstruction budget and inject a changed code
   epoch while a request is in flight. Expect all previous instructions,
   generation, epoch, next address and gap provenance to remain intact, with no
   automatic replacement request. Close/reuse a split: old rows, annotations,
   anchor history and old request identity must not transfer to the new view.

The shared 4 MiB allocation check includes cached listings, source annotations,
anchor payloads and entry headers. Fixed active views and bounded temporary
reconstruction buffers are separate from that retained-history pool. Actual
terminal appearance, hardware-image correspondence and maintainer acceptance remain
separate checks.
