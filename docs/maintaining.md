# Maintenance and release procedures

Technical scope is in [requirements](requirements.md) and the [roadmap](roadmap.md).
Test results are recorded in [validation records](validation.md).

## Milestone lifecycle

Each M/F gate includes implementation, tests, documentation and manual review.
Agree on scope changes before implementation.

1. Implement the change and run the required checks.
2. Submit the changes, test results and manual reproduction steps for review.
3. Fix reported issues and repeat affected checks.
4. Obtain maintainer acceptance before merging and starting the next gate.

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

## Release planning

Assess cumulative changes since the latest release against `CHANGELOG.md` and the
roadmap. Propose a version, release notes and any outstanding validation before
starting release preparation. Maintainers approve publication.

## Evidence records

Use states `planned`, `implemented`, `automated-verified`, `awaiting-user` and
`accepted`. Record commands, results, artifacts, environment and physical setup.
Mark acceptance after maintainer review. Record revisions at release or when
needed for an investigation.
Keep current status in the roadmap and durable results in the validation register.
Store raw logs under ignored `build/`. Update README after technical documents.

## Required checks

Focused checks support development; the complete hosted Tests target list is
shared through `make test-ci`. Code, test and build changes require a fresh
fortified baseline with commands, tool versions and logs recorded.
Existing input and terminal tests remain necessary when adding shortcuts; new
shortcut tests alone do not establish compatibility with frame selection, text
entry or terminal restoration. Report local and hosted results separately. See the
[reproduction command](testing.md#hosted-ci).

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
   Missing hardware/support requires maintainer review before release acceptance.
4. Review requirements, compatibility, limitations, clean-checkout instructions,
   changelog, provenance/license status and the validation evidence summary.
   Record the actual release tag/commit, relevant implementation revisions and
   artifact checksums without upgrading unrun checks to PASS.
5. Obtain final maintainer acceptance before tagging and publishing.

Agree on acquisition hardware and backend capabilities before implementing a
hardware-dependent gate. Review unavailable capabilities with the maintainer.

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
