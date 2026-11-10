# 12 — Stack byte inspection

[Guide index](README.md) · Previous: [Memory](11-memory.md) · Next: [Registers](13-registers.md)

**Call Stack** answers “which functions are active?” **Stack** shows raw storage around the stack pointer (SP). Use it to inspect addresses, offsets from SP and raw bytes or words. Each sample identifies the thread and stop it came from. The view does not automatically identify every word as a pointer, local or return address.

**On this page**

- [Open and take a sample](#open-and-take-a-sample)
- [Position the window without reading](#position-the-window-without-reading)
- [Understand sample provenance and history](#understand-sample-provenance-and-history)
- [Add address points](#add-address-points)
- [Declare bounds without claiming measured usage](#declare-bounds-without-claiming-measured-usage)
- [Permissions and cancellation](#permissions-and-cancellation)
- [Example: inspect bytes and annotate a caller](#example-inspect-bytes-and-annotate-a-caller)

## Open and take a sample

1. Stop the target and expand the lower-left Memory group.
2. Choose **Stack**, or add it through that pane's `...` menu.
3. Check the proposed window: initially 128 bytes centered on the selected thread's **frame-0 SP**. It is **UNREAD**; opening Stack has not read those bytes.
4. Choose **Read**, or **Options → Read / Refresh exact window (explicit bytes)**. Wait for **SAMPLED** or an explanation of why the read could not complete.

Frame 0 is the innermost execution frame. Selecting a caller in Call Stack does not replace this anchor or read bytes. If SP is unavailable, Stack reports it rather than guessing an address. You can still [navigate to a known literal address](#position-the-window-without-reading). The application does not automatically establish the stack's bounds.

**READING** means the request is still pending; **STALE** identifies an older context, and **HISTORY** a deliberately selected retained sample. If permission is missing, use the separate [Stack temporary read ranges](#permissions-and-cancellation); a Memory authorization does not apply here.

Rows contain address, signed offset from SP, raw grouped value and available point annotations. Missing bytes are `??`. Incomplete groups remain raw bytes with an explanation. Unknown target byte order falls back to byte grouping. A numeric word is not automatically a verified address.

## Position the window without reading

**Options** offers these read-free settings:

| Action/input | Result |
|---|---|
| Follow frame-0 SP | Restores following; starts at SP minus half the requested byte count. |
| Navigate literal address / signed SP offset | Chooses a specific window start. |
| Unsigned literal, e.g. `0x20001000` | Uses that absolute start address. |
| Signed offset, e.g. `-64`, `+0`, `+0x20` | Starts relative to a fresh frame-0 SP. |
| Set requested byte count | Sets 1–1024 bytes without sampling. |
| Cycle grouping | Chooses 1/2/4/8-byte presentation without sampling. |
| Previous / Next address window | Navigates without sampling. |

Literal overflow is rejected rather than clipped or aligned to another address. Browsing, row selection and panning suspend following; choose Follow SP to resume. Thread changes restore following with unread content. Neither operation automatically samples bytes.

Use focused-body arrows/Page keys, wheel and bars to browse. Left/Right pans, with `<`/`>` indicating clipped content. Enter on a selected body row or **Selected row: complete details / provenance** opens paginated complete fields without a read; Back/Escape returns. This is useful in narrow panes where the full row does not fit.

## Understand sample provenance and history

A sample belongs to the thread and stopped context in which it was read. Stepping, resuming or changing context makes it stale. In **Options**, choose **Inspect previous cached sample** or **Inspect next cached sample** to open retained history without rereading. History can include a different thread; its SP offsets and points remain associated with that original sample, not the currently selected thread.

Follow SP or numeric navigation leaves history. A historical sample cannot be refreshed as if it belonged to the currently selected thread. Navigate to the intended current window and explicitly Read instead. Browsing history is limited, so older observations may no longer be available; this is not a complete stack recording.

## Add address points

Use **Stack → Options** to add an address annotation:

- **Query selected-frame SP point** asks for that selected frame's SP. A caller's value may be reconstructed by GDB and differs from the frame-0 anchor.
- **Query local/argument names, then select one address** opens a names-only picker. Choosing an eligible name requests one address in the selected thread/frame.

A variable-address query requires GDB's **may-call-functions** setting to be off, so the query cannot call a function in your program through that setting. If the query is blocked because calls are enabled:

1. Open **Options → Review session-wide no-call change** and read all pages.
2. Choose **Disable calls session-wide and query** only if you accept the change for the entire GDB session, including Console and other inspectors. The query proceeds only after the setting is verified off.
3. Use **Back** to cancel before sending the change. A setting command already sent can still complete. Once disabled, calls are not automatically re-enabled afterward.

GDB may still read memory to locate a variable, follow a reference or reconstruct a caller. A reference can identify the object it refers to; unsupported, ambiguous, optimized-out or register-only locations show an explanation instead of an address. An annotation marks one address. It does not establish the object's size or prove that its storage belongs to the stack.

A view retains up to 32 points. Use **Remove point annotations** to free slots. Points expire on relevant context/image/configuration changes; they are not persistent object identities.

## Declare bounds without claiming measured usage

If you know the stack's bounds, choose **Options → Declare bounds / growth direction**. Enter **Low** (included) and **High** (excluded), choose **Growth**, then **Declare**. **Remove declaration** clears it. These settings apply to this view and current thread; they grant no read permission and do not verify that the interval belongs to the stack.

With a fresh SP between those endpoints, including either endpoint, details can show **SP displacement from declared start**: `high − SP` for downward growth or `SP − low` upward. This distance is calculated from your declaration; it is not measured peak usage, fullness or a safety margin. A zero at an endpoint does not establish a readable byte. If SP lies outside the declared bounds, the view reports that instead of a usage figure. A later thread reusing the same numeric ID does not inherit the declaration.

## Permissions and cancellation

To authorize a known ordinary range for Stack, open **Options → Temporary read ranges (separate from Memory)**. Follow the [range declaration and review steps](11-memory.md#temporary-ranges) for that inspector. Memory consent and Stack bounds declarations do not authorize Stack reads. A declared MMIO range blocks generic Stack reads; Stack has no MMIO sampling action.

Choose **Options → Cancel active Stack operation** to stop further requests for that operation without interrupting target execution. Canceled results are not installed as a new sample. **Cancel requested** means an outstanding request has not finished; it does not confirm that the backend stopped it. A timeout also does not prove cancellation, and no automatic retry follows.

## Example: inspect bytes and annotate a caller

At a stopped native code fixture:

1. Open Stack, inspect its frame-0 SP availability and choose Read.
2. Select a row and open Details. Note address, sample context and SP-relative offset.
3. Select a caller in Call Stack. The Stack anchor and existing bytes stay associated with their original sample.
4. Deliberately query the caller's SP point, then inspect the annotation's provenance.
5. Step and observe that old bytes become stale. Restore Follow SP and Read at the intended new stop.
6. Open a previous cached sample to compare historical observations, then leave history before requesting current bytes.

Stack settings, samples, declarations and points are session-only. Saved setups retain its tab membership/layout, not its storage, grants or detailed Stack settings.
