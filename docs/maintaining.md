# Maintenance and release procedures

Technical scope is in [requirements](requirements.md) and the [roadmap](roadmap.md).
Results belong in [validation records](validation.md); coding-agent rules are in
`AGENTS.md`.

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
   relevant documentation together for routine work. Revise the message after fixes.
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

Git operations remain owner-controlled unless explicitly delegated. An acceptance
report does not authorize pushing, merging or publishing. Documentation-only
changes need consistency checks; run application tests when a technical change
requires them.

## Commit organization and traceability

Routine feature and fix commits include their source, tests, build changes and
relevant requirements, architecture, usage, changelog and validation updates.
Use separate commits for independent documentation work, already-committed code
or an agreed review boundary. List exact staging paths.

Record implementation hashes when assembling release evidence, and earlier only
when a regression, security finding or other investigation needs an exact revision.
Routine records identify scope, acceptance and checks. At release, record the
tag/commit, cumulative changelog, test results, artifact checksums and relevant
implementation revisions.

Preserve historical hashes, failures and acceptance records.

## Release recommendation at delivery

After the proposed commit message, assess the cumulative changes since the latest
public release, including intervening commits, current work and `CHANGELOG.md`.
A completed task does not automatically justify a release. If none is warranted,
add the line **No release needed yet.**

When a release is warranted, recommend a version and explain why, provide a
publication-ready title and description, and give sequential preparation and
publication steps through the protected-branch workflow. Identify outstanding
checks or acceptance conditions before publication. The owner authorizes publication.

## Evidence records

Use states `planned`, `implemented`, `automated-verified`, `awaiting-user` and
`accepted`. Only manual acceptance or an owner-reported/observed milestone commit
permits `accepted`. Record command, result, relevant artifact, who ran it, tool/
environment limitations and physical setup when applicable. Record exact revisions
for releases and investigations as described above.
Keep current status in the roadmap and durable results in the validation register.
Store raw logs under ignored `build/`. Update README after technical documents.

## Test execution policy for coding agents

Focused checks support development; the complete hosted Tests target list is
shared through `make test-ci`. Code/test/build handoffs include a fresh fortified
run of that baseline, with command, tool versions and log location recorded.
Existing input and terminal tests remain necessary when adding shortcuts; new
shortcut tests alone do not establish compatibility with frame selection, text
entry or terminal restoration. Report local and hosted results separately. See the
[reproduction command](testing.md#hosted-ci).

Current agent execution authorization is governed by `AGENTS.md` and explicit
user instructions. Additional stress runs, sanitizer/compiler matrices, physical
hardware and other checks outside the normal host baseline retain their separate
authorization requirements. Identify who ran each check.

## Release procedure

Use MAJOR.MINOR.PATCH versioning based on cumulative changes and roadmap scope.

For every release, including patches:

1. Audit all release-blocking FRs and assess every NFR. Resolve critical defects,
   unsafe defaults, terminal corruption, leaks and unusable interaction paths.
2. Collect actual [release-matrix evidence](testing.md#every-release-regression-matrix)
   for native x86-64 and physical ARM/Cortex-M, Xtensa and RISC-V, applying
   the [recorded release deferrals](requirements.md#release-acceptance).
   QEMU is supplementary.
3. Record board/probe, firmware, GDB/server/toolchain versions, capability-specific
   exclusions, new-feature regression, reset/disconnect and cleanup behavior.
   Missing hardware/support requires owner resolution before release acceptance.
4. Review requirements, compatibility, limitations, clean-checkout instructions,
   changelog, provenance/license status and the validation evidence summary.
   Record the actual release tag/commit, relevant implementation revisions and
   artifact checksums without upgrading unrun checks to PASS.
5. Obtain explicit final owner release acceptance and authorization before
   recommending a release commit/tag. Do not create or publish it automatically.

Agree on acquisition hardware and backend capabilities before implementing a
hardware-dependent gate. Review unavailable capabilities with the owner.

## Linux x86-64 release artifacts

The version comes from `include/the_bugger/version.h`.
`make release` builds a separate optimized executable with PIE, stack protection,
FORTIFY and full RELRO, then prepares these files under `build/dist/v<version>/` by default.
`RELEASE_DIST_DIR` overrides the destination without changing archive formats:

```sh
make release
# Optional alternate output directory:
make RELEASE_DIST_DIR=/path/to/output release
```

Each version directory contains:

- `the-bugger-v<version>-linux-x86_64.tar.gz`: one host binary, GPLv3 license,
  runtime instructions, build/dependency information and `THIRD_PARTY_NOTICES`.
- `the-bugger-v<version>-source.tar.gz`: corresponding working-tree source and build
  instructions, including packaging infrastructure.
- `SHA256SUMS`: checksums for both archives. The binary archive also identifies
  its matching source archive by checksum.

Packaging needs Bash, Git, GNU binutils, tar, gzip and coreutils. It packages
tracked working-tree files, so rebuild after final source/documentation changes.
`SOURCE_DATE_EPOCH` controls archive timestamps and defaults to zero.

The executable is dynamically linked; shared libraries, terminfo, GDB and OpenOCD
remain external. `BUILD-INFO.txt` records direct libraries, executable GLIBC
symbol requirements and the actual build host. Use those requirements when assessing compatibility with another distribution.

Follow the [artifact checks](testing.md#release-artifacts-m18) before proposing
release acceptance.

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
successful hosted run. Hardware, sanitizer, slow-recovery and release-artifact checks run separately.
Review runner and action-pin updates.
