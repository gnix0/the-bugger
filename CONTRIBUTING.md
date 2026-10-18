# Contributing

The Bugger is a Linux-hosted, terminal-native GDB frontend written in C17.
Embedded debugging drives feature priorities; native Linux remains a maintained,
regression-tested capability. See the [roadmap](docs/roadmap.md) before proposing
work. Planned features are not necessarily available in the current build.

## Build and test

Build prerequisites and a first session are in [README](README.md#build-and-first-session).
The [testing guide](docs/testing.md) describes commands, focused suites, fixtures,
sanitizers, static analysis and physical validation. A typical development check is:

```sh
make all fixtures
make test
make test-native
make test-tmux
git diff --check
```

Use a separate `BUILD_DIR` for another compiler or instrumentation. GDB/ptrace,
tmux and hardware prerequisites vary by suite; report unavailable checks rather
than counting them as passed. C++ is permitted for debug-target fixtures only.

## Code and design conventions

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
- Preserve the [interface specification](docs/design/tui-mapping.md): Monokai Pro,
  default six tiles, tabs, dynamic splits, mouse and keyboard access, no presets
  or floating windows. Prototype samples never count as live functionality.
- Keep changes small and reviewable. Avoid unrelated formatting or framework
  changes. Shared changes retain native regression coverage; target-specific
  behavior requires explicit capabilities and appropriate evidence.

## Tests and documentation

Add focused C tests for changed behavior and meaningful failure cases, including
stale responses, bounds and cleanup where relevant. Native success does not prove
embedded compatibility. Tests must expose application defects; do not weaken
assertions or safety checks to obtain a passing result.

Documentation responsibilities are:

| Document | Purpose |
|---|---|
| [Usage](docs/usage.md) | Current controls, configuration and limitations |
| [Requirements](docs/requirements.md) | Product scope, FR/NFR identifiers and release criteria |
| [Architecture](docs/architecture.md) | Implemented modules, interfaces, invariants and extension boundaries |
| [Roadmap](docs/roadmap.md) | Completion status, planned scope, dependencies and version targets |
| [Testing](docs/testing.md) | Reproducible procedures and evidence requirements |
| [Design](docs/design/tui-mapping.md) | Interface constraints, terminal mapping and reference rationale |
| [Historical ledger](docs/history/milestones.md) | Recorded hashes, results, failures and decisions |

Use concise technical descriptions. Distinguish implemented behavior, validation
gaps and plans. Keep execution records out of procedures and agent instructions
out of public technical references. Link to the canonical description instead of
copying it into several documents. Never invent test results or commit hashes.

## Change review

Work is divided into the existing M/F gates. Coordinate scope before starting;
do not combine unrelated gates. Proposed Conventional Commit titles are at most
50 characters, followed by a body explaining behavior, validation and limitations.
Implementation and documentation are separate commits under the
[maintainer workflow](docs/maintaining.md). Maintainers control repository Git
operations and acceptance; this guide does not authorize automatic commits,
pushes, tags or releases. Coding agents also follow the local `AGENTS.md`.
