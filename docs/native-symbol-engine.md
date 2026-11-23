# Native Symbol Engine

## Status and scope

This is the accepted design and implementation plan for native symbol discovery
and F07. It defines planned contracts, not shipped functionality. The accepted
decision set is **1A / 2A / 3A / 4A / 5A / 6B / 7A**, including cancellable
Console review, centralized command provenance, safe retirement while running
and close-on-exec for received descriptors. Planning approval establishes no
implementation completion, production verification, physical validation or F07
acceptance. SE01 now provides the [independent bounded primitives](architecture.md#symbol-engine-primitives-se01);
SE02 adds the [supervised helper foundation](architecture.md#symbol-engine-helper-se02).
SE03 has [artifact resource components](architecture.md#artifact-admission-accounting-se03-partial)
for admission accounting, private byte copying and complete backing equality,
with supervised storage dispatch, explicit size grants and a frontend ownership
registry for contexts, backing leases and scoped sharing. SE04 adds
[scoped ELF validation](architecture.md#supervised-elf-validation-se04-partial),
and SE05 provides structural DWARF decoding through
[selected text dependencies](architecture.md#selected-text-attributes-se05-partial).
SE06 provides [direct entry facts](architecture.md#direct-dwarf-entry-facts-se06-partial)
with [scoped links](architecture.md#scoped-dwarf-relationship-links-se06-partial)
and [bounded relationship traversal](architecture.md#bounded-dwarf-relationship-traversal-se06-partial),
followed by [inherited attribute owners](architecture.md#inherited-dwarf-attribute-owners-se06-partial),
[full inherited text agreement](architecture.md#inherited-dwarf-text-agreement-se06-partial)
and [declaration-context anchors](architecture.md#declaration-context-anchors-se06-partial)
with [recursive dependency validation](architecture.md#recursive-declaration-context-graph-se06-partial)
and [language/inline constants](architecture.md#language-and-inline-constants-se06-partial).
[Name sequence comparison](architecture.md#name-sequence-comparison-se06-partial)
provides complete byte ordering independently of component boundaries.
The supported [callable grammar](#descriptive-callable-recipes-se06) and
[concrete-inline inventories](#concrete-inline-details-se06) complete the SE06
implementation scope; composed host verification and maintainer acceptance
are recorded in the [SE06 delivery closeout](validation.md#se06-delivery-closeout--2026-11-22).
SE07 begins with [bounded numeric line-program decoding](#line-program-foundation-se07-partial).
Artifact Open workflow, qualified acquisition profiles, complete semantic
interpretation and consumers remain unfinished. Unknown helper operations return
Unsupported.
Verification evidence is recorded separately from integration acceptance.

The engine owns bounded project-native C17 ELF/DWARF decoding, metadata
interpretation, identity and discovery. “Native” describes the implementation,
not a restriction to Linux target artifacts. GDB/MI retains supported live
debugger operations; OpenOCD and other servers retain their responsibilities.
There is no public SDK or stable external ABI.

This document is authoritative for the subsystem's contracts and resource
policies. [Requirements](requirements.md#planned-native-symbol-discovery-f07)
define product obligations; the [roadmap](roadmap.md#native-symbol-engine-foundations)
links feature dependencies; [native subsystems](native-subsystems.md) covers
future research. [Research records](validation.md#native-symbol-engine-research-and-design--2026-11-12)
retain observations and limitations separately from design acceptance.

## Decision ledger

| Classification | Decision or obligation |
|---|---|
| Accepted design | Independent artifact, physical-definition, search, runtime-binding and action layers; no inference of authority between layers. |
| Accepted design | Supervised single helper, protected private filesystem backing, complete-content identity, bounded indexes and immutable frontend publications. |
| Accepted design | Explicit committed native searches, complete reachable ordered matches, exact selection identity and inert consumer transfer. |
| Accepted design | Local scalar interpretation of explicitly authorized object bytes; exact native Source/action integration and separately versioned saved intent. |
| Accepted design — 5A | Retire proven owned guarded DSO installations before arbitrary Console transmission; irreversible GDB-process disqualification. Review itself is cancellable without mutation. |
| Accepted clarification | Shared provenance-aware command admission and final transmission checks; safe confirmed retirement while running or refusal; every received descriptor close-on-exec. |
| Verification obligation | Actual decoder/schema/index/cache sizing, qualified acquisition coherence, backend unload/retirement behavior and consumer/safety acceptance. |
| Initially unsupported | TLS runtime resolution, ambiguous repeated-load targeting, unsupported relocations/storage, extended location evaluation and unproven backend profiles. Metadata may still describe these cases. |
| Deferred capability | ET_REL, compressed ELF/DWARF, split/supplementary DWARF, general ABI demangling, extended80/binary128, memfd and persistent caching. Each needs separate review and acceptance. |

The accepted decision set governs the contracts below as a whole. Numerical
policies appear only in [resource policy](#resource-policy) and [D04](#d04-bounded-mi-rejection-recovery).
Research recommendations do not override these decisions. In particular, the
Console study's original unapproved fallback status remains historical; later
approval selects 5A without claiming its production qualification is complete.

## Layers, ownership and identity

The architecture separates artifact acquisition and retained content; physical
definitions and metadata relationships; search/index/presentation; runtime-image
correspondence and location resolution; and action preparation/authorization/
execution. Establishing a fact in one layer establishes no permission in another.

| Component | Owns | Does not establish |
|---|---|---|
| Artifact backing | Frozen bytes, content fingerprint, storage ownership | Atomic source coherence, runtime correspondence or read permission |
| Artifact-open context | Origin, acquisition evidence, attachments, logical charge, lifetime | Equivalence to another context sharing backing |
| Physical record | Exact content and section/unit/record coordinates | Semantic equivalence from names or addresses |
| Validation evidence | Scoped claims, dependencies, revisions, provenance | Unexamined attributes or unrelated scopes |
| Decoded cache | Reconstructible interpreted metadata | Ownership of published guarantees |
| Search index | Reusable bounded discovery structures | Artifact-wide completeness with incomplete coverage |
| Published snapshot | Immutable frontend-owned accepted results | Runtime eligibility after evidence expires |
| Prepared action facts | Exact selection and action-specific validated requirements | Evaluation, read, write or execution authorization |
| Runtime binding | Selected instance, mapping, session and target evidence | Permission to access an address |

Physical identities distinguish DWARF DIEs, ELF `.symtab` entries and `.dynsym`
entries. Paths, names, signatures, source coordinates, build IDs and apparent
addresses never replace physical identity. Validated declaration, definition,
specification and abstract-origin relationships retain distinct records.
Unresolved and conflicting relationships retain their reasons rather than
silently choosing a canonical definition.

### Internal interface families

| Family | Input and result contract |
|---|---|
| Artifact lifecycle | Explicit Open/Close/Attach/Detach returns an owned context or typed failure; discovery and admission are separate from Search. |
| Validation | Requested scope and dependencies return scoped evidence; no success for unexamined attributes. |
| Search and windows | Committed query and captured inventory return a complete match plan and bounded immutable windows. |
| Details | Exact record returns full names, provenance, relationships or bounded inline-instance details within retained evidence and budgets. |
| Action preparation | Exact Source, Disassembly, breakpoint, Watch or Memory selection returns action-specific facts; no live side effect. |
| Runtime association | Exact artifact location and selected runtime instance return supported mapping evidence or an explicit unavailable reason. |
| Live dispatch | Explicit operation, prepared facts and current authorization are revalidated immediately before transmission. |
| Withdrawal | Evidence revision/invalidation removes dependent capabilities independently of cache reclamation. |

Every asynchronous interface carries request and consumer identity, captured
context revisions and applicable generations. Completion is typed success,
unsupported, cancelled, stale, resource failure, malformed input or operational
failure. An empty successful search is distinct from all these failures.

Prepared facts retain physical record, content and open-context identity,
validation dependencies and the particular action's requirements. Helper restart
alone does not invalidate independently retained valid facts. Relevant evidence
changes make dependent facts unavailable for dispatch. No fact silently binds
to a replacement record, reopened pathname or new runtime instance.

### Atomic publication and failure

1. Reserve all required ownership and accounting, including old/candidate overlap.
2. Validate the requested scope and transitive dependencies.
3. Construct and validate the complete candidate snapshot.
4. Recheck captured revisions and consumer interest.
5. Replace the old snapshot in one frontend publication step.

Cancellation, malformed metadata, allocation/serialization failure and stale
completion publish no unchecked prefix. Previously valid results remain visible
unless their own supporting evidence becomes invalid. Memory reclamation and
evidence invalidation are separate operations: cache eviction cannot revoke a
published guarantee that has independent evidence, and retained storage cannot
keep an invalid guarantee alive.

## Artifact admission and backing

Acquisition and decoding run in the supervised helper. The TUI never acquires
artifacts through a blocking execution path. Initial backing uses protected
private filesystem storage, with qualified full-file reflink where supported.
Memfd and persistent caching are future capabilities.

| Acquisition outcome | Permitted claim |
|---|---|
| Snapshot under a verified coherence contract | Coherent retained acquisition within that contract |
| Structurally validated frozen copy without coherence proof | Exact retained bytes; source point-in-time coherence unproven |
| Detectable mutation, integrity failure, malformed required structure or exhausted budget | Rejected acquisition; no usable publication |

An open descriptor, unchanged timestamps, successful hashing or successful
parsing does not prove atomic source consistency. Lower-confidence retained
bytes may support qualified offline metadata and Source browsing. Stronger
individual relationships may later be established without rewriting acquisition
history. Runtime-sensitive actions require their own sufficient evidence.

Before admission, all supported staging writers and writable mappings must be
relinquished. Transfer only permitted read-only backing descriptors. The parent
validates admission evidence and descriptor properties, explicitly rejecting
`O_PATH`. Descriptor inspection cannot prove all writers closed: this remains
a tested helper ownership invariant. Private storage protection is not a claim
of kernel-enforced immutability against arbitrary same-UID code.

Implement project-owned streaming SHA-256 independently of IPC and artifact
management. Hash complete frozen bytes; an unfinished digest is never published.
Digest and length identify deduplication candidates. Backing is shared only
after complete byte-for-byte equality verification.

### Accounting and lifecycle

Open artifacts stay pinned until explicit close. Acquisition reservations count
concurrently with retained backing. Filesystem capacity and logical admission
capacity are independent. Every independently opened artifact and attached
companion context retains its full logical charge even if backing is physically
shared. Deduplication saves physical storage, not logical admission capacity.

Ownership may be preallocated before its file size is known. An unsized ticket
cannot commit as an empty artifact: the full offered size, including explicit
zero, must be assigned before a grant permits file-data growth. Empty staging
creation does not establish admitted content. Retained storage stays named while
frontend descriptors close; final filesystem deletion runs in the helper.
Operation retirement and process reaping do not prove namespace cleanup.

When final active ownership ends, cached backing receives one full-size cache
charge. Capacity is released only after confirmed cleanup; delayed cleanup stays
accounted. Admission failure preserves existing GDB workflows.

| Event | Required transition |
|---|---|
| Artifact close | Cancel affected work; release ownership through bounded cleanup; retain independently valid historical display metadata. |
| Session disconnect | Revoke runtime bindings and authority; preserve open offline artifacts. |
| Helper restart | Discard unfinished work and disposable helper state; preserve valid frontend snapshots/evidence. |
| Origin-path mutation | Report changed origin; never retarget retained content. Replacement requires explicit reopen. |
| Backing eviction | Reclaim eligible closed backing without destroying independent historical display metadata. |
| Runtime DSO unload | Invalidate instance-dependent evidence/installations independently of artifact lifetime. |

### Debug companions

Discovery is explicit during Open or later attachment. Search never initiates
filesystem discovery, admission, network activity or debuginfod. Support bounded,
documented local GNU debuglink/build-ID locations and configured roots. Validate
relevant relationships, including complete-file CRC32 where applicable.
The initial local candidate policy covers the debuglink basename beside the
primary, its `.debug/` sibling directory and the corresponding directory under
configured debug roots; build-ID candidates use `.build-id/<first-byte-hex>/<remaining-hex>.debug`
under those roots. Validate names and candidate paths before access; no recursive
filesystem crawl or guessed network fallback. Bound candidates, root traversal
and work within admission/scheduling budgets, reporting exhaustion explicitly.

Primary and companion keep independent identities, acquisition evidence, origins
and accounting. A validated relationship never merges their physical records.
If companion admission fails after primary admission, retain the primary and
report that failure precisely. Newly committed DWARF searches include selected
primary and companion inventories by default, with explicit origin narrowing.
Attachment changes the relevant context revision; existing queries never
silently acquire the new inventory.

## Formats and semantic coverage

Initial foundations require linked ELF32/ELF64, independently handled target
byte order and address widths, DWARF 4–5, DWARF32/DWARF64 and mixed-format
artifacts. Decode all applicable standard attribute-form encodings with bounded
structure. Include non-split compilation, partial and type units, including
DWARF 4 `.debug_types`, required cross-unit relationships and explicit missing
or conflicting dependency outcomes. Native line tables **versions 2–5** are
required before F07 acceptance.

Structural decoding does not imply semantic support or location evaluation.
Never guess or skip an unknown encoding whose boundary cannot be determined.
Safely framed unsupported constructs produce coverage limitations; malformed
supported data fails and withdraws dependent facts. Deferred format extensions
remain separate gates in the decision ledger.

Initial F07 searches cover supported function definitions and nonlocal data,
including file statics and supported C++ methods. DWARF is the default provenance;
ELF symbol-table discovery is explicit. Declarations are opt-in. TLS remains
discoverable metadata with unsupported runtime actions. Concrete inline instances
appear through explicit bounded details. There is no Types search category,
general C++ semantic analyzer or unrestricted expression evaluator.

Qualified C++ names and the agreed signature subset come from validated metadata.
Retain raw linkage names and complete/partial signature status. SE06 must document
the supported signature grammar and its partial cases before acceptance; this
plan does not claim a complete C++ type renderer. A descriptive signature or
matching linkage name never proves a GDB expression selects the intended record.


### Descriptive type recipes (SE06, partial)

The initial type-recipe profile is a description of selected metadata. Its grammar
is shown below as notation for the structured recipe, not an expression language
or a promised textual rendering:

```text
type := base(name) | alias(name) | record(name) | enumeration(name)
      | pointer(type) | lvalue_reference(type) | rvalue_reference(type)
      | const(type) | volatile(type) | restrict(type)
```

`name` is a complete validated qualified metadata name. Records preserve their
original class/structure/union tag and physical identity. Named records and
aliases are opaque leaves: their layouts, members and underlying alias types
are outside this description. Modifier order is preserved; no source-language
type normalization or canonical equivalence is inferred.

Complete means complete within this grammar. Arrays, subroutine/member-pointer
shapes, unspecified types and other unsupported constructors have explicit
partial terminals. Missing modifier types remain partial; absence does not
supply a guessed void pointee. Several distinct resolved physical targets remain
explicit alternatives rather than an arbitrarily selected type. Every supporting
graph and reference owner remains available with that partial description.

Recognized non-type targets are malformed rather than unsupported constructors.
Malformed supported facts, dependency failure, unavailable names, exhausted
resources and unresolvable modifier cycles do not expose a successful recipe
prefix. Named recursive records terminate at their opaque name without walking
layout, while modifier-chain cycles fail. Caller-owned capacity and outer-job
deadlines bound construction; production sizing remains SE10 work.

This does not add a Types search category or prove storage size, scalar encoding,
a valid C/C++ declarator, ABI correspondence, object footprint, runtime binding
or access permission. [Callable recipes](#descriptive-callable-recipes-se06)
define parameter reconciliation, implicit objects, method qualifiers, returns
and complete/partial status separately from this type grammar.


### Callable parameter sources (SE06, partial)

The initial source-inventory profile selects an exact physical parameter list
from a completed callable graph and its validated parameter correspondences.
It does not construct a language-level prototype. Each callable's independent
inventory and every relationship remain available; selection neither merges
records nor concatenates lists.

An origin-bearing instance uses the source selected through its abstract-origin
relationship, because concrete children may be omitted. A non-reduced
specification inventory is preferred over a local inventory. An enclosing class,
structure or union with `DW_AT_signature` makes its member inventory potentially
reduced, whether or not the producer retained children. Such a declaration
cannot independently establish the source list in this profile. A non-origin
subprogram may instead use its own inventory when its specification supplies
only a potentially reduced inventory. A concrete-inline record never supplies
its own source list. Distinct preferred physical sources remain alternatives;
equal names, parameter positions or apparent types do not resolve them.

The complete selected direct-child scope is examined. Standard type definitions,
lexical blocks, nested callables, variables, labels, template type/value
parameters and imported module/unit records do not add source formals. Unknown
child constructs, including vendor formal-parameter packs, leave explicit
coverage limitations. Standard tag meanings introduced in DWARF 5 are not
assumed for DWARF 4 extension tags. The original child coordinates remain
available. More than one unspecified-parameters marker, or a formal following
that marker, is a conflicting list; no successful selection view is exposed.

Every formal in a captured inventory that selects another inventory needs an
explicit validated correspondence to that selected source. Missing links remain
partial, including those in intermediate callable records when the concrete
root omitted all children. Source parameters omitted from a concrete instance
remain in the selected source list. No positional correspondence is invented.

READY means this inventory-selection profile completed without a partial reason.
It proves neither an empty C prototype nor source-language applicability,
artificial-parameter handling, implicit-object identity, method qualifiers,
return semantics or complete type descriptions. Those are subsequent callable
recipe obligations. A PARTIAL view retains the selected inventory when unique,
or no selected inventory when unavailable/ambiguous, together with exact
supporting scopes and reasons. Malformed required signature attributes,
conflicting lists, unavailable dependencies, resource failure and invalid
captured contexts do not expose a successful prefix. Physical inventories and
otherwise independent metadata retain their own validity.


### Descriptive callable recipes (SE06)

The initial callable grammar combines a complete qualified metadata name, a
selected physical source inventory, the existing type-recipe grammar and scoped
language/flag/reference evidence:

```text
callable := name return_description ordered_parameters member_description
return_description := type | no_recorded_return_type | partial
parameter := explicit(type) | object(type) | artificial(type) | unspecified
member_description := ordinary | static_member | instance_member(cv, ref) | partial
```

This is structured metadata, not a C/C++ declarator, expression, type-equivalence
proof or ABI description. Every type, parameter, object-pointer owner and source
inventory retains its physical provenance. The initial language profile is the
existing explicit C/C++ language-code set. Unsupported or inconsistent language
coverage remains partial, without interpreting an unchecked parameter prefix.

Return types are resolved from the selected callable's inherited type owners,
so a definition's deduced return type can override a declaration's unspecified
auto type. Complete inherited absence is recorded as `no_recorded_return_type`;
it does not choose between C++ void, constructor or destructor syntax. Missing
formal-parameter types remain partial. Distinct physical type targets remain
alternatives even when their names look equal. Unsupported type constructors or
unavailable type names retain partial type status; malformed metadata, missing
required dependencies and exhausted resources do not become a successful prefix.

Parameters retain source order, including unspecified markers and every
artificial parameter. The object pointer must resolve to an exact formal in the
captured inventories and map through explicit parameter correspondences to the
selected source list. All object-pointer owners must agree on that mapped
formal. Only that formal with validated artificial presence receives the object
role; no spelling or position identifies it. Other artificial arguments remain
visible and make the source description partial. No parameter is removed from
the structured result.

Member classification examines every captured lexical-context derivation.
Supported C++ class/structure/union contexts, an exact object formal and its
artificial evidence establish the instance profile. Complete object-pointer
absence in that context supports the static profile only without unexplained
artificial arguments. Mixed or unsupported contexts remain partial. Object cv
comes from a supported pointer-to-record recipe: qualifiers on the pointer
object do not become method qualifiers, while const/volatile on its pointee do.
Every containing-class derivation must match a physical node in the pointee's
validated relationship graph. Opaque aliases, unsupported pointer shapes,
restrict-qualified pointees and unresolved class correspondence remain partial.
Names do not establish class correspondence, including across type-unit signatures.

Both lvalue- and rvalue-reference qualifier presence is a conflict. DWARF 5
absence can establish an unqualified supported instance method. DWARF 4
qualifier facts are retained, but the instance description remains partial for
that version's extension profile; absence is not a completeness proof. C
prototype completeness requires validated prototyped presence, including for an
empty parameter list. These limits preserve useful discovery without claiming
universal compiler or source-language support.

A completed recipe is immutable borrowed metadata until discard. Wrong child
identity/context preserves the pending request. Failed candidates expose no
successful view; partial results distinguish supported facts, unsupported
coverage and unexamined fields explicitly. No rendering, discovery, target
access, expression selection or frontend publication follows from completion.

### Concrete-inline details (SE06)

An explicit complete supported unit supplies a bounded physical inventory of
`DW_TAG_inlined_subroutine` records, including nested instances, in DIE order.
Each entry retains its exact identity, immediate physical parent and parent tag,
completed relationship graph and direct `DW_AT_call_file`, `DW_AT_call_line`
and `DW_AT_call_column` selections. Distinct instances remain distinct even when
their origins, names or coordinates agree. Declaration coordinates are not
substituted for call-site coordinates, and names never associate instances.

The caller supplies completed graphs rooted at the exact requested instances,
including required cross-unit dependencies. Graph nodes, source views and
abbreviation dependencies remain pinned through discard. Wrong record or root
context preserves the pending request. Missing dependencies, malformed metadata,
conflicting/cyclic relationships and exhausted construction capacity expose no
successful inventory prefix. Diagnostics retain the failed instance and available
child provenance. Absence of a direct origin remains an explicit partial entry;
it cannot be associated with a definition by spelling or proximity.

Direct call-site constants support nonnegative standard constant forms through
the existing bounded decoder, including DWARF 5 abbreviation implicit constants.
Each field distinguishes found, absent and safely framed unsupported values.
Found zero remains zero, and full-width values are never truncated. Duplicate
fields conflict even when values agree. An unsupported coordinate retains its
raw physical selection and makes the inventory partial. Malformed supported
coordinates fail the inventory.

File indexes and zero/sentinel meanings are interpreted only with the relevant
line-table version and dependencies under SE07. This inventory neither resolves
source paths nor establishes executable ranges, standalone entry points, runtime
locations or action authority. Consumers may inspect the retained graph to follow
validated physical relationships; the inventory introduces no semantic identity
merging, artifact-wide index or implicit enumeration of other units.

Caller-owned stationary state and the entire arena capacity require accounting;
every retained graph and source dependency has its own owner. Each step performs
at most 256 bounded tree, graph-node or constant-decoder actions. No allocation,
I/O, recursion or discovery occurs in the core. Outer jobs own revisions,
consumer interests, cancellation and deadlines. A complete borrowed unit
inventory is not frontend publication. Multi-unit search coverage, reusable
indexes, result windows and explicit UI details remain SE09/SE11 work; actual
production capacities remain subject to SE10.

### Line-program foundation (SE07, partial)

The initial line decoder separates the fixed header/opcode table from the
variable directory/file prologue and from numeric program execution. Line-table
versions 2–5, DWARF32/DWARF64 and target byte order are independent dimensions.
The fixed prefix retains exact contribution, variable-prologue and program
boundaries; it does not validate file entries or establish a CU association.
For versions 2–4 the associated CU supplies address width explicitly. Version 5
carries its own width, which must agree with the execution request. Numeric
widths of 1, 2, 4 and 8 bytes are supported; segmented and wider addressing are
explicitly unsupported, with no host-width fallback.

The streaming machine retains addresses, VLIW operation indexes, raw file indexes,
line/column, statement/basic-block/prologue/epilogue flags, ISA, discriminator,
sequence identity and instruction provenance. Its file register starts at 1,
including version 5, as specified in
[DWARF 5 Table 6.4](https://dwarfstd.org/doc/DWARF5.pdf).
The [change to initial file 0](https://dwarfstd.org/issues/210713.1.html) belongs
to DWARF 6; it is not silently applied to older tables. File-table and declaration
index rules require their own version-specific interpretation later in SE07.
Any nonzero `default_is_stmt` byte denotes true.

Checked arithmetic rejects address/line underflow or overflow and decreasing
emitted operation pointers within a sequence. Equal pointers remain distinct
rows, and separate sequences can restart at lower addresses. Sequence-end rows
remain explicit exclusive endpoints; their other source fields are not usable
as ordinary source rows. Line zero remains an unattributed line, not line one.
No instruction-set meaning or executable/function-entry claim follows from
numeric row flags alone.

Legacy `define_file` instructions retain bounded raw name spans and numeric
directory/time/size metadata in a separate event inventory. Events do not invent
file indexes before the initial file table is validated. Version 5's deprecated
`define_file`, unknown framed opcodes and unrepresentable numeric operands remain
explicitly unsupported. Unknown instructions never produce a supposedly complete
matrix after being silently ignored. Malformed framing, invalid supported
operands, unterminated sequences and exhausted row/file capacity expose no
successful view.

Each feed examines at most 256 bytes and stops at the exact contribution end.
Complete views require the whole supplied program to finish. State and full
caller-owned array capacities require accounting, with no production defaults,
allocation, I/O or callbacks in the decoder. Source bytes and all prologue/CU
evidence remain separately pinned by the caller. Discard affects neither caller
arrays nor independent frontend publications. Outer jobs own request identity,
revisions, consumer interests, cancellation and deadlines. Complete prologues,
string/checksum dependencies, CU linkage, source mapping, executable ranges and
Source preparation remain SE07 work; sizing and frontend publication retain
SE10 and SE09/SE11 respectively.

## Committed searches and result windows

Opening Symbols and editing controls perform no discovery. Search/Enter commits
the query; an explicitly committed empty query browses the selected scope.
Initial matching supports exact, prefix and substring modes, case-sensitive by
default, plus explicit deterministic ASCII-insensitive folding. Ordinary,
qualified and linkage names participate where available. Report matched fields,
returning each physical record once.

Apply kind, role, provenance, metadata-origin and source-path filters before
presentation limits. Selected Artifact is the default scope. All Open Artifacts
captures a fixed admitted-context set and inventory revisions; it discovers and
admits nothing. Combined publication is atomic across the captured scope. Failure
in any required scope prevents a new combined publication. Safely identified
unsupported coverage is reported without implying failure or artifact-wide
completeness.

Order independently of locale by qualified/display name, source provenance,
kind and stable physical identity. Define deterministic missing-field ordering
in the accepted schema. Full names and provenance remain authoritative for
matching, ordering and actions. Summaries carry original lengths and explicit
truncation; renderer escaping preserves binary-safe meaning.

Before first publication, prove every matching record can satisfy the bounded
mandatory representation. Schema bounds and metadata examination can establish
this without eagerly constructing every window. An unrepresentable record fails
the query; never silently skip it.

Previous/Next uses stable ordinal intervals and validated cursors. Every
representable match in a successfully completed scope remains reachable.
Reconstruction reproduces the captured scope, complete ordered sequence and
exact established variable window boundaries, including after index eviction or
helper restart. Aggregate pressure cannot shrink a canonical window or change
the sequence; fail the request and preserve the old valid window.

Closing an artifact cancels affected requests and makes the captured query
non-advanceable. Independently valid bounded display metadata can remain
historical without backing. Visibility grants no action authority or guarantee
of recoverable full details. Reopen never reactivates old queries. Malformed
supporting metadata withdraws affected facts; it cannot be kept as trustworthy
historical information.

## Helper, scheduling and IPC

Use one supervised helper through a private early entry point in the existing
executable, before ordinary frontend initialization. The independently testable
core decoder has no IPC dependency. One helper serializes active work with shared
construction and independently owned consumer interests; there is no worker pool.

Each pane owns its draft query, committed query, captured scope, published
window, selection and navigation. Bound consumer interests, underlying jobs,
shared construction and completed results awaiting publication separately.
Saturation returns explicit Busy; cancellation/shutdown retain reserved control
capacity. Use finite interactive-first rounds guaranteeing service for admitted
runnable construction jobs. New arrivals cannot indefinitely postpone older work.

Replacing Search supersedes only that pane's old interest. Shared construction
survives while another eligible consumer needs it. Obsolete completions cannot
publish. The old valid window stays visible, distinguished from its pending
replacement.

Track admission, first service, absolute operation deadline, progress timestamp,
cancellation escalation and reap confirmation separately. Meaningful progress
is bounded advancement of the requested operation. Heartbeats do not reset the
watchdog; progress never indefinitely extends an absolute deadline. Blocked
filesystem operations may violate cooperative targets; parent supervision must
remain responsive independently.

Signal only the verified owned helper. Reap before replacement. If safe reap
cannot be established, leave the engine unavailable instead of accumulating
workers. Worker failure preserves independent publications and GDB. Unfinished
operations fail explicitly; no hidden retry or automatic rescan after restart.

### Framed transport

Use a private UNIX stream socketpair, explicit versioned wire encoding,
length-delimited payloads and bounded buffers. Never send ABI-dependent raw C
structures or pointers. The receive state machine is:

1. Receive and validate exactly the fixed header.
2. Receive exactly the bounded declared payload.
3. Receive exactly one descriptor-trailer byte.
4. Validate marker, descriptor count, roles and properties.
5. Dispatch only the complete validated message.

Use `recvmsg()` at every receive boundary. Never cross header, payload, trailer
or subsequent-frame boundaries. Every received descriptor must be close-on-exec;
use `MSG_CMSG_CLOEXEC` on supported Linux configurations and verify required
properties before use. Reject configurations that cannot safely establish the
required descriptor hygiene.

Handle partial transfers, EINTR, EAGAIN, EOF, overflow, unexpected ancillary data,
`MSG_CTRUNC`, missing/extra descriptors and rejected-frame cleanup. The sender
serializes complete frames without interleaving; partial descriptor-bearing
sends cannot duplicate descriptor transfer. Protocol violation or uncertain
association terminates the helper channel, preserving GDB and independent valid
metadata. Cancellation cannot splice bytes into a partial frame. If bounded
orderly completion is impossible, terminate the channel and supervise cleanup.

## Resource policy

Ceilings are not reservations, fixed allocations or RSS guarantees. MiB/GiB are
binary units. Startup configuration is validated before use; values must be
positive, representable with checked arithmetic and consistent with aggregate
and wire/schema constraints. Invalid configurations fail explicitly, never
silently clamp or disable accounting. No pressure-driven ceiling increases.
Implementation gates must document configuration spelling without inventing
available command-line flags in advance.

### Configurable operational budgets

The following operational ceilings are startup-configurable. This retains the
accepted configurability of per-file and aggregate backing admission limits.
Configuration does not alter the fixed representation constraints below.

| Account | Default ceiling | Enforcement owner and exhaustion behavior |
|---|---:|---|
| Admitted file | 256 MiB/file | Parent admission manager rejects acquisition before exceeding reservation. |
| Aggregate logical admission, including reservations/cache | 1 GiB | Parent manager retains existing contexts; rejects new admission; cleanup remains charged until confirmed. |
| Combined frontend Symbol Engine storage | 128 MiB | Frontend allocator/account owner rejects reservation and preserves old valid publications. |
| Shared validation/dependency evidence | 16 MiB | Frontend evidence owner; contained in aggregate; no new unsupported guarantee on failure. |
| Prepared action facts | 8 MiB | Frontend action owner; contained in aggregate; preparation fails without dispatch. |
| Frontend temporary construction | 16 MiB | Construction owner; contained in aggregate; candidate discarded on failure. |
| IPC storage | 4 MiB/process | Transport owner in each process; frontend portion included in aggregate; fail operation/channel safely. |
| Admitted underlying helper jobs, active included | 64 jobs | Parent scheduler returns Busy at saturation; separate reserved control capacity. |
| Consumer interests, including retained completions | 128 interests | Parent scheduler bounds interests independently; Busy preserves other consumers. |

### Fixed representation constraints

These are fixed maximum bounds, not startup-configurable settings. Operational
budget configuration cannot raise them. Windows may contain fewer rows within
these bounds, but reconstruction must preserve established canonical boundaries;
resource pressure cannot silently shrink a window.

| Representation | Fixed maximum | Enforcement owner and exhaustion behavior |
|---|---:|---|
| Result window | 256 rows | Search/publication owner bounds each window; all matches remain reachable. |
| Available name-field summary | 256 raw bytes/field | Schema/publication owner records original length and truncation. |
| Textual provenance summary | 256 raw bytes/summary | Schema/publication owner records original length and truncation. |
| Frontend-owned window | 1 MiB/window | Publication owner reserves complete candidate; unrepresentable match fails query. |
| Serialized result window | 1 MiB/payload | Encoder/receiver validate before publication; no unchecked prefix. |

### Per-pane operation invariants

These limits remain invariants rather than configurable budgets.

| Interest | Limit | Enforcement owner and replacement behavior |
|---|---:|---|
| Search/window interest | 1 outstanding/pane | Frontend pane owner supersedes old interest without destroying other consumers. |
| Detail/action-preparation interest | 1 outstanding/pane | Frontend pane owner supersedes old interest; stale completions cannot publish. |

### Limits awaiting production measurement

| Account | Sizing boundary | Enforcement and acceptance requirement |
|---|---|---|
| Indexes, decoded caches, construction peaks and supporting count bounds | SE10 measurement gate | Configurable bounded memory-resident structures; select numeric limits using actual decoder/schema costs before dependent production acceptance. |

Supporting counts (interests, shared builds, completed results, control reserve,
companion search candidates) need explicit finite bounds within accepted budgets
at their implementation gates. SE10 must verify that no auxiliary collection
bypasses accounting. The measurement gate is not permission for unbounded
production structures and cannot raise the fixed representation maxima.

Count bookkeeping, retained capacity, owned strings, references and concurrent
old/candidate/temporary storage. Charge shared allocations once. Every allocation
has an accounting owner and checked reservation/release. Helper index/cache and
construction costs require separate measured bounds; frontend limits cannot
stand in for total helper memory. The internal helper configuration currently accepts
an explicit shared decoder-construction allowance, with zero disabling decoder
requests before filesystem access. This supplies a bounded integration path for
verification without selecting a production default or startup setting ahead of
SE10.

| Timing boundary | Default | Enforcement and failure |
|---|---:|---|
| Cooperative service slice | 5 ms target | Helper yields; parent supervises independently. |
| Cancellation checkpoint | At most 64 KiB inspected or 256 bounded steps | Helper checks at whichever threshold is reached first; parent escalates unresponsive cancellation. |
| Helper startup | 3 s | Parent startup timeout and supervised cleanup. |
| Queue wait before first service | 30 s | Parent fails unserved operation explicitly. |
| Ready-index window/details | 5 s | Parent operation deadline; preserve valid old window. |
| Heavy acquisition/index/query/reconstruction | 120 s from first service | Parent absolute deadline, never extended by progress. |
| No meaningful progress | 5 s | Parent watchdog; fail stalled operation and supervise helper as needed. |
| Cancellation grace before TERM | 250 ms | Parent signals verified helper after grace. |
| TERM grace before KILL | 250 ms | Parent escalates, then confirms reap before replacement. |

All timing settings are startup-configurable and parent-enforced with monotonic
time; cooperative targets also require helper checkpoints. Raising a setting
never makes progress heartbeats sufficient or permits replacement before reap.

### Available startup configuration

SE02/SE03 read the following environment settings once during frontend initialization.
Values are positive decimal integers with no sign or whitespace. MiB uses binary
units; time settings use milliseconds and are converted with checked arithmetic.
Invalid settings leave the Symbol Engine unavailable with a diagnostic; ordinary
GDB workflows remain available. Configuration neither starts the helper nor
changes the fixed representation constraints.

| Setting | Policy and supported range |
|---|---|
| `TB_SYMBOL_FILE_MIB` | Per-file logical admission ceiling above; positive checked `uint64_t` MiB, no larger than aggregate admission. |
| `TB_SYMBOL_ADMISSION_MIB` | Aggregate logical admission ceiling above; positive checked `uint64_t` MiB; includes pending reservations and cached backing. |
| `TB_SYMBOL_FRONTEND_MIB` | Combined frontend storage ceiling above; must fit `size_t` and the initial bookkeeping. |
| `TB_SYMBOL_IPC_MIB` | IPC ceiling above; must accommodate worker/transport bookkeeping and not exceed frontend storage. |
| `TB_SYMBOL_JOBS` | Underlying-job ceiling above; 1–64. |
| `TB_SYMBOL_INTERESTS` | Consumer-interest ceiling above; 1–128. |
| `TB_SYMBOL_CHECKPOINT_BYTES` | Work-byte allowance; 64 bytes through the checkpoint maximum above. The minimum accommodates a fixed ELF header/hash block. |
| `TB_SYMBOL_CHECKPOINT_STEPS` | Work-step allowance; 1 through the checkpoint maximum above. The scheduler can supply a smaller allowance to reserve bounded control work. |
| `TB_SYMBOL_SLICE_MS` | Cooperative service slice above. |
| `TB_SYMBOL_STARTUP_MS` | Helper startup deadline above. |
| `TB_SYMBOL_QUEUE_MS` | Queue wait before first service above. |
| `TB_SYMBOL_READY_MS` | Ready-index window/details deadline above. |
| `TB_SYMBOL_HEAVY_MS` | Acquisition/index/query/reconstruction deadline above. |
| `TB_SYMBOL_PROGRESS_MS` | No-meaningful-progress deadline above. |
| `TB_SYMBOL_CANCEL_MS` | Cancellation grace before TERM above. |
| `TB_SYMBOL_TERM_MS` | TERM grace before KILL above. |

These are ceilings/allowances, not reservations. The frontend root also contains
helper IPC, scheduler/job metadata and frontend ownership bookkeeping. The worker
IPC account includes its job/scheduler bookkeeping, request copies and transport
storage. Small protocol-control buffers are owned and charged at channel creation,
so cancellation and failure acknowledgements need no later allocation. Decoder,
index/cache, evidence, prepared-fact and publication budgets receive their own
integration and sizing work; this configuration does not establish their
implementation or acceptance.

## Consumer integration and live actions

### Source, Disassembly and exact locations

Source navigation uses exact native CU/file/line provenance with explicit path
mapping and ambiguity handling. Recorded Source browsing may proceed without
checksum verification when clearly labelled. Verified correspondence concerns
the exact displayed source bytes and expires when those bytes change. Respect
existing limits such as Source path length; never truncate into an actionable
argument.

F07 includes native Source action parity: exact compiled-location breakpoints
and run-to. Multiple compiled locations form a managed set with per-member
status; report and reconcile partial backend mutation. Nearby compiled-line
proposals need explicit confirmation. Ignore counts apply per selected location.

Disassembly requires an explicitly selected supported code location and current
runtime mapping where necessary. Reuse bounded instruction requests. An inline
range is not proof of a standalone entry point; navigation never changes PC.

### Inert symbol-bound intent and location resolution

Introduce discriminated native Watch definitions and Memory drafts retaining
exact identity, context and resolution requirements. Transfer performs no
evaluation, `-var-create`, target read, execution or refresh. Ordinary expression
Watches retain their behavior; the Watch pump must recognize native definitions
and never route them through ordinary expression handling.

Supported pending intent may exist before runtime binding. Initially unsupported
profiles, including TLS and ambiguous repeated-load targeting, stay explicitly
unavailable. Failure preserves exact intent with no bare-name fallback.

Initial data locations are exact standalone `DW_OP_addr`, exact standalone
`DW_OP_addrx` with validated CU-relative address-table dependencies, and supported
ordinary ELF-defined data storage. Never ignore trailing operations.
`DW_OP_constx` is not a location because it yields an address-sized value.
Reject unsupported relocations, special ELF storage, missing dependencies and
ambiguous runtime instances.

Artifact and runtime addresses are separate. Establish appropriate section/
segment mapping, not a universal relocation delta. User-reviewed mappings stay
distinct from backend-verified mappings and participate only in the selected
supported profile with independent artifact correspondence and current instance
evidence. No implicit GDB `file` or `symbol-file` reload.

### Native Watch reads and edits

The selected adapter obtains an explicitly authorized bounded object-byte range
through MI; project-owned code decodes and presents scalars locally. Before
dispatch establish exact location, supported representation, object size,
complete footprint, current context and access authorization. Validate reply
coverage, integrity and generation; retain raw bytes separately. Formatting
never causes debugger requests or pointer dereference.

Initial common scalars include validated integers, booleans, enums,
pointer-object values and IEEE binary32/binary64. Extended80/binary128 need
separate gates. Editing uses explicit Review/Apply and separate explicit byte
verification, preserving mutation interlocks, uncertainty/quarantine and no
automatic retry.

Enums accept names and other representable values; label unnamed values and
distinguish source-language validity from storage representability. Floating
editing uses direct target-precision nearest-even decimal conversion, rejects
finite overflow, supports explicit infinities and a documented canonical typed
NaN, and preserves raw bits of supported exceptional representations. Locale and
host rounding mode cannot silently change results.

### Memory drafts and saved setups

Add a genuine draft-only API. `tb_memory_address()` currently triggers refresh
and cannot serve this transfer contract. Pointer transfer selects pointer-object
storage, never the pointee. Keep pane-local drafts and existing destination
selection patterns. Replacing an existing draft needs explicit review.

Go is a separate explicit operation establishing current resolution and complete
range authorization. Transfer grants no ordinary-memory or MMIO consent.

Version saved native symbol-bound entries separately from ordinary expression/
address entries. Persist inert selection intent and sufficient exact provenance
for explicit reassociation; never restore runtime bindings, live addresses,
sampled values or permissions as authority. Reload causes no implicit discovery,
companion admission, resolution or target access. Missing/changed/ambiguous
artifacts stay visibly unreassociated. Native Source/breakpoint intent needs
compatibility handling; temporary run-to state is not persisted.

## Guarded DSO breakpoints and Console 5A

Separate logical breakpoint intent from backend installations. The installation
ledger records exact frontend ownership, GDB process generation, native intent,
physical location, runtime instance, mapping and backend installation identity.
Only proven owned installations may be retired; unknown/ambiguous ownership
blocks the affected operation. Preserve unrelated breakpoints. Artifact closure
and DSO unload differ. Exact-address unload/reload behavior needs production
backend qualification; ordinary symbolic-breakpoint behavior is insufficient.

### Qualification and review

Process qualification is independent of the Console transaction:

| Process state | Meaning |
|---|---|
| Qualified | Guarded operations eligible subject to current evidence. |
| Suspended | Guard-relevant ownership or mutation uncertainty blocks unsafe operations. |
| Disqualified | Arbitrary Console transmission authorized; irreversible for this GDB process. |

A transaction separately captures exact command, review revision, consent,
retirement work and transmission authorization. Opening review queues/transmits
nothing, preserves qualification and pending work, retires nothing and leaves
unrelated debugging activity available. Always show, even with no installations:

> Sending an arbitrary Console command permanently disables the guarded native DSO breakpoint profile for this GDB process. Restoring eligibility requires an explicitly started fresh frontend-configured GDB session.

Relevant state changes make review stale; refresh before authorization. Declining
without guard-relevant mutation transmits nothing, retires nothing and preserves
qualification/pending work. Decline cannot erase independently arising mutation
or ownership uncertainty.

### Approved transition

1. Revalidate command, process identity, review revision and ordinary Console eligibility.
2. Fence incompatible new installations and revoke incompatible pending/prepared actions.
3. Reconcile dispatched mutations and late completions.
4. Retire only proven owned guarded installations.
5. Confirm the complete required retirement.
6. Irreversibly disqualify the process and invalidate incompatible authorization generations.
7. Only then authorize the captured arbitrary command for transmission.

There is no interval permitting a stale/concurrent completion to install another
guarded breakpoint after retirement. If the reviewed installation set changes
before control is established, refresh review rather than silently enlarging
retirement consent. After transmission authorization, enqueue failure,
cancellation or GDB command error cannot restore qualification.

Failed/uncertain retirement blocks Console transmission and unsafe guarded
operations, preserves exact intent and unrelated breakpoints, and reports known
outcomes/unresolved ownership. Require supported reconciliation or session
replacement. Cancellation after retirement starts is not rollback: never
reinstall or restore pre-mutation qualification merely because review closes.

### Central command provenance and running targets

Enforce policy at shared command admission and final pre-transmission validation.
Distinguish arbitrary user CLI from narrowly defined frontend-owned operations
with typed parameters and validated construction. Text, apparent harmlessness,
aliases and UI origin do not classify arbitrary commands; conversely, not every
`-interpreter-exec console` is arbitrary use.

The current inventory includes Console execution, session monitor/reset/halt/
resume/cleanup, Source substitution rules and explicit bounded MMIO monitor
operations. Each retains its own authorization, context and invalidation
contract; an “internal” label is insufficient. Generic CLI without required
provenance/authorization is rejected. New entry points share the same boundary.
Preserve `--nx` and auto-loading protections before ELF load. Arbitrary Console
authorization disqualifies even before the first guarded installation.

With zero guarded installations, apply ordinary Console eligibility and
irreversible process disqualification. With installations, require safe confirmed
retirement under the current backend profile. If it cannot be established while
running, refuse transmission, require an appropriate stopped state and explicit
retry. Never implicitly interrupt, resume, reconnect or restart for retirement.
Only an explicitly started fresh frontend-configured GDB process can requalify;
reconnect, reset, symbol reload and configuration checks cannot.

## D04: bounded MI rejection recovery

Keep normal parser limits: **1 MiB record, 16,384 values, depth 64**. For explicitly
eligible oversized correlated results, add a separate non-tree validator:

| Recovery boundary | Limit |
|---|---:|
| Complete record, prefix included | 8 MiB |
| Syntactic values | 1,048,576 |
| Deadline from record's first byte | 5 s |
| Retired-correlation entries | 128 |
| Ordinary retention | 60 s |

The parser/controller enforce these limits independently of Symbol Engine
budgets. Pin active rejection proof within the history capacity. Nonfatal
rejection requires complete grammar, framing and trustworthy correlation; emit
one command-specific failure, never success or partial data. Malformed syntax,
excessive depth, allocation failure, uncertain framing/correlation or exhausted
recovery limits remain fatal. History exhaustion cannot evict active proof;
retired tokens cannot identify a newer request. Test arbitrary chunking, EOF,
partial records, exact/one-over boundaries and subsequent valid records.

Eligibility is explicit per command result. This is no general resynchronization
permission and does not weaken malformed asynchronous-record handling. No live
integration acceptance precedes D04 acceptance.

## Implementation gates and dependencies

Foundation labels are independent of F IDs. Each implementation gate can use
several coherent commits with related tests/docs; review and acceptance remain
separate from implementation. This planning delivery contains documentation only.

| Gate | Objective and integration | Verification and failure boundary |
|---|---|---|
| D04 | `src/gdb/mi.c`, controller and headers: typed bounded rejection and correlation history. | Parser/allocation/controller cases; unsafe recovery still shuts down. |
| SE01 — bounded primitives | Independent checked readers, physical coordinates and streaming SHA-256. | Published vectors, fragmented input, endian/width, overflow, injection; failed hash yields no identity. |
| SE02 — helper boundary | Private early `src/main.c` entry, supervisor, scheduler and event-loop transport. | Fragmentation, FD hygiene, generations, fairness, saturation, stalls, death/reap; GDB independent. |
| SE03 — artifact ownership | Admission/backing manager over SE01–02. | Coherence grades, mutation, reservations, charges, deduplication, cleanup/cache transitions; preserve existing workflows. |
| SE04 — ELF foundation | Native sections/symbol decoder; preserve existing `src/model/images.c` libelf consumers. | Class/endian/special storage/malformed fixtures; no unsupported storage/code claim. |
| SE05 — DWARF structure | Units, abbreviations, forms and references over retained backing. | DWARF32/64, required forms, partial/type units, bounds, dependencies; no partial catalog. |
| SE06 — definitions/names | Roles, relationships, qualified names, signature subset, inline metadata. | C/C++ duplicates, cycles/conflicts, incomplete signatures, inline instances; preserve distinctions. |
| SE07 — source/ranges | Native line matrices and executable ranges; `src/model/code.c` contract. | Versions 2–5, duplicate CUs/basenames, headers, substitutions, checksums, disjoint ranges; explicit ambiguity. |
| SE08 — companions | Explicit local discovery/attachment and relationship ownership. | Debuglink/build-ID, missing/mismatched/malformed candidates, overlapping inventory, charges; primary survives failure. |
| SE09 — discovery/publication | Indexes, captured queries, match plans, evidence and immutable snapshots. | Filters before limits, reachability, representability, atomic combined scope, exact reconstruction, allocation failure. |
| SE10 — production sizing | Actual decoder/index/cache/evidence/schema costs under accepted budgets. | Native/embedded/pathological corpora, peaks, reconstruction/contention; fix outstanding limits before dependent acceptance. |
| SE11 — offline UI | Artifact browser, functional Symbols, bounded details, independent Source preview through TUI/layout. | 80×20, resize, keyboard/mouse/Vim/custom bindings, multiple panes, demo/historical states; editing/opening causes no discovery. |
| SE12 — runtime preparation | Facts, supported binding profiles, direct locations, final guards through session/image/context models. | Identity, mapping conflicts, addr/addrx/constx, stale dependencies/repeated instances; no fallback/reload. |
| SE13 — native Watches | Discriminated definitions, native bytes/scalars/editing in variables integration. | Inert transfer, authorized exact reads, scalar vectors, enum/float edits, explicit verification; ordinary Watch regression. |
| SE14 — Memory drafts | Draft-only transfer through `src/model/memory.c` and destination selection. | Zero evaluation/refresh/read, pointer storage, replacement, running/authorization checks. |
| SE15 — Source/actions/5A | Exact Source actions, managed breakpoint sets/run-to, DSO ledger, shared Console provenance. | State machine, ownership, running retirement, stale installs, alternate Console paths, unload/reload; unsupported profiles unavailable. |
| SE16 — setup compatibility | Versioned inert native intent in profile/setup capture/restore. | Explicit reassociation, missing/changed artifacts, schema/partial-save diagnostics and ordinary compatibility. |
| F07 acceptance | Accepted foundations integrated in six-root tiled workspace. | Complete consumer/safety matrix; foundations alone never complete F07. |

Dependencies follow capability need: SE03 uses SE01/02; SE04 uses admitted
backing; SE05 uses ELF sections/readers; SE06/07 use structural DWARF; SE08 uses
admission and validated primary relationships. SE09 consumes supported inventories
and evidence without initiating SE08 discovery. SE10 measures actual SE03–09
structures and blocks their dependent production acceptance, while allowing
implementation/measurement to proceed incrementally. SE11 consumes SE07/09/10;
SE12 consumes exact metadata plus accepted session/mapping evidence. SE13/14
require SE12; SE15 also requires SE07, D04 and backend qualification. SE16 follows
the accepted consumer-intent schemas. F07 requires all applicable gates above,
including native line and Source action parity.

Offline artifact inspection can be accepted before live resolution. Companion
work never implies automatic indexing. Deferred compatibility gates do not block
F07 outside the agreed envelope. F08 remains an independent globals/statics
consumer, not completion implied by F07. Classify consumer impacts as coupling
regression, semantic refinement, capability gap, genuine behavioral regression
or unsupported case; never weaken exact identities to fit inadequate consumers.

## Acceptance matrix

These are required future observations, not results of the planning delivery.

| Area | Required observations |
|---|---|
| Identity | Duplicate names, paths, DIEs, ELF tables and runtime instances never merge/retarget silently. |
| Validation | Unsupported coverage, malformed data, unexamined scopes and unresolved dependencies remain distinct. |
| Publication | Failed/cancelled/stale candidates preserve valid windows; invalid supporting facts withdraw. |
| Reconstruction | Captured scope, ordered identities and variable boundaries survive eviction/restart exactly. |
| Resources | Exact/one-over per-file, aggregate, subsidiary and replacement peaks; arithmetic and all retained storage accounted. |
| Concurrency | Maximum 32-pane workspace, independent state, shared construction, detach/replacement storms and Busy saturation. |
| Scheduling | Sustained interactive work cannot starve admitted construction; observable bounded cancellation/deadlines. |
| Process | Startup failure, blocked acquisition, crash, TERM/KILL, delayed reap; no replacement before reap. |
| IPC | Fragmented/adjacent descriptor frames, missing/extra FDs, truncation, close-on-exec, complete cleanup. |
| D04 | Eligible oversized valid correlated results recover only within envelope; unsafe cases close connection. |
| Actions | Recheck identity/evidence/context/authorization; restart-surviving facts remain dispatchable only while valid. |
| Console 5A | Decline, zero-installation warning, stale review, retirement failure, alternate paths, late installs, irreversible disqualification. |
| Running target | Proven safe retirement or refusal; no implicit interrupt/resume. |
| Scalar edits | Exact storage, unnamed enums, float rounding/exceptional representations and independent explicit verification. |
| Persistence | Restored intent inert; no discovery, binding, evaluation or authority restoration. |

### No unsolicited access

Instrument outgoing MI for opening Symbols, control edits, Search/filtering,
window navigation, selection/details, transfer, cancellation and reopen. These
paths must issue zero unsolicited `-var-create`, `-var-update`, child expansion,
runtime expression evaluation, target-byte reads, Memory Go/Refresh, writes or
inferior execution commands. Source browsing is metadata/file access. Explicit
Disassembly reads code only under its supported contract; native Watch Read,
Memory Go and edits have separate authorization paths.

Extend existing parser/controller, code, variables, memory, breakpoint, profile
and TUI harnesses in their separately authorized implementation gates. Use
[testing procedures](testing.md#planned-native-symbol-engine-verification) and the
fresh fortified baseline for implementation/test/build changes. Production
measurement must include actual serialization/schema ownership, decoder/index
allocations, cold/warm latency, cancellation, acquisition contention,
reconstruction and UI responsiveness. Earlier prototypes do not establish these.

### Manual F07 acceptance

1. Open a fixture with duplicate file statics and duplicate basenames.
2. Commit searches/filters and distinguish exact records and provenance.
3. Traverse all windows, including identical summaries and over 256 matches.
4. Open the selected definition and concrete supported code location in Source/Disassembly.
5. Transfer a supported variable to Watches without reading, then explicitly Read.
6. Transfer to Memory without evaluation/refresh; review the draft, then explicitly Go.
7. Exercise missing Source, ELF-only results, incomplete DWARF, unsupported locations and exhausted budgets.
8. Replace queries, close artifacts and restart the helper; check retained displays and exact reconstruction rules.
9. Exercise Console decline/approval, retirement failure and running refusal, preserving unrelated breakpoints.
10. Save/reload inert intent and explicitly reassociate it.
11. Repeat compact-terminal, mouse, keyboard, Vim, custom-binding and tmux workflows.

Remote MI/RSP envelopes need separate supported-profile evidence and prove no
physical bus width, transaction count or MMIO safety. Other GDB versions,
multithreaded loaders, remote configurations and physical STM32/ARM,
ESP32/Xtensa and ESP32/RISC-V boundaries remain unverified until their acceptance
work. Every release retains the existing physical/native regression matrix.
