# Contributing

The Bugger is a Linux-hosted, terminal-native GDB frontend written in C17.
Embedded debugging drives feature priorities; native Linux remains a maintained,
regression-tested capability. See the [roadmap](docs/roadmap.md) before proposing
work.

## Branches and pull requests

`develop` is the integration branch and default target for contributions. `main`
contains stable release history. Create `feature/<description>` or
`fix/<description>` branches from `develop`, and open focused pull requests back
to `develop`. Discuss scope before starting a new roadmap feature.

Release preparation starts from `develop`; published releases belong to `main`.
Keep the integration and release branches consistent, preserving commits referenced
by validation records. Urgent `hotfix/<description>` branches start from `main`;
integrate their fixes into both branches. See the
[release procedure](docs/maintaining.md#release-procedure).

Explain the problem, behavior change, relevant tests and limitations in the pull
request. Confirm that CI passes and address review conversations before merging.
Hardware-specific changes require the appropriate physical evidence in addition
to hosted checks. Reviewers should check capability handling, bounds, ownership
and read authorization as well as the happy path.

## Build and test

Build prerequisites and a first session are in [README](README.md#build-and-first-session).
The [testing guide](docs/testing.md) describes commands, focused suites, fixtures,
sanitizers, static analysis and physical validation. Start with a build and the
checks relevant to your change:

```sh
make
make test                 # Model and terminal checks
make test-native          # When changing debugger integration
git diff --check
```

Use a separate `BUILD_DIR` for another compiler or instrumentation. GDB/ptrace,
tmux and hardware prerequisites vary by suite; report unavailable checks rather
than counting them as passed. C++ is permitted for debug-target fixtures only.
Before submitting code, tests or build changes, run the complete fortified
[`test-ci` baseline](docs/testing.md#hosted-ci) in an unused build directory and
retain its log. Focused tests support iteration but do not replace that baseline.
Documentation-only changes need link, command and consistency checks rather than
unrelated local application test runs; hosted PR checks still apply.

## Continuous integration

[GitHub Actions](.github/workflows/ci.yml) runs on pull requests and manual
dispatch. Automatic push runs are disabled to avoid duplicate checks for the
same pull request update. The jobs are:

- **Tests** (Ubuntu 26.04, GDB 17+): `make test-ci`, sharing the default, native,
  tmux, localhost remote and offline OpenOCD suites with local preflight.
- **Static analysis** (Ubuntu 24.04): `make analyze`, with analyzer/bugprone warnings
  treated as errors, plus compilation of all C test sources with fortification.

The test job installs GDB/gdbserver, tmux, OpenOCD and the build dependencies.
Test compilation explicitly enables `_FORTIFY_SOURCE=3`.
The OpenOCD check stops before hardware initialization. Logs are retained for
seven days. Sanitizers, slow recovery, packaging and physical tests run separately.

## Code and design conventions

- Builds of project-owned code must include `-Wall` and `-Werror`; fix emitted
  warnings before committing rather than bypassing the policy. This includes
  application, tests and embedded fixtures. SDK/vendor components retain their
  own policy; project-owned ESP sources override inherited warning-as-error
  exceptions privately on their component.
- Keep application and harness code C17 with the repository's strict warnings.
  Use existing Make/ncursesw/POSIX interfaces; introduce modules only when needed.
- Preserve explicit ownership, bounded buffers/queues/work and nonblocking input.
  Keep parsing, transport, session orchestration, models, layout and rendering
  separate. Callbacks borrow records; copy anything retained beyond return.
- Use structured GDB/MI, token correlation and generation checks. Do not derive
  debugger state from human CLI text or fabricate missing target data.
- Spawn literal argument vectors. Shell quoting and MI quoting are different;
  do not concatenate user fields into shell commands. Validate paths with spaces.
- Memory/value inspection must satisfy the [pre-dispatch access policy](docs/architecture.md#locals-and-watches).
  Calls/assignment filters alone are not safety proofs. No implicit flash, reset,
  writes, peripheral polling or termination of external servers.
- Preserve the [interface specification](docs/requirements.md#interface-constraints): semantic theme roles,
  Catppuccin Macchiato default, three primary columns with independent lower panes,
  tabs, dynamic splits, mouse and keyboard access, no presets or floating debugging panes. Prototype samples never count as live functionality.
- Keep changes small and reviewable. Avoid unrelated formatting or framework
  changes. Shared changes retain native regression coverage; target-specific
  behavior requires explicit capabilities and appropriate evidence.

## Tests and documentation

Add focused C tests for changed behavior and meaningful failure cases, including
stale responses, bounds and cleanup where relevant. Keep assertions and safety checks intact when diagnosing failures.

Documentation responsibilities are:

| Document | Purpose |
|---|---|
| [User guide](docs/tutorial/README.md) | Current controls, configuration and limitations |
| [Requirements](docs/requirements.md) | Product scope, FR/NFR identifiers and release criteria |
| [Architecture](docs/architecture.md) | Implemented modules, interfaces, invariants and extension boundaries |
| [Roadmap](docs/roadmap.md) | Completion status, planned scope, dependencies and version targets |
| [Testing](docs/testing.md) | Reproducible procedures and evidence requirements |
| [Maintaining](docs/maintaining.md) | Review, release procedures and distribution artifacts |
| [Changelog](CHANGELOG.md) | User-visible changes and compatibility between releases |
| [Validation records](docs/validation.md) | Recorded hashes, results, failures and decisions |

## Security reports

Report vulnerabilities privately using the [security policy](SECURITY.md).
Use public issues for ordinary bugs; keep exploit details and sensitive target
data out of public reports and pull requests before coordinated disclosure.

## Change review

Coordinate roadmap feature scope before starting and keep unrelated work in
separate pull requests. Proposed Conventional Commit titles are at most
50 characters, with a short body for relevant behavior and validation details.
Routine commits include relevant implementation, tests and documentation together.
Separate documentation follow-ups remain useful when the code is already committed.
The [maintainer workflow](docs/maintaining.md#commit-organization-and-traceability)
records exact implementation hashes at release or when an investigation needs
revision-level evidence; existing historical references are preserved.
Maintainers approve milestones and control merges, tags and publication.
