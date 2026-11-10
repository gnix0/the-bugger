# 02 — Interface and navigation

[Guide index](README.md) · Previous: [Getting started](01-getting_started.md) · Next: [Native sessions](03-native_sessions.md)

The most useful distinction in the interface is between **moving among controls**, **working inside a view**, and **editing a field**. The same arrow key has a sensible but different job in each situation. Watch the focused control, the pane header and the help bar to tell which context you are in.

**On this page**

- [Read the workspace](#read-the-workspace)
- [Move among buttons and tabs](#move-among-buttons-and-tabs)
- [Work inside a view](#work-inside-a-view)
- [Expand the lower inspectors](#expand-the-lower-inspectors)
- [Change tabs and split panes](#change-tabs-and-split-panes)
- [Resize with mouse or keyboard](#resize-with-mouse-or-keyboard)
- [Use Source fullscreen](#use-source-fullscreen)
- [Menus and dialogs](#menus-and-dialogs)
- [Enter, edit and paste text](#enter-edit-and-paste-text)
- [Recognize feedback](#recognize-feedback)

## Read the workspace

The top toolbar controls execution and includes **Setup**, **Close Session** and **Quit**. The session header identifies the connection, state and stepping mode. At the bottom, the status area reports results and the help area describes the current interaction.

The initial six pane groups are:

| Column | Primary pane | Lower group, initially minimized |
|---|---|---|
| Left | Source / Symbols | Memory / Stack |
| Middle | Disassembly / Call Stack | Locals / Watches |
| Right | Registers / Peripherals | Console / Breakpoints |

Threads can be added through a pane's `...` selector. **Symbols** and **Peripherals** are labeled unavailable in live sessions; their tab names do not imply working inspectors. A pane may hold several tabs, and the same supported view can appear in more than one pane. Memory locations and many browsing positions are pane-specific.

The active view tab is promoted to the left. Clicking another tab activates it and removes selection styling from the previous one. Source has its own file tabs inside the view; those are separate from pane view tabs.

## Move among buttons and tabs

At **control focus**, arrows choose the nearest control in the corresponding visual direction. Right moves from Continue toward Pause; Down seeks a control below. Directional movement stops at an edge rather than wrapping.

Tab and Shift+Tab cycle accessible controls in a stable order. They skip view bodies, scrollbars, dividers and hidden panes. Press Enter to activate a focused button. Click a visible control to activate it directly. Space also activates ordinary buttons and list choices; inside a text field it is text.

With optional [Vim-like input](17-customization.md#vim-like-input), `h/j/k/l` act as Left/Down/Up/Right outside ordinary typing. Custom assignments may differ.

## Work inside a view

Click a pane's content body or scrollbar to give it **CONTENT FOCUS**. Its header shows `*`, and the help bar names this state. Arrows now navigate the listing instead of selecting toolbar controls:

- Up/Down moves through rows or scrolls text, depending on the view.
- Page Up/Page Down moves farther through the current listing.
- Left/Right pans long rows horizontally in views supporting it.
- Enter activates a selected row where the view has an action: for example, selects a Call Stack frame, opens Stack row details, or opens register editing.

The mouse wheel scrolls the view under the pointer without taking keyboard focus. This lets you inspect another pane without redirecting subsequent keys. A scrollbar thumb can be dragged; clicking its track pages the viewport. Bars appear only for overflowing content. Source keeps line numbers fixed while its text pans.

Escape or Tab leaves content focus and returns to the last available control. Clicking a control or empty space outside the pane also leaves it. Escape does not close a debugging session.

### Move directly to a neighboring pane

While in CONTENT FOCUS, Ctrl+Left/Down/Up/Right transfers content focus to the visible neighbor in that direction. The neighbor keeps its active tab and browsing position. The transfer follows actual pane geometry, including nested splits, and stops at layout edges.

Optional Vim aliases are Ctrl+H/J/K/L. Terminals can encode Ctrl+H as Backspace and Ctrl+J as line feed, so these aliases apply to content transfer only in this context; they do not replace field editing.

Outside fields, `[` and `]` cycle panes and `1`–`9` select retained pane IDs. These are configurable shortcuts, not a universal row/column numbering scheme after splits. Hidden panes are excluded from spatial transfer. Source fullscreen disables pane switching so you cannot select an invisible pane.

## Expand the lower inspectors

Click a lower dock title or its `^` to expand it beneath that column's primary pane. Click `_`, immediately before `...`, to minimize it again. Each column's split is independent: opening Console reduces Registers' height, not Source's or Disassembly's.

Drag the horizontal separator to adjust the two partners' heights. Both remain accessible. Multiple lower groups can stay open at different heights. Preferred heights survive terminal resizing during the session; minimizing gives the space back to the primary pane. Lower expansion and heights are not restored by a saved setup.

For example, expand Locals while stepping in Source. Click Source once to establish source stepping, then inspect a local. The toolbar keeps source stepping even though an inspector now has focus. Use Ctrl+Up from lower-middle content to reach the primary pane above it; the result is based on current geometry.

## Change tabs and split panes

Open a pane's **...** menu. A filled circle marks views already included in that pane; a `*` substitutes when the terminal cannot display the circle. This marker is local to the pane, not a global open/closed state.

Choose an absent view to add and activate it. Choose an included view to remove its tab. Every pane retains at least one tab. To manipulate the pane itself, choose **Pane actions >**:

| Action | Result |
|---|---|
| Split right / Split below | Creates a tiled neighbor within the current group. |
| Close selected tab | Removes the active view tab when another remains. |
| Close split pane | Removes a nested pane and expands its sibling. Root groups retain a pane. |
| Resize dividers | Starts keyboard divider adjustment. |
| Fullscreen Source | Available for an active Source view only. |

There are no layout presets or floating debugging panes. Dialogs and menus float over the workspace, but the debugging views remain tiled. The workspace supports up to 32 panes, subject to available space and minimum tiled dimensions of 20×7 cells.

## Resize with mouse or keyboard

Drag the one-cell gutters between columns or nested panes. For keyboard resizing, choose **Pane actions → Resize dividers**, use Tab/Shift+Tab to select a divider, then arrows to move it. Enter or Escape finishes. Vim movement aliases also apply when enabled.

On a narrower terminal, the interface shows the selected column's pair where it fits, otherwise one retained view. Pane cycling and dock/numbered selectors provide access to other groups. Enlarging the terminal restores the retained split arrangement. Below 80×20, a resize warning replaces normal interaction while retaining state; the target is not automatically paused or disconnected.

## Use Source fullscreen

Choose **Source → Actions → Fullscreen Source**, or the same action under that Source pane's **... → Pane actions**. Source fills the working area; toolbar and status/help rows remain. Other panes and dividers are hidden.

Choose **Restore** in its header or **Actions → Restore tiled workspace** to return. At control focus, Escape also restores the tiles. If you are in content focus, the first Escape leaves content and the second restores. Menus and fields handle Escape locally first. Your splits, tab membership and proportions survive fullscreen and terminal resizing. This is session presentation state, not a saved setup or terminal-emulator fullscreen command.

## Menus and dialogs

**Actions**, **Options** and **...** open dropdowns attached to their controls, upward when needed. Use Up/Down, Home/End, Page Up/Down, the wheel or scrollbar; Enter selects, Escape cancels. Clicking outside dismisses the menu and consumes that click, so it does not trigger an underlying button. Immediate actions close the list.

Input, confirmation and review dialogs appear centered over the retained workspace or session form. Outside clicks leave them open. **Cancel**, **Back** or Escape returns to the invoking control. Connection reviews for every session type remain over the configuration form; Start/Connect is a separate deliberate acceptance.

An open menu is not a pause button. Debugger events continue, and configured execution shortcuts remain available in contextual lists. A target may resume, stop or disconnect while you are reading a menu, changing which actions are valid.

## Enter, edit and paste text

With Vim-like input off, fields accept literal printable text, including `q`, digits and `h/j/k/l`. Left/Right, Home/End, Backspace and Delete edit the current line. Ctrl+A/Ctrl+E moves to its beginning/end; Ctrl+U clears it. Do not assume every shell or Emacs editing shortcut is implemented.

Tab/Shift+Tab changes control focus. Up/Down can move between form rows. Enter invokes the field's operation where one exists: Console sends, Memory Go reads, and a draft editor advances to Review. Filling a connection field is separate from choosing **Review session** and accepting the review.

In a terminal supporting bracketed paste, pasted tabs and newlines become spaces; other control characters are ignored. A paste stays in the focused field, never submits or moves focus, and discards text beyond the 255-character field limit with a truncation notice. Pastes outside fields are ignored. Review the result before submitting. Terminals without bracketed-paste support cannot distinguish pasted shortcut bytes from typed keys.

When Vim-like input is enabled, fields start in **VIM NORMAL**; press `i` to insert. Escape first leaves Insert, then leaves the field/dialog. The [full field-mode reference](17-customization.md#vim-like-input) explains the supported motions.

## Recognize feedback

Selection styling identifies a row/control, not necessarily the target's current execution location. `>` in code identifies stopped execution; `~` identifies an inspected caller. Registers and variables use `*` for changed samples. Memory and other inspectors explicitly label stale observations. Text and symbols preserve these distinctions without color.

Read the pane header and bottom status after an action. A pending request is not a successful read; a write acknowledgment is not verification. The [indicator reference](20-controls_and_glossary.md#indicators-and-visible-states) lists these states together.
