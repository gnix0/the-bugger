# Tests

Run `make test` from the repository root. Python 3's standard library runs
the built executable as a subprocess; no Python packages are needed.

M00's seven integration tests verify version/help output, honest bare-invocation
status, invalid arguments and exit codes, literal handling of hostile arguments,
operation without TERM/from another directory, and unchanged PTY attributes.
Each child has a five-second timeout. Coverage is foundational evidence for
NFR-01/03/08/12, not completion of the release-wide requirements.

Run `make sanitize` for the same tests against an ASan/UBSan executable in
`build/sanitize/`. Run `make analyze` for clang-tidy analysis. See
`docs/testing.md` for compiler checks and manual acceptance.

There are no debugger fixtures yet. Native C/C++ fixtures and their expected
states arrive in M04; saved MI records and the explicitly fake emitter arrive
in M03. Hardware tests will require real hardware evidence.
