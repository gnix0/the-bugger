# Native subsystem roadmap

## Purpose and current boundary

Project-owned metadata and analysis can give embedded debugging workflows precise
identities, explicit evidence and reusable offline capabilities without moving
live-debugger responsibilities out of GDB/MI. The first concrete subsystem is
the [Native Symbol Engine](native-symbol-engine.md), supporting F07 and later
consumers. Its design is accepted; implementation and acceptance remain planned.
This initiative introduces no runtime dependency, prototype or public SDK.

The Symbol Engine separates retained artifacts, physical records, discovery,
runtime correspondence and authorized actions. Its real Source, Disassembly,
Watch, Memory, breakpoint and setup consumers will test whether those boundaries
hold. Independently accepted foundations do not complete F07, F08 or another
consumer gate. Existing libelf consumers and GDB/server responsibilities remain
in place until an explicitly reviewed change replaces a particular responsibility.

The [feature roadmap](roadmap.md#native-symbol-engine-foundations) retains F IDs
and scheduling. The Symbol Engine's [implementation gates](native-symbol-engine.md#implementation-gates-and-dependencies),
[acceptance matrix](native-symbol-engine.md#acceptance-matrix) and
[research record](validation.md#native-symbol-engine-research-and-design--2026-11-12)
are authoritative for that subsystem. Future candidates below are research/design
tasks, not selected architectures or promises of implementation.

## Future candidates

Each candidate has separately reviewable research, design, implementation,
verification and acceptance work. Scheduling follows product need and evidence;
none is an implicit prerequisite for F07. No library, representation, interface
or detailed acceptance criteria are selected here.

| Candidate and motivation | Related features | Independent future tasks |
|---|---|---|
| Hardware-aware memory analysis: explain relevant address-space and device constraints without treating descriptions as access consent. | Memory/F04, F20/F21, F34/F35 | Research authoritative memory/device information and uncertainty; review bounded scope and access policy; implement the approved slice; verify descriptions and failure behavior; accept on named supported configurations. |
| Instruction interpretation and offline code analysis: improve code understanding where available bytes and architecture evidence suffice. | Source/Disassembly, F28, F37–F39 | Research required architectures and useful workflows; review limits and debugger interaction; implement a selected capability; verify against appropriate reference material/corpora; accept supported profiles without implying trace or coverage acquisition. |
| Register/SoC descriptions: explain discovered registers and device fields with provenance and raw fallback. | F11, F34, F35 | Research description sources, compatibility and licensing; review matching and interpretation scope; implement the approved subset; verify mismatches/unknown fields; accept with named core/device evidence. |
| Safe object-footprint planning: improve exact storage reasoning for deliberate value operations. | Watches/F03, Memory/F04, F08/F09/F30 | Research types, locations and safe-footprint gaps; review authorization boundaries; implement selected cases; verify completeness/unsupported cases; accept without granting permission from metadata alone. |
| Stack/frame reconstruction: explain recoverable frame information and explicit uncertainty. | Call Stack, F02, F31/F35 | Research ABI/unwind/fault-context evidence; review scope and ownership alongside GDB; implement accepted cases; verify incomplete/corrupt inputs; accept supported configurations without guessed fault context. |
| Snapshot/postmortem analysis, where justified: permit useful offline diagnosis from adequately described captured state. | F21, F33, F35 | Research demand, capture provenance and missing-state limits; review a concrete workflow; implement only the justified scope; verify incomplete/inconsistent captures; accept separately from live-debugging claims. |
| Focused MI/controller reliability: address concrete protocol and lifecycle limitations as consumers expose them. | D04, shared debugger operations | Research reproducible failures; review bounded recovery/correlation rules; implement focused corrections; verify malformed/stale/resource paths and regressions; accept without general resynchronization assumptions. |

## Consumer review and progression

Classify impacts before changing a consumer: coupling regression, semantic
refinement, capability gap, genuine behavioral regression or unsupported case.
More precise identity is not weakened to preserve an inadequate consumer
representation. A capability gap needs an independently reviewed consumer change;
a genuine regression requires correction. Unsupported cases remain visible.

Research produces durable observations, commands/configurations and limitations.
Design review selects a bounded contract and dependencies. Implementation uses
small coherent steps with relevant verification and documentation. Acceptance
requires actual consumer behavior and the appropriate supported-target evidence.
Neither prototype measurements nor design approval establish production or
physical validation. The [maintenance process](maintaining.md#milestone-lifecycle)
and existing release matrix apply to each accepted implementation gate.
