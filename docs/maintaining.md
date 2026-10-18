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
exceptions do not replace the normal two-commit lifecycle for future gates.

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
   for native x86-64, real STM32/ARM, ESP32/Xtensa and ESP32/RISC-V. QEMU ARM/RISC-V
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

## CI planning

No hosted workflow is configured. A future reviewed Linux CI setup can use GCC,
Clang, Make, pkg-config, ncursesw/libelf/json-c development files, terminfo and
clang-tidy with the documented checks. Native/tmux jobs need their actual runtime
permissions. Upload failure logs; keep physical acceptance separately recorded.
Do not label a board passed from a synthetic or hosted run.
