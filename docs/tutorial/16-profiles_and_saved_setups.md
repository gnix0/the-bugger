# 16 — Profiles and saved setups

[Guide index](README.md) · Previous: [Console](15-console_and_logs.md) · Next: [Customization](17-customization.md)

A **connection profile** remembers how to reach a program or target. A **saved setup** remembers the workspace and inspection definitions associated with that profile. Personal themes/keybindings are a third, separate kind of preference.

None of these is a snapshot of a running machine. Reopening a profile does not restore RAM, registers, stopped threads, access permissions or the previous process.

**On this page**

- [What each save contains](#what-each-save-contains)
- [Save and reopen a connection profile](#save-and-reopen-a-connection-profile)
- [Save the current workspace](#save-the-current-workspace)
- [Restore a saved setup](#restore-a-saved-setup)
- [Resolve partial restoration](#resolve-partial-restoration)
- [Review breakpoints created outside the frontend](#review-breakpoints-created-outside-the-frontend)
- [Where files live and how recovery works](#where-files-live-and-how-recovery-works)

## What each save contains

| Kind | Retains | Does not retain |
|---|---|---|
| Connection profile | Session kind, executable/symbol paths, tools, native arguments/directory, endpoint/protocol, OpenOCD script/search choices, source directories/mappings | Current PID, environment overrides, samples, themes/keybindings |
| Saved setup | Pane splits/proportions and tabs, Source context role/pinned files, Memory expressions/count/grouping, Watch roots/formats, supported code-breakpoint intent | Live values, PC/frame/thread context, temporary/data breakpoints, grants/evidence, fullscreen, lower-pane expansion/heights, scrolling, detailed Stack settings/samples/points |
| Preferences | Theme/variant, personal keybindings and Vim-like input | Connection and target state |

You can reuse a board connection without changing your personal controls. After restoring a setup, Read a Watch explicitly to obtain its value, and check that the saved breakpoints were installed; a saved definition does not guarantee available hardware resources.

## Save and reopen a connection profile

1. Fill in a session configuration.
2. Use **Profile...** for additional **Source directories**, **Source mappings**, **Ordered OpenOCD scripts**, **OpenOCD search directories**, or **Remote protocol**, as applicable.
3. For a list, use **Add entry**, **Edit entry** and **Remove entry**; Previous/Next pages the list. Return with Back. These edits remain a draft.
4. Choose **Save profile**, enter a nonempty name shorter than 64 bytes and save. Replacing an existing name requires confirmation.
5. At the launcher, open **Config → Profiles** and select the saved entry. Selection loads its configuration for review; it does not start tools.
6. Supply omitted session-only fields such as an Attach PID or environment overrides, then review and accept the connection normally.

Up to 16 named profiles and 8 recent entries are retained. Recent saved sessions are labeled in the profile list. An old PID is not reused as live identity. Saving never launches GDB or OpenOCD.

### Duplicate or delete

For an existing saved connection, **Profile... → Duplicate** asks for a new name and copies the saved profile and saved setup independently. Unsaved draft changes are excluded. To copy your current changes, save them first.

**Delete** requires a loaded saved profile and confirmation. It also deletes that profile's associated saved setup. Replacing a connection profile retains its setup for compatibility review instead of silently dropping it.

## Save the current workspace

In a live session, choose **Setup → Save setup** near Close Session/Quit. An asterisk marks changes to definitions that can be saved. The review names the associated profile, lists what will be saved and shows any connection or launch-context differences.

If there is no associated saved profile, choose the offered **Save profile** first. Then return and **Save setup**. Setup saving stores the current reviewed connection snapshot separately; it does not rewrite the connection profile.

For example, pin two source files, keep Watches beside Call Stack, configure Memory for a known buffer, and create a supported code breakpoint with a condition. Save this as a setup after the underlying connection profile is associated. Sample values and temporary grants remain session-only.

Saving waits for active source-setting and breakpoint work to settle. An external change to a managed breakpoint or uncertain restoration can block saving until explicitly reviewed; do not interpret the asterisk as an automatic save promise.

## Restore a saved setup

Reopen its connection profile and complete session review. An associated setup adds a **Session setup restoration review** before starting:

1. Leave **Restore saved setup** checked to restore its definitions, or clear it to start without them.
2. Review connection differences, effective local ELF and any changed relative-path launch context.
3. If configuration or ELF metadata changes during review, confirm the new review rather than relying on an earlier one.
4. Start the session and inspect **Setup → Restoration information**.

Expect the saved panes, tabs, Memory definitions and Watches to reappear, followed by source lookup settings and pinned files. Lower groups start minimized. Expand them as needed; Memory stays unread, and Watches require explicit Read. A matching local ELF file does not prove that the same firmware is programmed remotely.

If you deliberately skipped the old setup, saving over it asks for **Replace setup**. Replacement discards hidden old definitions; it does not merge an invisible previous workspace into your current one.

### Breakpoint restoration

Restored code breakpoints are initially disabled while their conditions, configured ignore counts and hardware policy are checked. Only then are saved enabled breakpoints enabled. GDB resolves pending symbols and multiple locations again. Even a successfully configured hardware breakpoint can fail to acquire a physical slot on resume; check the resulting Console message and stop state.

If restoration asks you to approve a literal breakpoint address, inspect its current location and use the explained **Retry** control only if it is still appropriate. Native addresses can be accepted without that extra approval when executable identity, mapped range and load address match. Remote literal addresses always require approval. These checks do not compare live bytes, and approval is not saved for another session.

The target is left running if restoration must await a stop. Use Pause/Halt deliberately. While active work restores debugger definitions, execution, Reset, Console and context/configuration changes are blocked. Local navigation and close remain available.

## Resolve partial restoration

Open **Setup → Unresolved setup entries**:

- **Unavailable Source pin:** the file's tab stays in place, including whether it was selected, but shows `!` without old source text. Repair source lookup, refresh the GDB source catalog, then choose **Retry** or **Remove**.
- **Unsupported saved Watch:** its expression stays unread. Remove it if this build cannot inspect it.
- **Breakpoint failure:** inspect the reason before choosing **Retry**. A known disabled breakpoint created during the failed attempt can be reused. **Remove** drops the saved definition from this setup; it does not delete a partial breakpoint already created in GDB. Check the Breakpoints view before continuing.
- **Enabled failures:** **Acknowledge failures** permits execution without claiming those stops are installed. The unchanged failure set does not repeatedly prompt. Decide whether proceeding without them suits your task.

**Restoration information → Cancel restoration** stops further requests without undoing work already applied. **Cancel requested** remains until outstanding requests finish. Already-applied definitions can remain. Saving a partially restored setup retains unresolved definitions; it does not mark them as successfully installed.

If GDB's response leaves a breakpoint change uncertain, restoration pauses while its breakpoint table is checked. The change is not repeated. If the result remains incomplete or ambiguous, execution stays blocked: use **Retry metadata reconciliation** to check the table again, or close the session. Cancellation also waits for uncertain work to finish.

## Review breakpoints created outside the frontend

Console/GDB can create or change breakpoints independently of the frontend's saved intentions. Choose **Setup → Review GDB breakpoints**, refresh metadata and inspect an eligible complete definition:

1. Choose **Review for import** or **Review managed definition**.
2. Read location/source, enabled intent, condition and observations.
3. Explicitly choose a new **Default / Require hardware** policy and ignore-count intent.
4. Choose **Include reviewed definition** to add it to the unsaved setup draft, then save the setup separately.

Import records a definition for saving; it does not change the GDB breakpoint. Choose the policy and ignore count you want restored next time rather than inferring them from the current hardware type, hit count or remaining ignore count. Temporary breakpoints, data watchpoints, incomplete entries and definitions with unsupported command lists/restrictions cannot be imported; the review explains why. Already included definitions cannot be imported twice.

External changes to a managed definition block saving until reviewed or **Exclude managed definition** removes its entire saved intent. Exclusion leaves GDB unchanged. Runtime hits and decreasing remaining ignore counts do not dirty or rewrite saved configured counts.

## Where files live and how recovery works

Profiles/setups use `profiles.json`; themes/bindings use `preferences.json`. Both are under:

```text
$XDG_CONFIG_HOME/the-bugger/
```

when `XDG_CONFIG_HOME` is absolute, otherwise under:

```text
$HOME/.config/the-bugger/
```

Use the application's save controls rather than editing JSON fields by hand. If another running instance saves newer settings, this instance refuses to overwrite them. Reload the profile list before saving profiles/setups, or restart before saving preferences.

If loading or saving reports an invalid or unsupported file:

1. Preserve a backup of the file before repairing or replacing it. The application leaves the problematic file intact.
2. For profiles, **READ ONLY** recovery may still let you open usable connections when a saved setup is malformed. Saving profiles/setups and updating recents remain blocked until the whole file is valid.
3. After correcting the profiles file or restoring a known-good backup, choose **Config → Profiles → Reload file** and check the result before saving.
4. For preferences, the application uses visible defaults and blocks saving. Correct the file or restore a known-good backup, then restart; reloading profiles does not reload preferences.

Current builds can read older profile files, but saving profiles or setups can update their format. Older executables may then be unable to read them, especially after a setup has been saved. Keep a backup before saving with a newer version if you still need an older executable; there is no automatic downgrade. Personal theme/key preferences remain separate from profiles.
