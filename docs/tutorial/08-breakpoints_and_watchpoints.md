# 08 — Breakpoints and watchpoints

[Guide index](README.md) · Previous: [Code browsing](07-source_and_disassembly.md) · Next: [Threads and Call Stack](09-threads_and_call_stack.md)

A **code breakpoint** stops execution when the program reaches a location. A **data watchpoint** stops on a supported kind of memory access. Use a breakpoint when you know where to inspect; use a watchpoint when you know which storage is being changed but do not yet know which code changes it.

**On this page**

- [Set a code breakpoint](#set-a-code-breakpoint)
- [Manage existing entries](#manage-existing-entries)
- [Example: stop on a particular argument](#example-stop-on-a-particular-argument)
- [Run to Selected](#run-to-selected)
- [Create a hardware data watchpoint](#create-a-hardware-data-watchpoint)
- [Save definitions carefully](#save-definitions-carefully)

## Set a code breakpoint

With a native executable **Loaded**, or with the target **Stopped** and no breakpoint operation pending, use any of these entry points:

- Click the Source gutter beside an executable line.
- Select a Source line or Disassembly instruction and press F9 or toolbar **BP**.
- Open the **Breakpoints** view, choose **Manage → Add code breakpoint**, enter a location and Apply.

Add Breakpoints through `...` if it is not already available in the expanded lower-right group. Location input supports a function, `file:line` or `*0xADDRESS`. Use actual symbols and line numbers from your build. GDB validates the location; a non-executable source line may resolve to another executable location.

For C++, **Add code breakpoint** accepts GDB function/method locations, including namespace qualification and overload signatures. In the supplied `break-fixture-cpp`, `m08_overload` selects both overloads, while `m08_overload(int)` selects the integer one. The list shows the requested location and resolved addresses/files/lines; it does not add each overload's demangled signature to its child row. Inspect those locations before Continue, or use a specific signature or source line to distinguish them.

The gutter uses **o** for enabled and **-** for disabled breakpoints. The Breakpoints list shows backend identity, location, enabled state and hardware/pending/multiple-location information. Expand a narrow pane or pan horizontally to see full rows.

Selecting a breakpoint row does not resume execution. After creating one, Continue and inspect the actual stop reason/location when it triggers.

## Manage existing entries

Select a row, then open **Manage**:

| Action | Use |
|---|---|
| Enable selected / Disable selected | Keep a definition while changing whether it can stop execution. |
| Remove selected | Delete that breakpoint rather than merely disable it. |
| Edit condition | Stop only when GDB evaluates the condition as true; empty input clears it. |
| Set ignore count | Ask GDB to ignore a configured number of encounters before stopping. |
| New breakpoint: Default / Require hardware | Select the policy for newly entered code locations. |
| Refresh | Reload breakpoint metadata and help resolve reported cleanup state. |
| Add hardware data watchpoint… | Open the explicit data-watchpoint workflow. |

Conditions are GDB expressions, distinct from the more restricted Memory/Watch expression fields. Evaluate their suitability for your program and target; they are not frontend read-permission declarations. Runtime hit counts and remaining ignore counts are observations, while [saved setups](16-profiles_and_saved_setups.md) retain explicitly configured intent.

### Hardware requirements

Hardware code breakpoints use target debug resources rather than relying on ordinary software breakpoint placement. They are useful for firmware in Flash, but supported locations and available slots depend on the target/server. **Require hardware** does not guarantee a free slot or successful insertion at resume. Errors remain visible; hardware requirements are not silently satisfied by retaining a software fallback.

An unresolved symbolic location can be **pending** until GDB can resolve it, and one definition can have multiple locations. Do not assume a pending entry is already installed or that one displayed definition uses only one hardware resource.

## Example: stop on a particular argument

Build a silent fixture containing repeated calls:

```sh
make build/break-fixture
```

Launch `./build/break-fixture` through Linux application, then send Console `start`.

1. Open Breakpoints and choose **Manage → Add code breakpoint**.
2. Enter `m08_tick` and Apply.
3. Select that entry and choose **Edit condition**. Enter `value == 3`.
4. Continue. Expect a stop inside `m08_tick` for argument 3.
5. Read `value` in Locals to confirm the stopped context.
6. Disable the breakpoint to retain it without more stops, or remove it when finished.

If it stops at a different location, read Console and Call Stack rather than assuming the condition caused the stop. A signal or another breakpoint can intervene. The fixture also has a long `m08_long` instruction range and `m08_zeros` data for deliberate browsing exercises; data bytes are not automatically executable code.

## Run to Selected

Select a Source line or instruction and use **Actions → Run to Selected**. This runs toward a temporary breakpoint without assigning PC directly. Existing user breakpoints remain. Any intervening stop can end the operation before the destination, after which its temporary state is cleaned up.

If cleanup fails or ownership is ambiguous, execution is blocked and the reason is shown. Use **Manage → Refresh** and inspect the result; do not delete arbitrary breakpoints to force continuation.

## Create a hardware data watchpoint

Use **Breakpoints → Manage → Add hardware data watchpoint…**, or begin from a selected variable through **Locals/Watches → Actions → Hardware data watchpoint…**. The variable entry point prefills a root expression; enter a qualified member/index path yourself when needed.

1. Stop in a context where the expression is valid.
2. Review/edit the expression.
3. Choose the mode: **Value change**, **Read**, or **Read/write access**.
4. For expression watches, width follows the type GDB reports. For numeric-address watches, explicitly choose 1/2/4/8 bytes, subject to target support.
5. Choose **Create**. This deliberately permits evaluation; GDB may read watched storage during creation and later stops.
6. Wait for hardware-type verification, then Continue.

The frontend requires hardware for these data watchpoints. Software fallback is removed. Width, alignment, access mode and hardware slots remain backend constraints, and insertion can still fail on resume. An uncertain verification/cleanup can block execution until resolved.

### Watch the first group of a Memory row

In Memory, explicitly read a known ordinary object, select a row, then choose **Options → Hardware watchpoint at selected row (first group)**. Review its numeric address and width. This watches the first displayed group, not the entire row or displayed Memory window. Grouping is a presentation choice; a hardware watchpoint's width is an explicit monitored extent.

Avoid choosing an arbitrary peripheral merely because an address is visible. Hardware monitoring does not prove GDB's associated reads have no side effects.

### Example: find a writer

```sh
make build/watch-fixture
```

Launch `./build/watch-fixture`, use Console `start`, then create a Value change hardware watchpoint on `watched_value`. Continue. On a supporting native target, expect the write from 17 to 20 to stop execution. Inspect the stop location, reported old/new value and Breakpoints hit information. Continue again to investigate the later change to 21. Disable, re-enable or remove it through Manage.

A watch on a local can expire when its scope ends; it is not a permanent identity for a stack address later reused by another call. Inspect GDB's reported state after context changes.

## Save definitions carefully

Supported frontend-created code breakpoint definitions can be retained in a saved setup. Temporary breakpoints and data watchpoints are excluded. Breakpoints created or changed through Console need explicit **Setup → Review GDB breakpoints** before supported definitions are included. A saved enabled intention is not proof that restoration installed the breakpoint successfully; see [restoration review](16-profiles_and_saved_setups.md#restore-a-saved-setup).
