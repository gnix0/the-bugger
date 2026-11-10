# 20 — Controls and glossary

[Guide index](README.md) · Previous: [Troubleshooting](19-troubleshooting.md)

This reference lists current factory controls. Saved keybindings can change configurable assignments; toolbar hints show saved execution keys. The physical navigation/exit fallbacks remain. Keys are context-dependent: a character used for navigation outside a field stays literal during ordinary typing or Vim Insert.

**On this page**

- [Execution and application actions](#execution-and-application-actions)
- [Controls, content and panes](#controls-content-and-panes)
- [Lists, fields and dialogs](#lists-fields-and-dialogs)
- [View actions at a glance](#view-actions-at-a-glance)
- [Indicators and visible states](#indicators-and-visible-states)
- [Glossary](#glossary)

## Execution and application actions

| Default key | Action | Context/alternative |
|---|---|---|
| F5 | Continue | Toolbar Continue; starts a loaded native program or resumes when valid. |
| F6 | Pause / Halt | Toolbar Pause/Halt; needs a relevant active target. |
| F10 | Step over | Toolbar Over; source/instruction mode follows the last selected code view. |
| F11 | Step into | Toolbar Into; fresh stopped context required. |
| Shift+F11 | Step out | Toolbar Out; returns from execution frame, not a loop. |
| F9 | Toggle code breakpoint | Selected Source line/Disassembly instruction, or toolbar BP. |
| None | Reset | Explicit toolbar confirmation on supported OpenOCD targets; configurable key. |
| q | Back / close session | Outside text fields; workspace asks before closing; launcher stays open. |
| Ctrl+Q | Quit application | Fixed fallback, confirms active session; an additional Quit binding can be assigned. |
| Ctrl+C | Interrupt application/cleanup | Not target Pause. |

Execution function-key bindings work while an ordinary inline field or Vim Normal Console field is focused. Vim Insert protects input; Escape first. Contextual lists keep execution shortcuts available, so an open menu does not pause a target.

## Controls, content and panes

| Key/gesture | Context | Action |
|---|---|---|
| Tab / Shift+Tab | Controls/forms | Next/previous accessible control. |
| Arrows | Control focus | Spatially choose a visible control; no edge wrapping. |
| Enter / Space | Button/list choice | Activate; Space remains text in fields. |
| Escape | Local interaction | Leave content/editing, dismiss menu/details, go Back or restore Source fullscreen at control focus. Never closes a live session. |
| Click content/scrollbar | Pane | Acquire CONTENT FOCUS. |
| Up/Down, Page Up/Down | Content | Move rows/viewport according to the view. |
| Left/Right | Scrollable content | Pan long rows. |
| Ctrl+arrows | CONTENT FOCUS | Transfer content focus to visible geometric neighbor. |
| `[` / `]` | Workspace outside fields | Previous/next pane; disabled during Source fullscreen. |
| 1–9 | Workspace outside fields | Select retained pane ID; configurable. |
| Wheel | View/list under pointer | Scroll without acquiring keyboard focus. |
| Click scrollbar track / drag thumb | Overflowing view/list | Page / position viewport. |
| Click tab | Pane | Activate its view. |
| Click lower dock title or `^` | Minimized lower group | Expand. |
| Click `_` | Expanded lower group | Minimize. |
| Drag gutter/grip | Tiled workspace | Resize column, nested split or lower partner. |

**... → Pane actions → Resize dividers** enables keyboard resizing: Tab/Shift+Tab chooses a divider, arrows move it, Enter/Escape finishes. Optional `h/j/k/l` aliases mirror arrows when enabled; Ctrl+H/J/K/L mirror content transfers. They are not universal field shortcuts.

## Lists, fields and dialogs

| Control | Supported behavior |
|---|---|
| Menu Up/Down, Home/End, Page keys | Navigate bounded choices; Enter chooses, Escape cancels. |
| Outside click on dropdown | Dismiss and consume the click. |
| Outside click on input/review modal | Does not dismiss or activate underlying controls. |
| Field Left/Right, Home/End | Move cursor. |
| Field Ctrl+A / Ctrl+E | Beginning/end in ordinary editing. |
| Field Backspace / Delete | Delete before/under cursor. |
| Field Ctrl+U | Clear the line. |
| Field Tab/Shift+Tab | Change focus, not literal indentation. |
| Console Enter / Send | Submit one GDB command. |
| Memory Enter / Go | Explicitly request the entered range. |
| Bracketed paste | Stay in one field; normalize tabs/newlines to spaces; discard overflow beyond 255 characters; never submit. |
| Keybind capture Escape / Delete | Cancel capture / clear binding to None. |

Vim Normal field commands are `i/a/I/A`, `h/l`, `0/$`, `w/b/e`, `x` and `j/k` for control movement. Escape leaves Insert, then local input. Enter still submits the field operation. Counts/operators/`dd`/`D` and general Emacs/Readline compatibility are not implemented; see [Vim-like input](17-customization.md#vim-like-input).

## View actions at a glance

| View | Important controls | Full reference |
|---|---|---|
| Source | Actions, Stopped, + File, file tabs/x, fullscreen Restore | [Code browsing](07-source_and_disassembly.md) |
| Disassembly | Actions, Mixed, `<`/`>` explicit paging, ELF mapping indicator | [Code browsing](07-source_and_disassembly.md) |
| Breakpoints | Manage; add/enable/disable/remove/condition/ignore/hardware policy/watchpoint/Refresh | [Breakpoints](08-breakpoints_and_watchpoints.md) |
| Call Stack | Select row, Details, Read Args, `< Frames` / `Frames >` | [Context](09-threads_and_call_stack.md) |
| Threads | Select row, Refresh | [Context](09-threads_and_call_stack.md) |
| Locals/Watches | Read, Actions, + Watch, disclosure/child paging/format/edit | [Variables](10-locals_and_watches.md) |
| Memory | Address/Go, Refresh or Read unit, Options | [Memory](11-memory.md) |
| Stack | Read, Options, Enter for full row details | [Stack](12-stack.md) |
| Registers | Refresh, format, Edit or Enter on selected row | [Registers](13-registers.md) |
| Console | Command/Send, Save Log | [Console](15-console_and_logs.md) |
| Setup | Save setup, unresolved/restoration information, GDB breakpoint review, eligible RAM evidence | [Persistence](16-profiles_and_saved_setups.md) |

Controls can be clipped in narrow panes or unavailable in the current state. Widen the pane or open its menu; read prerequisites before assuming a missing action is a new global shortcut.

## Indicators and visible states

| Indicator | Meaning and useful response |
|---|---|
| Loaded / PID not started / Awaiting stop | Native ELF loaded without a stopped program. Console start or set a stop and Continue. |
| Connected | Server connected; extended-remote may have no inferior. |
| Running / Stopped | Execution state; context can still be loading after a transition. |
| LIVE NATIVE / OPENOCD / REMOTE | Actual connection kind; demo/theme preview explicitly uses simulated data. |
| CONTENT FOCUS, `*` in pane header | Keyboard targets the view body. Escape/Tab returns to controls. |
| `>` in code | Actual stopped execution location. |
| `~` in code / inspected caller | Selected inspection frame differs from the actual stop. |
| `>` / `*` in Call Stack | Selected context / stopped thread's innermost frame. |
| `o` / `-` breakpoint | Enabled / disabled; pending/multiple state needs list review. |
| `*` on variable/register row | Changed comparable sample. |
| `?`, Read required | Explicit value sample needed. |
| `!`, unavailable | Missing/unsupported value, source pin or other data; inspect explanation. |
| STALE | Retained sample no longer current. Read/Refresh at intended stop. |
| READING / pending | Request not yet completed. |
| Stack UNREAD / SAMPLED / HISTORY | No sample for chosen window / sampled observation / retained history. |
| `??` bytes | Missing/unavailable bytes, not zero. |
| ELF:native / ELF:user / ELF:? | Native mapping metadata / user-confirmed remote placement / unverified mapping. None proves live-byte equality. |
| Start/End of available instructions | Failed ordinary edge-scroll expansion; reverse scrolling clears it. |
| AUTHORIZED / not authorized | Temporary range consent state, not proof that hardware is harmless. |
| USER-CONFIRMED | Your assertion about the reviewed setup/image/region; not automatic identity detection. |
| Save setup* | Persistable definitions differ; live values are not being saved. |
| Cancel requested | Outstanding work has not settled; not proof of backend cancellation. |
| Write acknowledged | Backend completion information; explicit verification still required. |

Colors reinforce these meanings, with symbols/text available in monochrome. Scrollbar accent is an interface color, independent of value/error meaning.

## Glossary

| Term | Practical meaning |
|---|---|
| Backend | GDB and, when applicable, the server responsible for target operations. |
| Breakpoint | Stop when execution reaches a code location. |
| Byte order / endian | Order used to interpret several stored bytes as a number. |
| Call frame | One active function invocation, inspected through Call Stack. |
| Debug information | Compiler-produced symbols/types/line mappings connecting machine state with source. |
| Debug probe | Hardware connecting a host/server to target debug signals such as SWD or JTAG. |
| ELF | Executable/object format; the matching debug ELF supplies symbols and mappings. Loading it is not flashing. |
| Frame 0 | Innermost call of the selected thread; required for live CPU-register editing. |
| GDB | The debugger controlled by The Bugger. Console commands go to it. |
| GDB server | Process exposing target debugging over the remote protocol. |
| Hardware watchpoint | Hardware-supported stop on a selected kind of data access; width/slots/modes are target-dependent. |
| Inferior / target | The program or machine being debugged. |
| Instruction / PC | Machine operation / program counter identifying execution position. |
| JTAG / SWD | Probe-to-target debugging interfaces; a UART connection alone is different. |
| Load bias | Additive difference between linked ELF addresses and declared runtime placement. |
| MMIO | Memory-mapped device registers; reading can have side effects. |
| OpenOCD | A debug server commonly connecting GDB to embedded probes/targets. |
| Pane / view / tab | A workspace region / its displayed tool / selector among tools. |
| Profile / setup | Connection definition / associated workspace and inspection definitions. |
| Provenance | Which thread, stop/context and sampling circumstances an observation belongs to. |
| RAM / Flash | Writable working storage / nonvolatile firmware storage; permission for one does not imply permission for the other. |
| Scope | Where a variable name/lifetime is valid, often tied to a function invocation. |
| SP | Stack pointer; an anchor, not proof of the stack's complete bounds or ownership. |
| Stale | Previously observed information that is no longer a fresh current sample. |
| Thread | Execution context reported by GDB; not automatically an RTOS task. |
| Unwinding | Recovering caller context from debugging information and machine state. |
| Verification | Explicit fresh observation after a write; acknowledgment alone is insufficient. |
| Watch | A retained expression definition; Read obtains its value. |

For a task rather than a term, return to the [feature-oriented index](README.md#find-the-task-you-want) or [Troubleshooting](19-troubleshooting.md).
