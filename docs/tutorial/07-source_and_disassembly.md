# 07 — Source and disassembly

[Guide index](README.md) · Previous: [Execution control](06-execution_control.md) · Next: [Breakpoints](08-breakpoints_and_watchpoints.md)

Source shows the program as written. Disassembly shows the instructions GDB decodes for the target. You can browse either without moving execution. Understanding the difference between a selected row, the stopped PC and an inspected caller prevents many confusing debugging sessions.

**On this page**

- [Orient yourself at a stop](#orient-yourself-at-a-stop)
- [Open source files without losing the current frame](#open-source-files-without-losing-the-current-frame)
- [Repair source lookup](#repair-source-lookup)
- [Read disassembly](#read-disassembly)
- [Ordinary scrolling, explicit paging and raw inspection](#ordinary-scrolling-explicit-paging-and-raw-inspection)
- [Review a remote ELF mapping](#review-a-remote-elf-mapping)
- [Example: compare source with machine execution](#example-compare-source-with-machine-execution)

## Orient yourself at a stop

`>` marks actual stopped execution. `~` identifies a selected caller's reported location. A row you select for a breakpoint or Run to Selected is a navigation selection, not a PC change. Code becomes historical/stale when its execution context is no longer current.

Source **Stopped** or **Actions → Return to stopped context** returns inspection to the original stopped thread and innermost frame. Disassembly's **Actions → Return to Stop (this view)** moves only that viewport to the stopped PC. **Follow selected frame (this view)** follows your current frame selection instead. Neither disassembly action silently selects a different thread/frame.

Focus Source for source-level Over/Into and Disassembly for instruction stepping. The chosen mode remains when you use an inspector or toolbar. See [execution control](06-execution_control.md).

## Open source files without losing the current frame

1. In Source, choose **+ File** or **Actions → Choose source file**.
2. Type part of a filename or full path in **Filter**. Full paths distinguish duplicate basenames.
3. Use **Previous**/**Next** for catalog pages and select the desired entry by click or Tab/Enter.
4. The file opens in that Source pane. **Back** or Escape leaves the chooser without opening a file.

The **Current** file tab follows the selected stopped frame. Additional file tabs stay pinned to their files as execution moves. Click a file tab, use its `<`/`>` cycling controls in narrow panes, or close the active file with its **x**. Each Source pane supports Current plus seven pinned files, with independent vertical and horizontal positions. Opening an already pinned file returns to its existing tab.

The chooser lists up to 256 files GDB knows from symbols; it is not a general filesystem editor. Missing debug information, an unresolved file path or an unavailable local file can prevent opening. Full paths help you distinguish files with the same name, such as two different `main.c` files.

For a native executable already Loaded, you can open GDB-known source before starting the program. Current has no stopped frame to follow yet; a pinned file is still browsable without an invented execution marker. This is useful for setting a breakpoint before the first run.

### Find text and go to a line

Use **Actions → Find in current file** for case-sensitive literal text in the active source, or **Go to line** for a line number. Find searches after the selected row and wraps through the file. For the code fixture, search for `CODE_FIND_MARKER` to locate the initialization used in the first exercise. These actions move browsing selection and leave the selected execution frame and PC unchanged.

Scroll with the wheel, focused-body arrows/Page keys or scrollbars. Left/Right pans long source text while the line-number gutter stays fixed. Each pane can load up to 1 MiB and 32,768 lines of source; larger files show a limited preview.

## Repair source lookup

An ELF can name a build-machine path that does not exist locally. For example, it might refer to `/build/work/project/src/main.c` while your checkout is under `/home/me/project/src/main.c`.

Open **Source → Actions → Source lookup settings**:

1. Choose **Add directory** when GDB needs another source search directory.
2. Choose **Add mapping** for an old build prefix and its local replacement. In the example, map `/build/work/project` to `/home/me/project`.
3. Review the displayed entries. Directories are newest-first; mapping rules use the first matching prefix in listed order.
4. Return to the source chooser and reopen/refresh the relevant GDB-known identity.
5. Use **Save source/profile...** to persist applied settings in a named connection profile. Otherwise they are session-only.

**Clear overrides** removes frontend source overrides; it does not repair a mismatched ELF. Paths containing spaces remain literal. Local destinations accept `~/`; mapping origins are build paths and are left unchanged.

Relative source directories, `.`, `$cwd` and `$cdir` are passed to GDB. `.` is resolved when added; `$cwd` follows GDB's later working-directory changes. Do not expect shell `$HOME` expansion. Absolute and expanded-home source directories are checked locally.

If restored pinned files show `!`, correct lookup and refresh the GDB catalog before **Setup → Unresolved setup entries → Retry**. Missing pins retain identity, not old text.

## Read disassembly

The listing aligns address, opcode bytes, mnemonic and operands. x86-64 uses AT&T syntax. ARM/Thumb, Xtensa and RISC-V presentation follows the target-compatible GDB. If GDB cannot establish an instruction boundary, the listing reports that limitation.

**Actions → Go to address / symbol** accepts a hexadecimal address or simple symbol. Use a real symbol such as `code_callee` in the native fixture. Browsing to an address does not run the target. Select an instruction before toggling BP or choosing Run to Selected.

For C++, Source shows the declarations as written, while symbol labels in Disassembly come from GDB. Instruction operands may show encoded linker names instead of readable C++ names. To request readable labels, send `set print asm-demangle on` in **Console → Command**, then request a new instruction window with Go to address / symbol. This changes display, not execution; the usual [Console confirmation/permission expiry rules](15-console_and_logs.md#expect-invalidation-after-commands) still apply.

The Go field accepts simple `namespace::name` or `Class::method` forms, but not parameter signatures, template arguments or operator spellings. An overloaded name may not resolve uniquely. Use a source location or known instruction address instead, or [set a breakpoint with a specific signature](08-breakpoints_and_watchpoints.md#set-a-code-breakpoint).

The **Mixed** toggle beside Actions adds source annotations where mappings and local source are available. It is not repeated in Actions. Annotations show only code, without repeated path/line prefixes, and strip leading indentation to align left. The Source view keeps original indentation. Blank lines and braces can be legitimate annotations. Missing source falls back to assembly or an explicit unavailable indication.

### Follow execution or browse independently

While following, the listing keeps roughly five rows of context above/below PC when enough space and decoded context exist; short panes use a smaller margin. Manual disassembly browsing stays where you put it across stepping and frame changes. Its header identifies the sampled file or **address window**, rather than borrowing an unrelated current frame's filename.

Use **Follow selected frame (this view)** or **Return to Stop (this view)** to regain the desired follow behavior. Switching the view chooser open/closed preserves the viewport. Horizontal panning should preserve address colors and selected execution highlighting.

## Ordinary scrolling, explicit paging and raw inspection

These are deliberately different operations:

| Operation | Use |
|---|---|
| Wheel, arrows/Page keys, scrollbar | Browse available code and extend within established code boundaries. |
| Footer `<` / `>` | Explicitly page instruction windows, including deliberate inspection outside ordinary-scroll bounds. |
| Go to address / symbol | Request a specific location. |
| Actions → Decode bytes (raw inspection) | Deliberately inspect raw bytes as instructions, including data/padding. |

Ordinary scrolling skips known data/section gaps and labels skipped bytes instead of decoding them as code. When you reach the available instructions' edge and try to scroll farther, **Start of available instructions** or **End of available instructions** appears. Scroll in the opposite direction to clear the notice and return through the listing. Loading a new listing or resizing the terminal also clears it.

**ELF:?** in the footer means the image mapping is unverified. That status alone does not cause a **Code range unknown** startup banner, and explicit paging does not itself display that banner. If an edge notice remains after reverse scrolling, appears before an edge-scroll attempt, or ordinary scrolling takes you into bytes outside established code boundaries, save the Console log and [report the navigation sequence](19-troubleshooting.md#report-a-reproducible-problem). Include any explicit paging, raw inspection, frame changes or resizing before the problem.

Repeated instructions can be a correct decoding of bytes that are data. For example, x86 zero bytes can decode as repeated `add` instructions. Raw inspection deliberately allows these results. Toggle **Decode bytes (raw inspection)** off to return to ordinary scrolling restrictions.

If the preceding instruction boundary cannot be established within the browsing limits, the previous listing stays in place and a message explains why. Use **Go to address / symbol** for a known location rather than guessing a preceding byte address. Each window holds up to 128 instructions. Disassembly and Memory share 4 MiB of browsing history, so older listings may no longer be available without another explicit request. Browsing history never changes PC or grants access to a new range.

Cached historical listings can be browsed while running. An uncached request while running leaves the viewport unchanged; it is not queued to surprise you at the next stop.

## Review a remote ELF mapping

Remote symbols alone do not verify where an image is loaded. In Disassembly, choose **Actions → Review ELF/load address mapping** for the configured ELF:

1. Enter an unsigned hexadecimal additive load bias; use `0x0` for linked virtual addresses.
2. Read the complete local path and all allocated-section runtime addresses across review pages.
3. Confirm only if they describe the image and placement on your current target. Back cancels.

The labels mean:

| Footer / review | Meaning |
|---|---|
| ELF:native / BACKEND-VERIFIED | Native file identity and mapped load segments match backend mapping metadata. |
| ELF:user / USER-CONFIRMED | You declared that the remote ELF and additive placement match this target. |
| ELF:? / UNVERIFIED | A relevant mapping is unavailable or no longer trusted. |

Neither form proves live-byte equality. User confirmation is not chip detection, firmware verification or Memory permission. Nonuniform relocation and general multi-image configuration are not inferred.

Changed ELF identity, target/image lifecycle changes and accepted Console commands invalidate trust. Existing rows remain historical; confirmation preserves them, and an explicit code action obtains fresh decoding where needed. A different confirmed bias supersedes the previous declaration for that ELF.

## Example: compare source with machine execution

Stop the code fixture at `main`. Enable Mixed, choose Disassembly and step Into repeatedly. Observe that several instructions can belong to the same source line. Pan horizontally to inspect a long operand, then return left. Choose Source to resume line stepping. Finally browse another function and use **Return to Stop (this view)** to orient the assembly pane without changing other inspectors.

For more room, use [Source fullscreen](02-interface_and_navigation.md#use-source-fullscreen). For a stop at a browsed line rather than just a visit to it, use [breakpoints or Run to Selected](08-breakpoints_and_watchpoints.md).
