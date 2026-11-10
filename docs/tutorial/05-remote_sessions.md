# 05 — Existing GDB server sessions

[Guide index](README.md) · Previous: [OpenOCD](04-openocd_sessions.md) · Next: [Execution control](06-execution_control.md)

Use **Connect to GDB server** when a server is already running and you want the frontend to connect through a compatible GDB. The server might be `gdbserver` on a Linux host or a target-specific embedded server. The frontend owns its GDB process; it does not own or administer the existing server.

**On this page**

- [Fill in a connection](#fill-in-a-connection)
- [Remote versus extended-remote](#remote-versus-extended-remote)
- [A local server exercise](#a-local-server-exercise)
- [Inspect a remote target conservatively](#inspect-a-remote-target-conservatively)
- [Close and recover](#close-and-recover)

## Fill in a connection

| Field | Meaning |
|---|---|
| GDB endpoint | TCP `host:port`, such as `127.0.0.1:2345`; IPv6 uses `[address]:port`. |
| Symbols (ELF) | Optional local ELF matching the remote program/firmware. |
| GDB binary | Local executable supporting that target; blank uses `gdb`. |
| Mode | Connect to existing server. |

Use **Profile... → Remote protocol** to choose `remote` or `extended-remote` before connection. `remote` is the default. The endpoint field is not a shell command and does not accept a serial-device or pipe transport in this workflow.

Choose **Review session**, inspect the floating dialog, then **Connect**. **Back** or Escape returns to editing without connecting. Connecting can halt the target according to the server's behavior. Loading symbols does not flash firmware, reset a board or launch a local program.

## Remote versus extended-remote

An ordinary remote connection works with the target the server exposes. Extended-remote can keep a connection open without a current inferior. In that case the workspace says **Connected** rather than presenting a fabricated stopped process. Deliberate GDB Console operations may be needed to attach or launch according to that server's capabilities. Continue does not invent a local process to launch.

A remote target identifier is opaque. The **LIVE REMOTE** header does not turn it into a host Linux PID, even if the identifier looks numeric. Use the target/server's actual semantics when selecting a process.

## A local server exercise

This exercise connects to a separately started `gdbserver`, then attaches to a small repository example program. You need native GDB, `gdbserver` and permission to trace local processes.

```sh
make build/remote-fixture
./build/remote-fixture &
printf 'Fixture PID: %s\n' "$!"
```

Keep the fixture running and note the PID printed by the shell. The fixture itself prints `REMOTE fixture ready`; it does not print its PID. In another terminal, start a server on a free local port:

```sh
gdbserver --multi 127.0.0.1:2345
```

In The Bugger, choose **Connect to GDB server** with endpoint `127.0.0.1:2345`, symbols `./build/remote-fixture`, GDB `gdb`, and **Remote protocol: extended-remote**. Review and Connect. In Console, explicitly attach to the fixture using its current PID:

```text
attach PID
```

Replace `PID` with the actual number. Wait for Stopped, expand Memory, enter `&remote_buffer` and choose Go. A successful sample begins with `REMOTE bounded memory`. The server and fixture remain separately started processes; closing the frontend connection does not terminate the server. Stop them in their own terminals after the exercise.

If permissions prevent attach, investigate host tracing policy rather than treating the connection as a successful debugging session. Use the [native fixture](03-native_sessions.md#attach-to-a-process) for a simpler first attachment exercise.

## Inspect a remote target conservatively

The same Source, Disassembly, Call Stack, Threads, Registers, Memory and variable controls are available when their target capabilities and stopped context permit them. Missing symbols produce assembly-only locations or unavailable types; they do not make the connection itself a preview.

Memory starts without a default address. Enter a known location and **Go** to read the displayed byte count. Scrolling can read beyond loaded bytes only in regions identified as ordinary readable memory. If the region is unknown, use **Options → Previous range (read)** or **Next range (read)** for deliberate adjacent requests, or enter another exact address. Unknown is not automatically dangerous, but it is not permission for speculative reading either.

The target's address units, address width and byte order must support the operation. Unknown byte order permits byte display but prevents multi-byte interpretation; no host byte order is assumed. Unsupported or unknown address units/width can block byte reads. The interface has no general manual endian selector in Memory; do not assume one from the grouping button.

Remote ELF/load-address confirmation helps [code browsing](07-source_and_disassembly.md#review-a-remote-elf-mapping). It does not prove that Memory is writable, authorize MMIO, or establish chip identity. Remote variable/RAM editing needs appropriate writable-region evidence; the supported user-confirmed STM32 path is described in [Editing values](14-editing_values.md#remote-stm32f446re-ram-evidence).

## Close and recover

**Close Session** confirms disconnection. A running target is interrupted first; failure to establish stopping is reported after bounded recovery. The external server controls the eventual target disposition. The frontend never terminates that server to force cleanup.

On refusal, check the address, port, server log and selected protocol. On a lost connection, values cease to be current; inspect the actual server and target before reconnecting. Correct retained form entries and start another reviewed connection. A new session does not resume an uncertain mutation automatically or restore previous runtime samples.
