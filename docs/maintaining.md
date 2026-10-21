# Maintenance and release procedures

This document defines repository workflow, not product functionality. Technical
scope is in [requirements](requirements.md), [design](requirements.md#interface-constraints) and
the [roadmap](roadmap.md). Coding-agent execution rules are in `AGENTS.md`.
Past execution results are in [validation records](validation.md); those results
do not grant current implementation or test authorization.

## Milestone lifecycle

Work on one authorized M/F gate at a time. Each gate includes its bounded scope,
relevant tests, documentation and a reproducible manual demonstration. Preserve
accepted work and history. Scope changes, combining gates or splitting an overly
broad deliverable require review before implementation; do not silently widen work.

1. Implement only the authorized scope and run the permitted relevant checks.
2. Deliver behavior changes/exclusions, exact test commands and results, artifact
   locations, limitations and a numbered manual checklist with expected results.
3. Include exact implementation staging paths and a proposed Conventional Commit
   message: title at most 50 characters plus a detailed body. The message does
   not itself imply acceptance. Revise it if manual feedback leads to fixes.
4. Stop for manual validation. Fix reported issues and repeat relevant validation.
5. Explicit acceptance is required. Simple positive feedback without issues also
   means acceptance and continuation authorization unless stated otherwise;
   questions/instructions are not acceptance. Silence or automated success is not.
6. The repository owner stages, commits and pushes implementation changes. An
   observed owner-created implementation commit establishes acceptance; do not
   demand a duplicate approval or leave the gate awaiting approval afterward.
7. Finalize separate documentation with the real implementation hash and confirmed
   evidence. Deliver documentation staging paths/message; the owner commits/pushes
   it. Continue only after the relevant commit/push gate and authorization.

Never independently commit, push, tag, force-push, rebase or merge unless explicitly
requested. Do not bypass Git gates through implicit positive feedback. Conversely,
do not repeatedly ask for confirmation or inspect Git state after a clear report
of completion. Read an actual implementation hash once when required for the
documentation record; do not invent it, substitute a placeholder or run redundant
remote checks. Documentation-only changes do not require application tests unless
a changed technical claim creates a concrete verification need.

## Implementation and documentation commits

The implementation commit contains source, tests, build configuration and related
implementation files. The documentation commit contains requirements, architecture,
roadmap, README/changelog and evidence updates referencing that implementation.
Prepare documentation as work progresses; finalize hashes/results only after the
implementation commit. Never suggest indiscriminate `git add .`; list paths.

Exceptions remain explicit and scoped: P00 and the historical post-M08 pane-tab
correction used combined documentation/code commits. The M10 completion required
explicit manual approval before its implementation message. Those accepted
exceptions do not replace the normal two-commit lifecycle for future gates. M18
finalization has an explicit exception: the pending release-preparation build
changes, CI, security policy and documentation form one preparation commit on
`develop`; a separate owner-created release commit follows. Record that actual
preparation hash once available. Release publication/tagging remains a separate
explicit owner action. This does not change future milestone gates.

## Evidence records

Use states `planned`, `implemented`, `automated-verified`, `awaiting-user` and
`accepted`. Only manual acceptance or an observed owner-created milestone commit
permits `accepted`. Record command, result, relevant artifact, who ran it, tool/
environment limitations, physical setup when applicable and the observed SHA.
The roadmap shows concise current status; the validation records preserves detailed
evidence and corrections. Do not rewrite a failure into PASS or infer board
compatibility from native/corpus tests. Ignored `build/` logs are local artifacts;
durable result summaries belong in tracked records. README is updated after the
technical documents so its capabilities and links match the final result.

## Test execution policy for coding agents

Run only quick focused checks directly. Before lengthy suites, stress runs,
sanitizer/compiler matrices or other slow verification, provide exact commands
and wait for owner-run results unless explicit permission covers that run.
Distinguish agent-run and owner-run evidence. General implementation permission
is not permission for lengthy tests. M10 completion, M11 corrections and the
final M13 regression exception were limited to those tasks; none is standing
authorization. No check is passed merely because a command was supplied.

## Release procedure

The target is v0.1.0, with MAJOR.MINOR.PATCH versions and no prerelease suffix.
Keep the M14 native integration gate intact, then complete M15 remote, M16 OpenOCD,
M17 physical boards and M18 release evidence. Future gates retain their own scope;
release preparation does not absorb deferred features.

For every release, including patches:

1. Audit all release-blocking FRs and assess every NFR. Resolve critical defects,
   unsafe defaults, terminal corruption, leaks and unusable interaction paths.
2. Collect actual [release-matrix evidence](testing.md#every-release-regression-matrix)
   for native x86-64, real STM32/ARM, ESP32/Xtensa and ESP32/RISC-V, applying
   the explicit [v0.1.0 RISC-V deferral](requirements.md#release-acceptance). QEMU ARM/RISC-V
   adds coverage where available and never replaces boards. UNVERIFIED is not PASS.
3. Record board/probe, firmware, GDB/server/toolchain versions, capability-specific
   exclusions, new-feature regression, reset/disconnect and cleanup behavior.
   Missing hardware/support requires owner resolution before release acceptance.
4. Review requirements, compatibility, limitations, clean-checkout instructions,
   changelog, provenance/license status and the validation evidence summary.
5. Obtain explicit final owner release acceptance and authorization before
   recommending a release commit/tag. Do not create or publish it automatically.

Each hardware-dependent future gate names its actual acquisition/backend setup
before implementation. Missing trace/power/counter support prompts scoped review
or deferral, not invented compatibility or a blanket pre-v1 feature requirement.
The owner has final say on release readiness.

## Linux x86-64 release artifacts

`make release` builds a separate optimized executable with PIE, stack protection,
FORTIFY and full RELRO, then prepares these files under `build/dist/`:

- `the-bugger-v0.1.0-linux-x86_64.tar.gz`: one host binary, GPLv3 license,
  runtime instructions and build/dependency information.
- `the-bugger-v0.1.0-source.tar.gz`: corresponding working-tree source and build
  instructions, including packaging infrastructure.
- `SHA256SUMS`: checksums for both archives. The binary archive also identifies
  its matching source archive by checksum.

Packaging additionally needs Bash, Git, GNU binutils, tar, gzip and coreutils.
It uses tracked working-tree files plus the packaging script; review local edits
before distributing. It never stages, commits, tags or publishes. Rebuild after
final source/documentation changes so the archives describe the final contents.
`SOURCE_DATE_EPOCH` controls archive timestamps; the default is zero. This does
not assert reproducibility across different compilers or dependency versions.

The executable is dynamically linked; shared libraries, terminfo, GDB and OpenOCD
remain external. `BUILD-INFO.txt` records direct libraries, executable GLIBC
symbol requirements and the actual build host. Older-distribution compatibility
requires separate evidence. One Linux x86-64 frontend serves the supported target
architectures; it is not a firmware image or a Windows distribution.

Follow the [artifact checks](testing.md#release-artifacts-m18) before proposing
release acceptance. A prepared archive is not an approved or published release.

## Continuous integration

[CI](../.github/workflows/ci.yml) runs on Ubuntu 24.04 for pushes to `main` and
`develop`, all pull requests and manual dispatch. **Tests** runs the default,
native, tmux, actual localhost remote and non-hardware OpenOCD suites.
**Static analysis** runs `make analyze`. Each job has a timeout and retains logs
for seven days even on failure; superseded runs on the same ref/PR are cancelled.
Actions are pinned to commit IDs, checkout credentials are not persisted, and
the workflow token has read-only repository contents permission. Untrusted PRs
use `pull_request`, not a privileged `pull_request_target` job.

Require **Tests** and **Static analysis** in branch protection after the first
successful hosted run. Do not substitute local results for GitHub execution
status. Hardware, sanitizer, slow-recovery and release-artifact checks retain
separate evidence requirements. Updating the runner or action pins requires
review; hosted test success does not establish physical compatibility.
