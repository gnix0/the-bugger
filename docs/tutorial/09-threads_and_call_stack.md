# 09 — Threads and Call Stack

[Guide index](README.md) · Previous: [Breakpoints](08-breakpoints_and_watchpoints.md) · Next: [Locals and Watches](10-locals_and_watches.md)

A **thread** is an execution context reported by GDB. Its **call stack** lists the active function calls: frame 0 is the innermost call, frame 1 its caller, and higher numbers lead outward. Selecting a frame lets you inspect that call's source, locals and GDB-reported registers without moving program execution backward.

On embedded systems, GDB threads may represent CPU cores or server-provided contexts. They are not automatically operating-system tasks. The supplied ESP configurations use hardware-thread awareness; no dedicated FreeRTOS task inspector is implemented.

**On this page**

- [Open the two views](#open-the-two-views)
- [What changes when you select a caller](#what-changes-when-you-select-a-caller)
- [Inspect full frame details](#inspect-full-frame-details)
- [Refresh and page](#refresh-and-page)
- [Example: find where a recursive call came from](#example-find-where-a-recursive-call-came-from)

## Open the two views

Select **Call Stack** in the middle primary pane or add it through `...`. Add **Threads** in another pane so both can remain visible. Stop the target before requesting fresh context.

Call Stack rows show frame number, function and compact `filename:line` where available. `>` marks selection, while `*` identifies the stopped thread's innermost frame. Missing source, unknown arguments and unwind errors stay visible as unavailable information.

C++ frames can identify methods, constructors and namespace-qualified functions using readable names supplied by GDB. Parameter signatures are not always included, and long names can be shortened; use **Details** for the retained name and source location. Select the method's frame, then use **Locals/Watches** to inspect supported object members. The separate **Stack** view shows raw bytes near SP, not a class-member listing.

Click a frame/thread row to select it, or give the body content focus, browse with Up/Down/Page keys and press Enter. Hovering or scrolling is not selection. The chosen thread/frame is reflected in inspector headers.

## What changes when you select a caller

- Source follows the selected caller and uses `~` for its reported line, distinct from the original `>` stop.
- Disassembly follows the selected frame only when that view is following; manual browsing remains independent.
- Locals belongs to the selected frame. Previously read values must not be mistaken for values in the new scope.
- Registers presents GDB's values for that selected frame; caller registers may be unwound/reconstructed.
- Stack byte inspection retains its frame-0 SP anchor. Selecting a caller neither samples Stack bytes nor silently creates an SP annotation.

During a frame switch, previous panes retain their original labels until replacement context is ready. A longer request shows **Switching to frame #…**. Wait for the replacement before using pane-local operations. Selecting the already-current fresh frame leaves its displayed values intact.

Use Source **Stopped** to return to the original stopped thread/frame. Returning a Disassembly viewport to Stop has a narrower effect and does not change the selected context.

## Inspect full frame details

Select or highlight the row of interest, then choose **Details**. This opens a pane-local read-free presentation of available metadata: full function/source path, line, address, thread, arguments and provenance. Opening Details does not select a frame or ask GDB for more values.

Scroll wrapped text using arrows/Page keys, wheel or scrollbar. **Frames** or Escape returns to the list with its row and scroll preserved. Retained details become stale after context changes. In a narrow pane, Details remains reachable; widen the pane if other footer actions are clipped.

Argument names and values are different operations. **Read Args** explicitly reads supported simple arguments for the selected frame; inspect the results in Details. Values expire on refresh or context/execution changes. An argument omitted or optimized out by the compiler cannot be recovered merely by opening Details again.

## Refresh and page

**Refresh** reloads stopped context. **Frames >** and **< Frames** page 32 frames at a time, up to the bounded 4096-frame display. Thread listing is bounded to the first 128 entries. Resize a narrow pane to expose paging actions. These are display/request bounds, not claims about the program's total thread count or stack depth.

While running, retained context is stale. Pause/Halt and wait for a fresh stop before selecting fresh frames. Unwind errors can reflect stripped symbols, optimization, corrupted stack state or backend limitations; the interface does not substitute example frames.

## Example: find where a recursive call came from

```sh
make build/context-fixture
```

Launch `./build/context-fixture`, then Continue. The fixture deliberately raises SIGTRAP while main is inside recursive calls and two worker threads are waiting.

1. Open Threads and Call Stack. Expect the main execution context and two workers when GDB reports them.
2. Inspect the actual stopped frame; it may include library frames around the deliberate signal.
3. Select a `recursive` caller. Source should show its reported call site, and Locals should now refer to that invocation.
4. Choose Read Args for the selected frame, then Details. Compare the `depth` argument in different recursive invocations where available.
5. Browse another thread, then return to the stopped context. No selection has changed the program's PC.
6. Continue. The workers are released and joined; the fixture later traps with main alone. Refresh rather than assuming the old thread list is still current.

For paging, relaunch with `--deep` in **Arguments (quoted)**. The deeper recursion spans multiple 32-frame pages. Optimized or stripped variants are useful for understanding missing information, but expect less source/argument detail rather than identical output.

For raw stack storage rather than function calls, open the separate [Stack inspector](12-stack.md).
