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
3. Include exact staging paths and one proposed Conventional Commit message:
   title at most 50 characters plus a detailed body. Include source, tests and
   relevant documentation together for routine work. The message does not imply
   acceptance; revise it after fixes.
4. Stop for manual validation. Fix reported issues and repeat relevant validation.
5. Explicit acceptance is required. Simple positive feedback without issues also
   means acceptance and continuation authorization unless stated otherwise;
   questions/instructions are not acceptance. Silence or automated success is not.
6. The repository owner stages, commits and pushes the reviewed work. An
   owner-reported or observed implementation commit establishes acceptance; do not
   demand a duplicate approval or leave the gate awaiting approval afterward.
7. Continue only after the relevant commit/push gate and authorization. If code
   was already committed, deliver any remaining documentation as a focused
   follow-up rather than rewriting the implementation commit.

Never independently commit, push, tag, force-push, rebase or merge unless explicitly
requested. Do not bypass Git gates through implicit positive feedback. Conversely,
do not repeatedly ask for confirmation or inspect Git state after a clear report
of completion. Read an actual hash once when release evidence or a specific
investigation requires it; do not invent hashes, substitute placeholders or run
redundant remote checks. Documentation-only changes do not require application
tests unless a changed technical claim creates a concrete verification need.

## Commit organization and traceability

Routine feature and fix commits include their source, tests, build changes and
relevant requirements, architecture, usage, changelog and validation updates.
A mandatory implementation/documentation split is no longer required merely to
obtain the implementation hash. Separate commits remain appropriate for substantial
independent documentation work, already-committed code, or an explicitly requested
review boundary. Never suggest indiscriminate `git add .`; list exact paths.

Record implementation hashes when assembling release evidence, and earlier only
when a regression, security finding or other investigation needs an exact revision.
Routine gate records identify scope, acceptance and executed checks without a
mandatory SHA. At release, associate the release tag/commit with its cumulative
changelog, actual test results, artifact checksums and relevant implementation
references. Resolve those references from real Git history; do not create a commit
solely to make its own hash available to documentation.

Existing hashes, test failures and acceptance records remain intact. Earlier
milestones used the former two-commit process, with recorded scoped exceptions;
those historical facts are not instructions for future work.

## Release recommendation at delivery

After the proposed commit message, assess the cumulative changes since the latest
public release, including intervening commits, current work and `CHANGELOG.md`.
A completed task does not automatically justify a release. If none is warranted,
add the line **No release needed yet.**

When a release is warranted, recommend a version and explain why, provide a
publication-ready title and description, and give sequential preparation and
publication steps through the protected-branch workflow. Identify outstanding
checks or acceptance conditions before publication. Release guidance is not
permission to publish, tag, merge or push; those actions remain owner-controlled.

## Evidence records

Use states `planned`, `implemented`, `automated-verified`, `awaiting-user` and
`accepted`. Only manual acceptance or an owner-reported/observed milestone commit
permits `accepted`. Record command, result, relevant artifact, who ran it, tool/
environment limitations and physical setup when applicable. Record exact revisions
for releases and investigations as described above.
The roadmap shows concise current status; the validation records preserves detailed
evidence and corrections. Do not rewrite a failure into PASS or infer board
compatibility from native/corpus tests. Ignored `build/` logs are local artifacts;
durable result summaries belong in tracked records. README is updated after the
technical documents so its capabilities and links match the final result.

## Test execution policy for coding agents

Focused checks support development; the complete hosted Tests target list is
shared through `make test-ci`. Code/test/build handoffs include a fresh fortified
run of that baseline, with command, tool versions and log location recorded.
Existing input and terminal tests remain necessary when adding shortcuts; new
shortcut tests alone do not establish compatibility with frame selection, text
entry or terminal restoration. Local host results do not establish Ubuntu runner
success. See the [reproduction command](testing.md#hosted-ci).

Current agent execution authorization is governed by `AGENTS.md` and explicit
user instructions. Additional stress runs, sanitizer/compiler matrices, physical
hardware and other checks outside the normal host baseline retain their separate
authorization requirements. Distinguish agent-run and owner-run evidence; no
check is passed merely because a command was supplied.

## Release procedure

v0.1.0 is the first public release. Use MAJOR.MINOR.PATCH versioning and assess
later versions from actual changes and the roadmap's planning targets. Its
M14–M18 evidence remains the baseline; future gates retain their own scope.
Release preparation does not absorb deferred features.

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
   Record the actual release tag/commit, relevant implementation revisions and
   artifact checksums without upgrading unrun checks to PASS.
5. Obtain explicit final owner release acceptance and authorization before
   recommending a release commit/tag. Do not create or publish it automatically.

Each hardware-dependent future gate names its actual acquisition/backend setup
before implementation. Missing trace/power/counter support prompts scoped review
or deferral, not invented compatibility or a blanket pre-v1 feature requirement.
The owner has final say on release readiness.

## Linux x86-64 release artifacts

The version comes from `include/the_bugger/version.h`.
`make release` builds a separate optimized executable with PIE, stack protection,
FORTIFY and full RELRO, then prepares these files under `build/dist/`:

- `the-bugger-v<version>-linux-x86_64.tar.gz`: one host binary, GPLv3 license,
  runtime instructions and build/dependency information. Builds containing F19
  also include `THIRD_PARTY_NOTICES` for the adapted theme palettes.
- `the-bugger-v<version>-source.tar.gz`: corresponding working-tree source and build
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

[CI](../.github/workflows/ci.yml) runs for all pull requests and manual dispatch.
Automatic push runs are disabled to avoid duplicate PR/push checks.
**Tests** runs the default,
native, tmux, actual localhost remote and non-hardware OpenOCD suites on Ubuntu
26.04 with GDB 17+. **Static analysis** runs `make analyze` and fortified
compilation of all C test sources on Ubuntu 24.04. Each job has a timeout and retains logs
for seven days even on failure; superseded runs on the same ref/PR are cancelled.
Actions are pinned to commit IDs, checkout credentials are not persisted, and
the workflow token has read-only repository contents permission. Untrusted PRs
use `pull_request`, not a privileged `pull_request_target` job.

Require **Tests** and **Static analysis** in branch protection after the first
successful hosted run. Do not substitute local results for GitHub execution
status. Hardware, sanitizer, slow-recovery and release-artifact checks retain
separate evidence requirements. Updating the runner or action pins requires
review; hosted test success does not establish physical compatibility.
