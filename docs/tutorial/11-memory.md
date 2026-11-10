# 11 — Memory inspection

[Guide index](README.md) · Previous: [Locals and Watches](10-locals_and_watches.md) · Next: [Stack](12-stack.md)

Memory shows bytes at a chosen address, alongside an ASCII rendering. It is useful for checking buffers, comparing a variable's storage with its displayed value, or examining a known firmware object. It starts empty: opening the view does not read an arbitrary default address or assume the stack pointer is a safe location.

**On this page**

- [Read a known object](#read-a-known-object)
- [Interpret the rows](#interpret-the-rows)
- [Browse and refresh](#browse-and-refresh)
- [Example: inspect a changing ASCII buffer](#example-inspect-a-changing-ascii-buffer)
- [Temporary ranges](#temporary-ranges)
- [Explicit-width MMIO sampling](#explicit-width-mmio-sampling)
- [Other Memory actions and limits](#other-memory-actions-and-limits)

## Read a known object

1. Stop the target and expand **Memory**, initially in the lower-left group.
2. Click its inline **Address** field.
3. Enter a known numeric address or supported address/pointer expression.
4. Press Enter or click **Go**. Wait for the read result.

Examples include `&memory_buffer`, a pointer variable, `$sp` when GDB provides that register name, a member/index expression, or `0x20001000` on a target where that location is deliberately appropriate. These are not interchangeable defaults. Calls, assignments and user casts are rejected; the field is not a general GDB command interpreter.

Go reads the byte count shown in the header. Check that this extent is appropriate for the object before submitting; choosing an address does not make every byte after it harmless to read. For a declared MMIO region, use the separate width-specific workflow below instead of ordinary byte reads.

## Interpret the rows

Rows contain an address, hexadecimal bytes/groups and ASCII. Printable bytes appear as characters; nonprintable data is represented in the ASCII column without executing terminal controls. `??` means unavailable bytes, not zero. Partial failure may leave a valid prefix and missing bytes; inspect the status rather than assuming the whole request succeeded.

The header reports the requested byte count, grouping and target byte order: **LE** is little-endian, **BE** big-endian. Byte order matters when interpreting several bytes as one number. The frontend uses target information, not the host's architecture. Unknown order permits individual bytes but blocks multi-byte grouping; the pane does not provide a general manual endian chooser.

**Options → Cycle grouping: 1/2/4/8 bytes** changes presentation, not physical bus-access width. **Byte count: 1 to 1024** changes the requested range without reading it; Go or Refresh requests bytes afterward. Left/Right or the horizontal scrollbar reveals long rows.

## Browse and refresh

Use the wheel, focused-body arrows/Page keys or scrollbars to browse loaded rows. Scrolling can read adjacent blocks only when the application has evidence that they are ordinary readable memory. At an unknown or device-memory boundary, it stops with a notice and keeps the previous data.

To inspect another exact location deliberately, use inline Go or **Options → Address / Read**. **Previous range (read)** and **Next range (read)** explicitly request adjacent ranges; unlike ordinary scrolling, their labels state that they read. No background scan or speculative peripheral prefetch occurs.

Each Memory pane keeps its own location, draft, grouping and scroll. Open Memory in a second pane to compare `memory_buffer` and `memory_other` without continually replacing one location. Browsing history is limited and shared between panes, so older blocks may no longer be available without another explicit read.

Execution or context changes mark retained data **STALE**. **Refresh** deliberately samples the current requested range at a fresh stop. Failure retains the prior snapshot as stale where available. Opening a tab or stopping again does not automatically make that sample current.

## Example: inspect a changing ASCII buffer

```sh
make build/memory-fixture
```

Launch `./build/memory-fixture`, then use Console `start`.

1. Enter `&memory_buffer` and Go. Expect ASCII beginning `THE BUGGER MEMORY: first buffer`; its first bytes are `54 48 45 20`.
2. Set a smaller byte count, such as 32, through Options, then Refresh explicitly.
3. Add a code breakpoint on `memory_checkpoint` through Breakpoints and Continue. The first checkpoint occurs before the buffer's first character changes.
4. Read at that stop, Continue to the next checkpoint, then Refresh again. The first character is now `Z`.
5. Inspect `&memory_other` in another Memory pane; it starts `INDEPENDENT MEMORY: second buffer`.

The fixture also exposes `memory_edge`, a pointer near the end of a mapped region. Deliberately reading across its boundary can demonstrate partial/unavailable data. Do not translate that native boundary exercise into arbitrary hardware addresses.

## Temporary ranges

Use a temporary range when you need to declare a known memory region and authorize inspection of it. Stop the target, then open **Options → Temporary ranges: Add / Inspect / Authorize / Revoke**. Each Memory pane has 16 session-only range slots. Creating a range does not read its contents.

1. Choose **Add** to open a new entry for inspection, or select an existing entry in the list.
2. Set **Begin** and **End (excluded)** using hexadecimal addresses. `[begin, end)` includes begin and excludes end.
3. Choose **Policy: ordinary readable memory** or, for device registers, **Policy: MMIO (explicit width-sized reads)**. For MMIO, also choose the appropriate **Width** before authorizing.
4. Record known **Side effects**. Empty means **UNKNOWN**, not “none.”
5. Choose **Authorize**, read every review page and choose **Authorize without reading** only for the intended extent and policy. **Cancel** or Escape abandons the review. Authorizing sends no read.
6. Use **Revoke** to withdraw consent. Editing the declaration revokes its existing consent.

Every intermediate edit must leave valid bounds. When shifting a range upward, enlarge its end before increasing its begin if necessary. Overflow and invalid intervals are rejected rather than silently clipped.

Authorization is temporary and view-specific. Resume/context changes expire it; range definitions can remain for inspection. A range does not establish device identity or make a writable region out of read-only storage. [RAM editing](14-editing_values.md#edit-memory-bytes) requires additional writable evidence and its own Review/Apply.

## Explicit-width MMIO sampling

Memory-mapped I/O (**MMIO**) connects addresses to device registers. Reading one can acknowledge an interrupt, consume input or otherwise change hardware. “Readable” in a device description does not mean “free of side effects.”

Before starting, identify the exact peripheral address, its documented read side effects and the correct access width. This feature supports only the specific STM32/OpenOCD setup shown in its review, including **ST-LINK V2J48M35 (API v2)**. Other STM32, ESP32, probe and server configurations are not covered.

To sample one unit:

1. Stop the target and open a [temporary range](#temporary-ranges). Set its bounds, MMIO policy, known side effects and **Width**: 8, 16 or 32 bits, as appropriate for that peripheral.
2. In the range's inspection dialog, choose **Setup: NOT CONFIRMED (review validated setup)**. Read the complete target/device, probe firmware, OpenOCD configuration/version and endpoint. Confirm only if **every detail** matches your actual setup; this records your assertion, not automatic detection. Cancel if it does not match.
3. Return to the range, choose **Authorize** and accept the complete range review. Setup confirmation and range authorization are separate; neither reads hardware. Set the width first, because editing the range revokes authorization.
4. Choose **Browse declared MMIO units (no read)**. Each row is one aligned address unit of the chosen width, not a verified named peripheral register.
5. Select exactly the unit you intend to sample and choose **Read unit**. Wait for a sample or an error before requesting anything else.
6. To sample that unit again deliberately, choose **Options → Refresh selected sample (hardware read)**.

Scrolling, numeric address navigation, previous/next units, cached inspection and ordinary stepping do not sample MMIO. There is no whole-window refresh or polling. **Return to byte memory (no read)** leaves the MMIO list without sampling it. Samples remain historical observations because peripherals can change even while the CPU is halted.

One Read request can cause more than one physical bus attempt inside the external server, probe or hardware, especially on failure. The Bugger does not automatically retry. If the result is uncertain, inspect Console and do not repeat the read: further MMIO is blocked until that exact request's outcome is resolved or you reconnect. A completed but untrustworthy response requires reconnection. Closing the session invalidates confirmation and consent immediately, but a request already sent may have reached hardware.

### When consent expires

Every accepted Console command expires MMIO setup confirmation **and** range authorizations before transmission, even an apparently harmless query. Scripts/aliases can change the backend. Confirm again and renew consent before another MMIO read. Ordinary stepping/navigation preserves setup confirmation, but range consent still expires on resume/context changes. Relevant target/configuration/connection changes also invalidate it. Retained range definitions grant no access by themselves.

## Other Memory actions and limits

**Hardware watchpoint at selected row (first group)** opens a reviewed watchpoint request for that address/width. It does not monitor the entire displayed range. **Edit selected row** is restricted to eligible ordinary writable RAM; see [Editing values](14-editing_values.md). Search, export and comparison tools for Memory are not implemented; Console log export is a different feature.

For raw stack storage with SP offsets and provenance, use [Stack](12-stack.md). For errors about unknown regions, unavailable bytes or stale data, see [Troubleshooting](19-troubleshooting.md#memory-and-mmio).
