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
and `-Werror`. `test` builds and runs three C executables: unit, CLI and PTY
integration suites, described in `tests/README.md`. `sanitize` builds separately
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

## M01 manual test now

1. Run `make clean && make && make test`. Expect strict compilation and passing
   C unit, CLI and PTY suites. `--help` and `--version` also work without a TTY.
2. In a terminal at least 110x28 (140x44 recommended), run
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
8. Resize below six-pane dimensions. Expect six pane navigation buttons and a
   single focused view. Keys 1-6 switch panes. Below 48x18 expect a resize notice;
   expanding restores tab/view state. Dragging/splitting is intentionally M02.
9. Quit using the visible button or Ctrl+Q. Repeat with Ctrl+C. Expect normal
   shell echo/cursor/mouse behavior afterward. Run `NO_COLOR=1 ./build/the-bugger`
   and verify that text labels, selection and markers remain understandable.
10. Repeat startup, clicks, wheel, tabs, function keys and resize in tmux with
    the user's opt-in `set -g mouse on`. Do not change existing key bindings.
    Record terminal type, tmux version and any intercepted keys for the ledger.

Current evidence covers FR-01's UI and portions of FR-13/17, plus foundations
for NFR-01/03/05/08/09/10/11/12. Live debugging, full layout behavior and hardware
requirements remain incomplete. Physical mouse/UTF-8/visual fidelity still need
owner acceptance; PTY simulation alone does not establish those experiences.

## Later verification gates

M01/M02 add PTY and real-terminal/tmux visual/input acceptance plus pure layout
tests, using the HTML as the visual/interaction reference. Verify the single
six-pane default, tab/view changes, divider resizing, dynamic horizontal/vertical
splits, a seventh visible view and state-preserving terminal resize. No layout
selector or alternate named layouts may appear; no preset-switching tests are
planned. M03 adds a structured MI corpus and explicitly fake-server integration
tests. M04/M05 add real GDB native C/C++ fixtures, optimized/stripped cases,
attach permissions and coherent inspection. M06 adds profiles, configuration
tests and a real external gdbserver test. M07 requires real NUCLEO-F446RE
evidence and the full FR/NFR matrix. Fixture instructions and expected states
will be stored alongside those fixtures when implemented.

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
