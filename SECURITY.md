# Security Policy

The Bugger is an embedded-first, terminal-native GDB frontend for Linux. This
policy covers the frontend and its integration with debugger tools, local
processes, remote servers and embedded targets.

## Reporting a vulnerability

Please report vulnerabilities privately. Do not publish exploit details in public
issues, discussions or pull requests before coordinated disclosure.

Use **Security → Report a vulnerability** in the
[project repository](https://github.com/gnix0/the-bugger/security), when available,
or open the [private reporting form](https://github.com/gnix0/the-bugger/security/advisories/new).
Private reporting must be enabled separately in GitHub; adding this file does
not enable it. If the form is unavailable, open an issue requesting a private
security contact **without including vulnerability details or sensitive data**.
See [GitHub's private reporting guidance](https://docs.github.com/en/code-security/how-tos/report-and-fix-vulnerabilities/report-privately).

Include what you can:

- Affected version or commit, build options, Linux distribution and architecture.
- GDB, OpenOCD or other server versions; terminal/tmux details when relevant.
- Affected source files and lines, or the feature and operation involved.
- Reproduction steps and a minimal proof of concept or sanitized input/MI trace.
- Expected versus actual behavior, impact and required attacker access.
- For embedded issues, the board, probe, firmware context and whether reproducing
  could read sensitive storage, modify memory, reset, erase or damage a target.
- Any workaround or suggested fix.

Remove credentials, private firmware/source, personal paths, device identifiers
and sensitive memory contents from reports unless necessary. Share necessary
sensitive material only through the agreed private channel. A complete exploit
or proposed fix is not required to report a credible issue.

## Supported versions and updates

During preparation of v0.1.0, reports against the current `main` branch are welcome.
Once stable releases are available, security fixes focus on the latest stable
release and current development branch. Older releases have no guaranteed
backports; include the affected version even if it is old.

Maintainers assess impact, investigate reproduction and coordinate fixes and
public disclosure with the reporter. Priority considers host code execution,
privilege boundaries, data exposure, unintended target operations and availability.
Response and fix times depend on severity, reproducibility and maintainer capacity;
there is no guaranteed response or remediation deadline.

Confirmed issues and mitigations can be published through
[GitHub Security Advisories](https://github.com/gnix0/the-bugger/security/advisories)
and release notes. Check those locations for published updates. This project does
not claim a dedicated security team, continuous monitoring or a bug bounty.

## Security boundaries

The Bugger is a debugging tool, **not a sandbox for untrusted programs or firmware**.
Launching a native program executes its code with the available user privileges.
GDB, OpenOCD, configuration scripts, debugger extensions and explicitly submitted
Console commands can execute code or affect the host and target. Review tool
paths, scripts, profiles, firmware and connection settings before use. Avoid
running the frontend with unnecessary privileges.

The frontend does not add authentication or encryption to remote GDB connections.
Keep debug servers on loopback or protected networks; use an authenticated,
encrypted tunnel when crossing an untrusted network. Managed OpenOCD binds to
loopback. External servers retain their own configuration and security boundaries.
The [GDB manual](https://sourceware.org/gdb/current/onlinedocs/gdb.html/Server.html)
warns that `gdbserver` itself has no built-in security.

Memory inspection can have hardware side effects. A syntactically simple
expression, pointer value, target memory description or SVD file does not prove
an access is safe. Unknown regions are not authorized for automatic reads.
Explicit inspection still requires deliberate access decisions; some peripheral
registers change state when read. Backend-internal accesses and initialization
scripts are not controlled solely by the frontend's inspection policy.

Logs, profiles and exported memory can contain sensitive paths, expressions,
program output and target data. Inspect them before sharing. Release checksums
check artifact integrity against a trusted checksum source; they are not digital
signatures or a substitute for trusting the distribution channel.

## Relevant protections and report scope

The implementation includes structured GDB/MI handling, bounded parsing/queues,
stale-response guards, literal argument-vector spawning, escaped diagnostic
output, reviewed connection configuration and explicit process ownership.
Normal session setup disables GDB auto-loading. Profiles use private atomic writes;
Save Log creates private files without overwriting existing files. Automatic
value-producing requests require pre-dispatch access authorization. The frontend
adds no implicit firmware programming or reset operation; selected backend scripts
and explicit commands can perform such operations.

These are engineering controls, not a guarantee that every input or dependency is
safe. Reports of failures in these boundaries are welcome, including:

- Memory corruption or resource exhaustion from malformed MI, ELF or configuration
  data, and terminal-control injection through debugger or program output.
- Unintended host command execution, argument injection, unsafe file access or
  disclosure through frontend handling of paths, profiles, logs or tool output.
- Automatic memory/MMIO access bypassing authorization, stale-session confusion,
  unintended target writes/reset/programming, or incorrect process ownership.

The intended effects of a deliberately executed Console command, trusted script
or launched program alone are not a frontend vulnerability. An integration flaw
that causes an unintended operation remains in scope. Ordinary functional defects
without security impact belong in public issues. Report dependency-only defects
to the relevant upstream project; notify this project privately when its
integration is affected or the boundary is unclear.

## Testing and responsible research

Use disposable processes, test firmware and hardware you own or are authorized to
test. Minimize reproduction to the access necessary to demonstrate the issue.
Avoid exposing others' data, disrupting shared systems, writing security eFuses
or testing on operational hardware where faults could cause harm. This policy
does not grant permission to test third-party systems.

The repository provides strict compiler builds, parser/model tests, actual GDB
integration, terminal/tmux tests, ASan/UBSan with leak detection and clang-tidy
analysis. See [testing procedures](docs/testing.md) for commands and prerequisites,
[architecture](docs/architecture.md) for access/ownership invariants, and
[validation records](docs/validation.md) for checks actually performed.
[CI](.github/workflows/ci.yml) defines tests and static analysis for pull requests
and manual dispatch. Automatic push runs are disabled.
This is not an independent security audit, security certification or proof of
complete vulnerability coverage.

Contributors should preserve input bounds, structured MI quoting, shell-free
spawning, private file handling, generation checks and authorization **before**
value-producing requests. Never include credentials or sensitive target data in
commits or test fixtures. Add focused regression coverage for confirmed defects;
security fixes should follow coordinated disclosure rather than exposing exploit
details in a public pull request prematurely.
