# MI parser corpus

These are synthetic protocol examples authored for M03, following the official
GDB manual's [MI output grammar](https://sourceware.org/gdb/current/onlinedocs/gdb.html/GDB_002fMI-Output-Syntax.html)
and [stream record specification](https://sourceware.org/gdb/current/onlinedocs/gdb.html/GDB_002fMI-Stream-Records.html).
They are not captured debugger sessions, hardware evidence or working transport
fixtures. Addresses and values are deliberately illustrative.

- `valid.mi`: 22 records covering the prompt, all seven output prefixes,
  result classes including unknown future classes, token preservation,
  nested tuples, both list forms, duplicate fields, empty containers, byte
  escapes, raw/quoted streams, source paths with spaces and generic register names.
- `invalid.mi`: six invalid records (unterminated string, mixed list,
  unknown escape, tokenized stream, unquoted value and unknown prefix), followed
  by two valid records demonstrating line-boundary recovery.

Run `make test-mi` for semantic assertions, exhaustive two-chunk splits,
chunk sizes 1–71, CR/LF framing, generated resource-limit cases and byte mutations.
The companion allocation suite walks malloc/calloc/realloc failures on nested
and stream records. Both are C programs and run in the sanitizer suite.

Run `make mi-demo` to decode the valid file one byte at a time. Expect exit 0.
Run `./build/mi-dump --chunk 1 tests/fixtures/mi/invalid.mi` to inspect diagnostics
and recovery. Expect six errors, recovered records 7–8 and exit 1.
The decoder escapes control bytes and launches no debugger.
