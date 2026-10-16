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
and `-Werror`. `test` builds and runs eight C test executables: two MI suites,
demo, layout, renderer, CLI and two PTY integration suites, described in
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
5. Optionally run the existing `./build/the-bugger` demo: presentation and
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
   `./build/the-bugger`. Expect the three clickable startup choices and a DEMO
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
   shell echo/cursor/mouse behavior afterward. Run `NO_COLOR=1 ./build/the-bugger`
   and verify that text labels, selection and markers remain understandable.
10. Repeat startup, clicks, wheel, tabs, function keys and resize in tmux with
    the user's opt-in `set -g mouse on`. Do not change existing key bindings.
    Record terminal type, tmux version and any intercepted keys for the ledger.

M01 evidence covered FR-01's UI and portions of FR-13/17, plus foundations
for NFR-01/03/05/08/09/10/11/12. Live debugging, full layout behavior and hardware
requirements remain incomplete. Physical mouse/UTF-8/visual fidelity still need
owner acceptance; PTY simulation alone does not establish those experiences.

## M02 manual test now

1. Run `make test`, then `./build/the-bugger` in a 140x44 or larger terminal.
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

1. Run `make` and `./build/the-bugger` at 140x44 or larger. Inspect launcher
   surfaces, descriptions, aligned configuration fields and retained editing.
2. Enter the workspace. Compare toolbar density, panel backgrounds/gutters,
   active/inactive tabs, table headings and panel footers against the HTML.
3. Click tabs and view menus. Check keyboard focus remains distinguishable
   from the active tab and that all controls still work.
4. Step with F10 and toolbar actions. Inspect source token colors, PC row,
   register names/values and yellow `+ changed` markers. All data remains DEMO.
5. Resize to 112x28, then 80x24, then below 48x18 and back. Check clipping,
   focused-pane navigation and retained state. Repeat inside tmux with mouse on.
6. Run `NO_COLOR=1 ./build/the-bugger` and verify labels, underline, focus,
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

1. Run `make` and `./build/the-bugger` in a 140x44 terminal. Choose Linux
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
5. Repeat with `NO_COLOR=1 ./build/the-bugger` and inside tmux with the owner's
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
| Native Linux x86-64 | Launch/attach, stepping both modes, AT&T, code/data breaks, registers, RAM, variables/frames/threads, output/log/profile, cleanup | UNVERIFIED — backend not implemented |
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
