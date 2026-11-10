# 13 — CPU registers

[Guide index](README.md) · Previous: [Stack](12-stack.md) · Next: [Editing values](14-editing_values.md)

CPU registers hold the processor's immediate execution state. Common examples are the program counter (PC), stack pointer (SP), general-purpose registers and status flags. Names differ by architecture: native x86-64 commonly exposes `rip`/`rsp`, while Cortex-M commonly exposes `pc`/`sp` and named core registers. Use the names your target actually reports.

## Inspect stopped values

Open **Registers**, initially the upper-right pane, and wait for fresh stopped context. The application discovers register names and IDs from GDB; it does not substitute a fixed ARM list for another target.

Select a row with the mouse or focused-body Up/Down. Scroll or pan to see long values. **Refresh** rereads stopped values. The list supports up to 512 register IDs; if a list or value reaches a limit, read the displayed explanation before treating it as complete.

When execution resumes, retained values are stale. Hidden/idle panes do not poll continuously. A changed thread/frame resets the comparison baseline. Selecting a caller displays GDB's frame-specific, possibly unwound values; these are not a second physical CPU snapshot taken in the past.

## Interpret changes and formats

Changed values use `*` and yellow styling in the default semantic palette. Changing the format does not itself mean the register changed. Unavailable/truncated values remain explicit; they are not zero or edit-ready samples.

Click the format control, initially **Hex**, to cycle:

| Format | Useful for |
|---|---|
| Hex | Addresses, masks and raw integer patterns. |
| Decimal | Integer counts. |
| Octal / Binary | Alternative numeric representations. |
| Raw | Backend raw representation. |
| Natural | GDB's natural presentation for that register. |

The available meaningful representation depends on GDB and the register. Registers appear in one list; there is no architecture-specific group selector. **Peripherals** is not a functional specialized peripheral-register view.

## Example: compare PC with an instruction

Stop the native code fixture at `main`, select Disassembly and execute one **Into** instruction step. Compare the stopped instruction address with `rip` in Registers. On a Cortex-M target, compare with the reported `pc` instead.

Step again and observe which general-purpose/status values changed. Switch Hex to Binary to inspect a bit pattern, then back; this changes only the presentation. Select a caller in Call Stack and read the frame label before interpreting the new displayed register context.

## Editing is a separate operation

Select a fresh supported row and choose **Edit**, or press Enter on its selected body row. This opens a draft, not an immediate write. Editing is restricted to frame 0 and eligible complete scalar CPU registers; raw bit input, target permissions, review pages and explicit verification all matter.

Use the full [CPU-register editing procedure](14-editing_values.md#edit-a-cpu-register). A backend-editable register is not proof that every physical bit can be changed. PC/SP/status/control changes can redirect execution or break the session, so use a disposable fixture and suitable general-purpose register for initial experiments.
