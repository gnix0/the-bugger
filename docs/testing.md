# Testing and manual acceptance

## Automated checks

Run from the repository root on Linux:

```sh
make clean
make all
make test
make sanitize
make CC=clang BUILD_DIR=build/clang test
make analyze
git diff --check
```

`all` compiles C17 with strict warnings including `-Wall -Wextra -Wpedantic`
and `-Werror`. `test` builds and runs thirteen C test executables: two MI suites,
two controller suites, context/code models, native word parsing, demo, layout,
renderer, CLI and two workspace PTY suites, described in
`tests/README.md`. `sanitize` builds separately
with ASan/UBSan; a runtime
diagnostic is a failure, not a skip. `analyze` uses clang-tidy and fails on
warnings; an unavailable tool is a reported limitation, never a pass. Checks are
explicitly enabled (`clang-analyzer-*` and `bugprone-*`) rather than relying on
machine defaults. The Annex K recommendation check is excluded: it flags even
bounded `snprintf`/`memmove`/`memset` and suggests optional `_s` functions that
Linux libc does not provide. Other analyzer/security checks remain enabled.

LeakSanitizer cannot run under ptrace. In an agent sandbox that uses tracing,
the sanitizer command may require execution outside that sandbox with approval.
Do not disable leak detection or label that environment failure a passing test.

`build/` is disposable and ignored. M01 command output and tmux captures are
retained as `build/m01-*`; these are local evidence, not tracked artifacts.
Durable command summaries and limitations belong in `docs/progress.md`.
Changing compiler or flags requires a fresh build directory or cleaning.

## M07 manual acceptance — live Source and Disassembly

M06 is accepted as 1b21504. This gate adds live code, stepping and navigation;
breakpoint UI/F9/Run to Selected remain M08. Registers/Memory/Locals remain
unavailable. GDB Console `start` supplies a temporary main breakpoint for this
gate's demonstration; it is an explicit command, not an automatic frontend stop.

1. Run `make fixtures` and `./build/the-bugger`. Choose Linux application / Launch,
   executable `./build/code-fixture`, no arguments. Review and Start native:
   expect Loaded. In Source click **Actions → Choose source file**. Filter with
   `code.c` and select its full path. Source is browsable before execution, with
   no invented execution marker. Disassembly explains that no stopped context
   exists. Escape returns from menus; fields accept ordinary text including q.
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
   are session-only until M13. Directory order is newest first; mappings show
   GDB's first-match order (replacing a prefix moves that rule to the end).
8. Repeat `start` with `code-fixture-optimized`; mappings may skip/repeat lines.
   For `code-fixture-stripped` use Console `starti`: assembly/instruction steps
   remain usable, with explicit missing-source/symbol states. Resize, split an
   extra code view, and repeat the core clicks/stepping/scrolling inside tmux
   (`set -g mouse on`). Check the approved theme/tabs/layout remain intact.
   Quit and verify terminal restoration. Stop for owner acceptance.
9. Keep a narrow Source/Disassembly/Console pane. Drag its horizontal thumb to
   reveal clipped text, click the track to page, and use Left/Right with its body
   focused. Source line numbers remain fixed; PC and pane size never change.
   Switch tabs/panes: horizontal positions remain independent. Vertical tracks
   and thumbs use one-cell targets matching the horizontal style. Enlarge a pane until its rows/columns
   fit: the corresponding bar disappears. Shrink it again and verify clamped,
   usable bars without covering content or footer actions. Repeat in tmux.
10. Relaunch without arguments and inspect the owner-refined startup card:
    centered on both axes, pink title, right-aligned v0.1.0, subtle separators,
    three described choices and selected-row indicator. Click either a title or
    its description; both open that flow. Check arrows/Tab/Enter, Quit/Ctrl+Q,
    140x44, a larger terminal and 48x18 compact presentation. Server flows remain
    explicit previews and recent profiles remain unavailable. The workspace
    after launch must retain its existing layout/theme.
11. Step instructions in Disassembly. Earlier instructions must remain browsable,
    with roughly four before the first PC where available. The viewport stays
    still through the middle, then scrolls to keep about five rows above and
    below PC where context permits (reduced in short panes). Browse until PC is
    near the top, then step: the top margin should return. The default upper row
    is modestly taller (62%); drag its divider and confirm your sizing survives
    terminal shrink/restore.
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
    horizontal orientation. Broader browsing boundaries and repeated zero-byte
    decoding are recorded for M08's research, not resolved by this correction.

Automated coverage: `make test` exercises synthetic code windows, mixed fallback,
caps, escaping, obsolete replies and invisible/running/idle reads. `make test-native`
adds actual -O0/-O2/stripped/C++ GDB code/stepping, caller inspection, safe symbol
lookup, relocated duplicate paths with spaces/quotes/metacharacters, mapping
replacement/clear, and the real TUI browse/find/line/mixed/settings workflow.
Full visual fidelity and physical terminal/tmux input still require owner review.
No remote, board or release acceptance is inferred from native tests.

## M06 manual acceptance — Call Stack and Threads

M05 is accepted as 0278b61; M06 is accepted as 1b21504. This historical checklist
covers frame/thread inspection; M07 above adds Source/Disassembly and stepping.
No real hardware, remote connection or release acceptance is claimed here.

1. Run `make fixtures`, then `./build/the-bugger` without --demo. Choose Linux
   application / Launch. Set executable to `./build/context-fixture`; clear
   arguments and environment for this run. Review and Start native. Expect
   Loaded. This fixture deliberately raises SIGTRAP under GDB to provide stops
   without depending on the future breakpoint UI; do not run it standalone.
2. Activate **Call Stack**, the existing tab beside Disassembly. Click Continue
   or press F5. The fixture creates two workers and stops in its main-thread
   recursive call. Expect Stopped, three real threads in the context, and live
   frames including `context_checkpoint`, several `recursive` calls and `main`.
   libc frames may lack symbols/source; their names/addresses follow local GDB.
   Arguments such as `depth=0, worker=0` come from GDB, not sample UI values.
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
   A seventh split view is optional; the default six-pane geometry is preserved.
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
   shows the accepted simulated reference separately. Stop for owner acceptance;
   Every next gate requires owner acceptance and commit/push before continuation.

Automated scope: a synthetic C peer supplies bounded pages/arguments, nonordinal
thread IDs, missing/malformed unwind data, 128-thread truncation, delayed query
and selection replies, rapid changes, thread exit, reconnect, and idle/running
read checks. Actual GDB runs recursive/multithread -O0, -O2 and stripped C targets,
checks stopped-PC preservation, selected caller/thread consistency, arguments,
worker exit, pause/resume and cleanup. A real TUI PTY test exercises row clicks,
keyboard selection, caller labels, two simultaneous context views and stale
state. The peer is not hardware evidence; the VT reader is not visual acceptance.

## M05 manual acceptance — real native Linux

Run `make fixtures` and `make test-native` first. These separate real-GDB tests
require native GDB, a C++ compiler for the target fixture, Linux PTYs/pidfds and
ptrace permission. Default `make test` remains deterministic and board/GDB-free.
`make sanitize-native` instruments the frontend and harnesses, keeping target
fixtures uninstrumented so LeakSanitizer does not run inside the ptraced target.
In restricted agent environments, real-GDB tests may need execution outside the
ptrace sandbox. No global permission/sysctl setting is changed.

1. Run `./build/the-bugger` without --demo. Choose Linux application / Launch.
   Use `./build/debug-fixture` as the executable, `/tmp` as working directory,
   `"two words" "" '$HOME;$(false)'` as arguments and
   `THE_BUGGER_TEST="hello world"` as environment. Review session, then Start
   native. Expect Loaded, no program PID yet, and five inspection panes with
   explicit unavailable messages. Console is live. No Cortex-M sample data,
   simulated registers, instructions or execution PC may appear.
2. Click Continue or press F5. Expect Running and an actual PID. Console should
   show the program's directory, literal arguments/environment and stderr marker.
   To explicitly test control-byte capture, add `--control-bytes` to Launch
   arguments: escape/OSC bytes must appear as text, not terminal commands. Do
   not use that test-only flag for a standalone fixture. Attached stdout is
   deliberately different: it stays with that process's existing terminal.
3. Click Pause. Expect an actual GDB stop and Stopped. In Console choose Command,
   enter `info program`, Send. Try `this-command-does-not-exist` and verify a
   readable error with a usable session. Then `echo console-ok\n` should work.
   Input uses ordinary arrows/Home/End/Delete/Backspace/Ctrl+U and Tab/Enter.
4. Choose Save Log and enter a new writable path. Expect success, retained
   readable log text and mode 0600. Repeat that existing path and try an invalid
   directory: expect errors with input retained and no overwrite. Pick a new
   path to retry. Logs contain bounded retained history, not all past output.
5. Scroll Console using wheel, scrollbar and keyboard; return to its end to
   follow new output. Resize/split panes and switch views. The established tiled
   workspace remains usable, and unavailable views never display demo values.
   Over/Into/Out/Breakpoint/Reset remain unavailable in live sessions in M05.
6. Click Session or Quit with the native session active. Verify the launch
   termination confirmation; Cancel preserves the session. Confirm closes GDB
   and the frontend-launched program, restoring the terminal. Repeat Ctrl+C,
   SIGTERM or SIGHUP cleanup. A running program must not remain orphaned.
   Also verify `q` closes the current screen outside text fields: from the
   workspace it asks to close the session and returns to the launcher after
   confirmation. In a confirmation, `q` cancels. At the launcher it stays open.
   `Ctrl+Q` quits the application, asking before closing an active session.
   Commands and filenames containing `q` remain editable; Escape cancels input.
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
   workflow in the owner's terminal/tmux with opt-in mouse support.
9. Run `./build/the-bugger --demo` for the unchanged visual reference. It must
   remain explicitly simulated and separate from native state. Embedded/remote
   flows are still previews. Stop here; M06 requires separate acceptance and
   owner commit/push.

Automated scope: actual C/C++ launch, argv/cwd/env fidelity, output/control-byte
isolation, pause/continue, console errors, Save Log open/short-write failures,
normal exit, permitted/denied attach, external survival/resumption, bounded
output floods, GDB crash cleanup, live TUI actions/confirmations, input retention
and signal terminal restoration. Native main/step/inspection and optimized/
stripped inspection evidence remain in M06–M14, not claimed by this gate.

## M04 manual acceptance

This gate exercises transport/controller foundations, not live native debugging.
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
   harness readable; this is not the future application log-retention policy.
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
not proof of whole-TUI latency or release performance. Final native performance
and terminal/tmux acceptance remain M14/M18. No new mouse/visual behavior is
introduced in M04; the existing workspace PTY regressions still run.

## M03 manual acceptance

This gate tests protocol parsing only. No GDB connection, process controller or
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
   required for M03 parsing. Stop here; M04 needs separate authorization.

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

## M01 manual test now

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
   `docs/design/prototype.html`. There is no layout selector or named layout.
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
8. For the initial workspace, resize below six-pane dimensions. Expect six pane navigation buttons and a
   single focused view. Keys 1-6 switch panes. Below 48x18 expect a resize notice;
   expanding restores tab/view state. Dragging/splitting was added in M02.
9. Quit using the visible button or Ctrl+Q. Repeat with Ctrl+C. Expect normal
   shell echo/cursor/mouse behavior afterward. Run `NO_COLOR=1 ./build/the-bugger --demo`
   and verify that text labels, selection and markers remain understandable.
10. Repeat startup, clicks, wheel, tabs, function keys and resize in tmux with
    the user's opt-in `set -g mouse on`. Do not change existing key bindings.
    Record terminal type, tmux version and any intercepted keys for the ledger.

M01 evidence covered FR-01's UI and portions of FR-13/17, plus foundations
for NFR-01/03/05/08/09/10/11/12. Live debugging, full layout behavior and hardware
requirements remain incomplete. Physical mouse/UTF-8/visual fidelity still need
owner acceptance; PTY simulation alone does not establish those experiences.

## M02 manual test now

1. Run `make test`, then `./build/the-bugger --demo` in a 140x44 or larger terminal.
   Choose Linux application, Review preview, Preview workspace. Compare the
   initial six-pane presentation with the accepted M01 interface and HTML.
2. Drag each vertical column gutter and the horizontal row gutter with the
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
8. Repeat inside tmux with the owner's opt-in `set -g mouse on`, and repeat in
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
Agent sandboxes may require execution permission for its local socket.
Output goes to `build/m02-tmux-workspace.txt`; final M02 commands/results are
recorded in `docs/progress.md`. Physical visual/input acceptance remains manual.

## Later verification gates

M01/M02 add PTY and real-terminal/tmux visual/input acceptance plus pure layout
tests, using the HTML as the visual/interaction reference. Verify the single
six-pane default, tab/view changes, divider resizing, dynamic horizontal/vertical
splits, a seventh visible view and state-preserving terminal resize. No layout
selector or alternate named layouts may appear; no preset-switching tests are
planned. M03 adds a structured MI corpus; M04 adds explicitly fake-server
transport/controller tests. M05 introduces real native GDB sessions/output;
M06–M12 cover context, code/stepping/source recovery, breakpoints, inspection and
hardware-only watchpoints. M13 adds basic profiles/key mapping; M14 completes
native end-to-end acceptance. M15 tests a real external gdbserver; M16 covers
OpenOCD configuration/lifecycle/ownership; M17 requires real release-board
evidence; M18 audits release-wide FR/NFR results. See [the roadmap](plan.md) for
each independent gate's tests and exclusions. Fixture instructions and expected
states are delivered with their implementation, not represented as tests run now.

No mock transcript is hardware evidence. No automated check marks a milestone
accepted; the owner must perform manual validation and explicitly approve.

## CI proposal

Once CI setup is authorized, use a Linux job with GCC, Clang, Make, pkg-config,
ncursesw development files/terminfo and clang-tidy, running the commands above.
No Python is required. Upload command logs on failure.
Extend dependencies and suites as milestones arrive. Hardware acceptance stays
a separate recorded owner gate; CI must not label it passed based on fixtures.
No hosted workflow or external service has been configured in M00.

### M01 corrected visual review

The original presentation was rejected. Compare the corrected renderer directly
with `docs/design/prototype.html`, using the component checklist in
[the terminal mapping](design/tui-mapping.md). Six panes or passing automated
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

## Post-M02 navigation correction

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

## P00 tab correction — manual test now

1. Run `make` and `./build/the-bugger --demo` in a 140x44 terminal. Choose Linux
   application → Review preview → Preview workspace. All values remain DEMO.
2. In the upper-middle pane, click Call Stack while Disassembly is active.
   Call Stack moves left and has normal active styling; Disassembly immediately
   has normal inactive styling, without a gray inverse highlight.
3. Alternate those two tabs repeatedly without clicking outside the pane.
   Each old tab immediately becomes inactive. Click Call Stack, press Enter:
   it must stay active rather than activating Disassembly at the old control index.
4. From that active tab, press Tab to focus the next tab. Keyboard focus is
   visible. Enter activates it; Left/Right still switch tabs and keep focus on
   the promoted tab. Existing view content/scroll, splits and resizing still work.
5. Repeat with `NO_COLOR=1 ./build/the-bugger --demo` and inside tmux with the owner's
   opt-in mouse support. Monochrome active tabs retain their selection marker;
   inactive tabs have no stale marker after mouse activation. Quit cleanly.
6. Review the versioned roadmap and per-release matrix. Confirm the supplied
   reference JPG is gone, and that README still distinguishes demo from plans.

C regression checks inspect actual PTY inverse attributes, not just label text,
in direct/256-color and monochrome. It fails against the pre-fix binary at the
old inactive-tab highlight. Additional checks verify leftward promotion, Enter
remaining on the clicked tab and visible keyboard focus. The VT reader skips
extended-color parameters when interpreting inverse attributes. Physical
appearance remains subject to owner acceptance.

## Every-release regression matrix

Before every release (including patches), record actual commands, results and
owner acceptance for the minimum matrix below. Exact ESP/probe setup is not a
P00 prerequisite; fill it in when conducting the tests. Tool/server paths stay
editable; OpenOCD is not a requirement for every architecture.

| Environment | Minimum evidence | Current live evidence |
|---|---|---|
| Native Linux x86-64 | Launch/attach, stepping both modes, AT&T, code/data breaks, registers, RAM, variables/frames/threads, output/log/profile, cleanup | PARTIAL — M05/M06 accepted; M07 code/stepping automated evidence and owner review recorded in progress.md; breakpoints/other inspection/profiles and release acceptance pending |
| STM32 / ARM | NUCLEO-F446RE fixture via GDB/OpenOCD; halt/step/break/watch, real code/register/RAM/variables, reset/disconnect/cleanup | UNVERIFIED |
| ESP32 / Xtensa | Owner's board via appropriate GDB/server; applicable core workflow and cleanup | UNVERIFIED |
| ESP32 / RISC-V | Owner's board via appropriate GDB/server; applicable core workflow and cleanup | UNVERIFIED |
| QEMU ARM / RISC-V | Supplementary reproducible native/guest runs where available | UNVERIFIED; not a physical-board substitute |

For each release, capture the actual setup/version, relevant fixture/source
identity, core-workflow results, capability-specific exclusions, new-feature
regression and cleanup. Unavailable hardware remains UNVERIFIED and is brought
to the owner before release acceptance. Tests are not waived because a release
only changes UI. QEMU test infrastructure does not add simulator support to the
product. Do not claim unsupported target-specific features or untested boards.
