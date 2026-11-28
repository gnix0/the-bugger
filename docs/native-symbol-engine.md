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
SE07 includes [line/file decoding](#line-program-foundation-se07-partial),
[code-range foundations](#code-range-foundation-se07-partial) and
[direct code attributes](#direct-code-attributes-se07-partial), with
[indexed-table association](#indexed-table-association-se07-partial) and
[code-location composition](#code-location-composition-se07-partial), plus
[source-file provenance and paths](#source-file-provenance-and-paths-se07-partial) and
[composed Source preparation and byte correspondence](#source-preparation-and-byte-correspondence-se07).
SE07 local verification and maintainer acceptance are recorded in the
[delivery closeout](validation.md#se07-delivery-closeout--2026-11-23).
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

### Companion metadata foundation (SE08, partial)

`crc32` implements the reflected IEEE CRC-32 profile used by GNU debuglink,
independently of artifact management and IPC. It hashes complete retained file
bytes in updates of at most 64 KiB. Finalization requires the exact expected
extent and occurs once; invalid input or length failure poisons the stream and
cannot publish a prefix checksum. This is neither RSP `qCRC` nor a security or
artifact-identity primitive. The format follows the
[GNU separate-debug-file specification](https://sourceware.org/gdb/current/onlinedocs/gdb.html/Separate-Debug-Files.html).

`companion_metadata` interprets a selected complete `.gnu_debuglink` section and
selected ELF note sections. Each request retains backing and physical section
coordinates, target byte order and exact scope extent. The caller establishes
section roles, uncompressed framing and content leases; these pure components
perform no discovery, allocation, file access or helper dispatch. State and full
name/ID buffer capacities belong to the caller's existing accounts. No production
capacity defaults or new runtime dependency are introduced.

Debuglink decoding retains the full basename and the four-byte checksum's exact
section offset. Padding is opaque, and the checksum uses target byte order.
Empty names, missing terminators, wrong extent and trailing bytes are malformed.
Well-framed names outside the initial usable POSIX-basename profile—dot/dot-dot,
slash, CR or LF—remain explicitly unsupported after complete framing. They are
never shortened into another name or treated as missing companions. Buffer
exhaustion reports resource failure with no usable prefix.

Build-ID decoding frames the entire selected note section in slices of at most
256 bytes. An explicitly established 4- or 8-byte note-alignment profile is
required; ELF class alone does not choose GNU note layout. Headers and alignment
arithmetic are checked against the selected extent, with null-terminated owner
names and opaque descriptor/padding bytes. Exact owner `GNU\0` and type 3 select
a nonempty opaque ID. No SHA-1/UUID length or algorithm is inferred. Other framed
note types are skipped; unsupported alignment is never guessed. See
[ELF note framing](https://gabi.xinuos.com/elf/08-pheader.html#note-sections).

All notes must finish before a result is available. Duplicate GNU IDs conflict
even when their bytes agree; a malformed later note withdraws the candidate.
Absence covers only the examined section, not unexamined sections, program
headers or the whole artifact. Later inventory/relationship construction must
retain those scopes independently and reconcile all selected occurrences.
Neither matching IDs nor CRCs merge physical records, validate unrelated DWARF,
establish source/runtime correspondence or authorize any live action.

The next foundation composes these selected metadata decoders into bounded
[local candidate and retained-inventory operations](#companion-discovery-and-relationship-foundation-se08-partial).
Neither foundation activates companion loading in the existing frontend or changes
Search behavior.

### Companion discovery and relationship foundation (SE08, partial)

`companion_paths` produces explicit local candidates from a captured absolute
primary origin, a complete debuglink basename, opaque build-ID bytes and an
ordered list of absolute debug roots. It validates all path inputs before yielding
the first candidate. The order is beside the primary, its `.debug/` directory,
mirrored primary directories under each root, then each root's build-ID location.
Build-ID hex is lowercase, with the first byte forming the directory. No build-ID
algorithm or fixed digest length is assumed; a one-byte ID produces an empty
hex remainder followed by `.debug`.

Candidate ordinals, route and root index remain distinct even for duplicate
paths. The iterator preserves internal separators, dot components, symlink
spelling, spaces and non-ASCII bytes; it joins boundary separators without
canonicalizing paths. Roots select candidate locations, not a filesystem sandbox:
normal OS path resolution may follow symlinks or dot components. There is no
recursive scan, environment/tilde expansion, implicit working-directory anchor,
network fallback or debuginfod request. Resource failure never yields a truncated
path. Earlier complete candidates do not establish completion after a later
failure, and no candidate proves that a file exists or is an eligible companion.

The current component/schema bounds are fixed upper representations, not
startup defaults or preallocated storage:

| Representation | Bound | Enforcement and failure |
|---|---:|---|
| Each input/output POSIX path, including terminator | 4,096 bytes | Path iterator; resource failure without an actionable prefix |
| Complete opaque build ID | 4,096 bytes | Scanner, receipt codec and path request; resource failure/invalid receipt |
| Configured roots per captured request | 32 | Path iterator admission; resource failure |
| Candidate ordinals | At most 66 | Derived from two beside-primary routes and two routes per root |
| Inventory receipt | 96-byte prefix plus complete name and ID, at most 8,287 bytes | Encoder/decoder require exact framing; failure publishes nothing |

Caller capacities can be smaller and fail explicitly. Hex expansion of a valid
ID can exceed the path bound; it is never shortened or silently skipped. The
existing configurable helper decoder, IPC, construction and admission accounts
remain authoritative budgets. Decoder requests require a positive explicit
allowance; the production decoder/index/cache defaults still await SE10.

Each generated path is submitted explicitly through the existing supervised
`ACQUIRE` operation. Its full logical reservation precedes growth, independently
of the primary's retained charge. Missing, inaccessible, unsupported, malformed or
exhausted candidates are separate reported failures; none closes the primary or
implicitly tries another origin. The attachment coordinator owns each explicitly
selected attempt; selecting another candidate is a separate caller decision. The path iterator itself performs
no I/O, admission or helper scheduling.

`companion_scan` builds a complete native section directory over pinned frozen
backing, examines every `SHT_NOTE` section, recognizes `.gnu_debuglink` through its
validated full section name, and computes the complete retained file's CRC32.
The protected reader checks exact backing identity/extent and descriptor
properties. It retains the existing acquisition ownership invariant rather than
claiming descriptor checks prove absence of writers or source coherence.
Construction, all reader windows, section arrays, metadata buffers and candidate
receipts are charged before ownership is acquired. Cancellation destroys private
construction without deleting backing or publishing a partial inventory.

Duplicate debuglink sections or GNU IDs conflict even when equal, including IDs
in separate note sections. Late malformed metadata, unsupported note alignment,
compressed notes, read failure or exhausted capacity prevents a receipt. Notes
use the established 4/8-byte alignment profiles independently of ELF class.
Absent/compressed section-name coverage leaves debuglink presence unknown.
Build-ID presence/absence covers the complete section inventory, not unexamined
program-header-only notes. Missing required evidence remains unavailable; it is
not supplied by guessed section names or an unrelated digest.

The private `COMPANION_INSPECT` helper operation accepts exact namespace/slot and
backing keys, extent and explicit basename/ID capacities. It opens only protected
retained storage; a disabled decoder allowance rejects before filesystem access.
Its versioned receipt uses explicit big-endian fields and complete raw name/ID
bytes. Frontend decoding validates exact lengths, encodings and consistent
presence/coordinates before borrowing spans. Request identity, consumer/context
revisions, process generation and retirement remain the enclosing caller's
responsibility; a valid frame alone does not validate its metadata claim.

Result serialization reserves temporary IPC storage independently of the decoder
receipt and wire-owned copy, including their concurrent peak. Allocation or send
failure publishes an explicit failure through the existing bounded control path.
It does not turn an oversized result into an empty success or stall forever.
Independent valid receipts and the GDB session remain unaffected.

`companion_relation` compares validated inventories without I/O or mutation. The
initial linked-ELF profile requires equal class, byte order, machine, object type,
ELF flags, OS ABI and ABI version. A debuglink relationship requires the primary's
complete link metadata and a matching complete candidate CRC. A build-ID
relationship requires both nonempty IDs to agree. If both IDs are present, a
conflict rejects either route even when the CRC matches. Missing ID evidence
remains acceptable for the legacy CRC route; that does not assert an ID match.
Header mismatch, unsupported profile, missing evidence and CRC/ID mismatch remain
separate outcomes. No program-interpreter, unrelated DWARF, source or runtime
validity follows from this metadata relationship.

Receipts retain independent physical backing/section/note coordinates. Matching
metadata cannot merge records or bypass full logical admission charges. The
[attachment owner](#companion-attachment-and-lifetime-foundation-se08) composes
these scoped receipts into independently retained relationship evidence. The
existing frontend performs no automatic companion discovery or target access.

### Companion attachment and lifetime foundation (SE08)

`companion` owns frontend inspection tickets, immutable metadata facts,
relationships and direct inventory captures. `companion_load` coordinates one
explicitly selected candidate through acquisition, inspection, relationship
validation and publication. These are internal components driven by the caller;
they introduce no public SDK, automatic loading, Symbols UI or Search request.
SE09 consumes inventory captures; SE11 supplies the explicit user workflow.

The initial profile selects one GNU companion per primary context. An installed
relationship cannot be silently replaced: detach it explicitly before selecting
another. Other primaries and independently opened artifacts remain independent.
Inventory is direct, never recursive. Neither an equal pathname nor a matching
CRC/build ID merges physical definitions, contexts or backing.

Before dispatching inspection, the owner reserves a fact slot and pins exact
retained content. Its plan captures the complete work identity, helper process
generation, context/inventory revision, content revision and actual backing
slot/key/extent. Proven backing sharing can make the slot differ from the open
context identity. An origin copy survives context closure for cleanup/evidence
ownership; it is never reopened to reconstruct a fact.

A complete validated helper result and matching worker-release or reap proof are
both required before finishing a ticket. Confirmed helper nonadmission also
permits finishing it as a failure. Wrong identities/generations consume nothing.
A matching final outcome consumes the ticket; a malformed receipt, wrong backing,
stale revision, allocation failure or cancellation publishes no fact. Successful
facts retain their own accounted receipt bytes, origins and content lease.
Helper replacement and disposable-cache eviction cannot revoke independently
valid evidence. Backing withdrawal, context closure and explicit scoped metadata
withdrawal make dependent facts unavailable; withdrawal never reactivates them.

Content evidence and membership eligibility are separate. Attaching or detaching
advances the primary's inventory revision without invalidating its unchanged
content metadata. Existing strict artifact leases continue to reject a changed
inventory revision. The weaker content-current predicate grants no query,
location, runtime-mapping or access permission.

The candidate coordinator allocates its owner, primary lease and relationship
slot before preparing an independently charged artifact context. It copies the
complete candidate origin and protected namespace spelling/key. Planning a job
performs no I/O or helper submission. The caller submits that exact plan and
records each acquisition offer before authorizing any growth. Failed reservation
or a late/stale offer requires cancellation and retirement without a grant.
A successful retained raw copy establishes only artifact ownership; the later
inspection and relationship check establish their separate claims. The candidate
context remains private to the coordinator until attachment commits. It is not
an independently opened artifact for All Open Artifacts scope capture.

Publication is one allocation-free frontend step after rechecking the captured
primary revision, both facts and the selected relationship route. Only then does
the primary revision advance and the reserved relationship become installed.
Until that step, the previous scope remains eligible. Failure leaves the primary
open, reports the exact failure/relationship reason, and retires only the private
candidate. There is no implicit fallback, rescan, helper restart or target access.

Cancellation and closure cannot erase an outstanding job. The coordinator exposes
the need to cancel, retains its exact ownership until worker release/reap, then
plans bounded helper cleanup. It closes a published candidate and begins cache
eviction, or abandons unpublished staging. Exact known keys select removal;
unoffered staging uses the existing empty-slot cleanup operation. Failed cleanup
blocks destruction, preserves all remaining charges and records cleanup failure
separately from the original failure. Retry is explicit. Releasing primary leases
after job retirement does not wait for physical candidate cleanup.

A new inventory capture includes primary and selected companion by default, with
explicit primary-only or companion-only narrowing. Companion-only without an
attachment is a valid empty inventory which still captures the primary revision.
Each member retains exact context, content, backing and provenance; companion
members also retain relationship identity. Allocation failure publishes no
partial capture. Invalid attached evidence fails combined/companion capture
rather than silently shrinking it. Primary-only capture does not depend on an
unrelated GNU relationship claim; its DWARF validation remains a separate duty.

Captures never acquire later attachments. Attach/detach, relevant context closure,
backing withdrawal or supporting fact withdrawal makes a dependent capture
non-advanceable immediately. Its original count does not change; stale captures
cannot return actionable members. Retaining historical bounded display data is
the later publication owner's responsibility, not permission to reuse a capture.
Reopening an origin creates a new context and cannot reactivate old captures.

Detach closes only the relationship-owned companion context and returns exact
retired context/content/relationship identities to the artifact-cache owner.
Closing or withdrawing supporting evidence already revokes eligibility; bounded
collection subsequently visits at most one relationship per call to release
invalid installed ownership. Pending candidates stay owned by their coordinator.
Returned cache obligations must be retained until explicit eviction and confirmed
supervised cleanup. Query leases can delay eviction; deletion and charge release
never follow merely from detach, cancellation or helper reap.

All ownership is reserved and charged before allocation: persistent metadata facts
to shared evidence, coordinator/inspection state to temporary construction, and
relationship maps, scope captures and artifact leases to frontend storage. The
subsidiary accounts remain within the aggregate frontend account. Simultaneous
old/candidate facts and captures are charged separately; shared facts are charged
once. Capacities for facts, inspections, attachments and captures are required
positive settings, allocated on demand. Inspection capacity cannot exceed the
existing helper-job ceiling. Lookup/insert/remove use fixed-depth identity maps;
collection does not scan the entire registry. Exhaustion preserves accepted
relationships/captures. Production count and decoder-sizing defaults remain the
SE10 measurement gate; test capacities are not production policy.

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
index rules remain distinct from the numeric machine and are described below.
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
revisions, consumer interests, cancellation and deadlines. The
[Source preparation layer](#source-preparation-and-byte-correspondence-se07) composes
file, line and code metadata; sizing and frontend publication retain SE10 and
SE09/SE11 respectively.

#### Directory and file prologues

`line_files` decodes the complete variable prologue into caller-owned entry and
field arenas. Versions 2–4 retain NUL-terminated directory/file inventories and
the file's directory index, timestamp and size. Version 5 retains the encoded
content/form schema and counted entries, including raw MD5 spans. Initial
version 2–4 indexes begin at 1; version 5 indexes begin at 0. The implicit legacy
compilation directory at index 0 is not fabricated as an encoded entry. Duplicate
names and identical metadata remain separate physical records.

Each field retains its section-relative start, content code, decoded form and
raw payload coordinates. The payload offset is relative to the field start;
DIE attribute views instead contain adjusted section-relative payload offsets.
Inline strings remain spans. `strp` and `line_strp` remain explicit dependencies
on the selected same-content string section. Supplementary/indexed path forms
retain their framing with unsupported coverage; this does not admit split or
supplementary artifacts. Supported standard content/form pairings follow DWARF 5
Section 6.2.4.1. Repeated standard content codes are conflicts; repeated vendor
codes remain distinct schema fields. Safely framed unknown content is preserved
with partial coverage. Unframeable encodings are unsupported, never guessed.

Each feed performs at most 256 byte, field or schema actions, including bounded
schema-conflict checks. It can advance internal work without consuming input;
the progress counter distinguishes this from waiting for bytes. Complete or
partial views require exact prologue consumption. Malformed data, schema
conflicts, unsupported framing, incomplete EOF or exhausted entry/field capacity
expose no successful prefix. The on-wire schema count is bounded by its ubyte
encoding; it is not a production memory-capacity default. State, schemas, full
arenas and pinned source/evidence require caller accounting and lifetime control.
No full path/string copies, allocation, I/O or local source access occur here.

MD5, timestamp and size are recorded metadata. Decoding them neither verifies
the displayed source bytes nor authorizes local-path resolution. Directory-index
interpretation, compilation-directory selection, path mapping and source
correspondence require separate evidence; raw fields cannot become actionable
paths merely by concatenating them.

#### Explicit unit references

`line_reference` examines the complete direct root `DW_AT_stmt_list` scope of
one selected, structurally validated unit. It checks matching backing, unit,
abbreviation context and root coordinates, preserving the attribute's physical
provenance and the associated CU header. It distinguishes absence, duplicate
attributes and malformed supported values. Equal duplicates remain conflicts;
no inherited/name-based fallback supplies a missing reference. Lookup examines
at most 256 attributes per step and retains no successful candidate before the
whole root scope has been examined.

A found `sec_offset` is an unresolved section-relative dependency. The consumer
must explicitly select the same-content uncompressed line section, prove the
referenced contribution boundary, validate its prologue/program and retain all
supporting scopes. It must not accept an arbitrary interior offset merely
because bytes there resemble a header. CU address width remains independent of
line-table version and offset format. These components grant no executable-range,
runtime-mapping, source-file correspondence or action authority. Composed Source
preparation and durable frontend evidence ownership remain subsequent work.

### Code-range foundation (SE07, partial)

The allocation/I/O-free range components decode uncompressed, non-split DWARF 4
`.debug_ranges` lists and all eight standard DWARF 5 `.debug_rnglists` record
kinds. Address widths 1/2/4/8 and target byte order remain independent of the
DWARF32/64 offset representation. Segmented and wider numeric addressing remain
explicitly unsupported. These are artifact-address metadata, not executable
instruction boundaries, standalone entries, runtime mappings or access authority.

`indexed_tables` decodes the fixed version-5 `.debug_addr` and `.debug_rnglists`
contribution prefixes in at most 20 bytes. It validates array framing and checked
entry offsets without loading an entire table. Address entries and range-offset
entries retain distinct kinds. Range offsets are relative to the beginning of
the offset array, not the contribution start. A selected range-offset entry must
point into the list area, but that check does not prove a physical list boundary.
Nonzero unsupported address/selector widths can retain header framing without
numeric interpretation. Entry decoding inspects at most eight bytes.

The caller explicitly establishes section roles, frozen-content ownership,
contribution/list boundaries, CU association, relocation applicability and the
exact `addr_base`/`rnglists_base` relationships. A plausible interior header or
list is insufficient. These primitives do not discover sections, inherit missing
bases or select an address table automatically. The
[table inventory and binding components](#indexed-table-association-se07-partial)
establish explicit contribution/base relationships. The
[boundary index and location composer](#code-location-composition-se07-partial)
now provide structural list scopes and resolved metadata; composed Source evidence
remains separate work. Direct DIE
attributes have their own [interpretation contract](#direct-code-attributes-se07-partial).

The list machine retains every encoded record in physical order, with exact
record/operand spans, original numeric operands and any supplied indexed-address
values. Legacy base-selection and end records retain both encoded operands;
their normalized kinds do not invent an on-wire opcode. Intervals retain their
source record ordinal and applicable base provenance. An absent initial CU base
is distinct from a validated zero base. Relative entries require an established
applicable base; explicit base records change only their own list. Checked
arithmetic rejects underflow/reversed bounds, overflow and values outside the
supported target address width. No reserved-address behavior from DWARF 6 is
silently applied to older formats.

An indexed address operand pauses with an explicit request against the captured
address-table header. Supply must match the exact requested index, entry offset
and unchanged header. An incorrect supply preserves the request; an unavailable
dependency fails explicitly. Resolution performs no filesystem/debugger work.
The outer operation retains the corresponding physical section, CU attribute,
content and context evidence; numeric values alone never establish those links.

End markers delimit lists without consuming adjacent-list bytes. A successful
view requires complete framing, all necessary dependencies and final interval
validation. Input ranges may be unordered. Bounded heap steps sort intervals by
begin/end/physical ordinal, followed by a bounded overlap scan. Nonempty overlap
is malformed; adjacent and zero-length intervals remain distinct. Raw records
retain their encoded order. Base and end records are never discarded merely
because they produce no interval.

Each feed performs at most 256 byte, interpretation, heap or validation actions.
It may consume no bytes while progressing internal work. Declaring EOF still
permits bounded sorting/dependency completion; incomplete framing is malformed.
Caller-owned state and full record/interval arenas require accounting and pinned
evidence. No production capacities, allocation, I/O or callbacks are introduced.
Failure, cancellation or exhaustion exposes no successful prefix. Discard releases
only decoder state; independent frontend ownership and invalidation remain with
the publication/evidence layers. Outer jobs retain revisions, interests,
cancellation checkpoints and absolute deadlines.

### Direct code attributes (SE07, partial)

`code_attributes` interprets the selected physical DIE's direct `low_pc`,
`high_pc`, `ranges`, `addr_base` and `rnglists_base` attributes. Initialization
requires a complete matching unit/tree context, including the physical root,
unit offset, abbreviation offset and retained backing. Unit and DIE indexes
are named fields in one selection. Each lookup proves uniqueness over the whole
selected DIE scope; equal duplicate values remain conflicts. Absent attributes
retain explicit complete-scope evidence. Specification/abstract-origin links
never supply inherited code addresses or bases.

Address-class values remain distinct from address indexes. High-PC constants
are nonnegative lengths, not absolute endpoints; supported forms are
`data1/2/4/8`, `udata`, nonnegative `sdata` and `implicit_const`. A direct implicit
constant requests the exact retained abbreviation table and validates its
physical owner and ordinal in bounded steps. Its numeric length and abbreviation
provenance do not replace the original zero-footprint DIE attribute. Wider
uninterpreted constants remain unsupported.

Direct numeric low/high pairs produce checked half-open artifact intervals.
Negative lengths, reversed bounds, target-width/64-bit overflow, a high PC
without a low PC, or high PC combined with a range-list attribute are malformed.
Empty intervals remain valid. A low PC alone supplies no extent; low PC combined
with a range list remains separate metadata, including the CU's explicit default
base. No first interval or numeric low PC alone authorizes an entry-point action.

Range-list section offsets and version-5 `rnglistx` indexes remain distinct
unresolved dependencies. Version-5 CU table bases are explicit root attributes;
selecting a child does not silently fetch or inherit them. Completion of these
metadata facts does not establish an indexed address, contribution association,
list boundary, relocation applicability or executable storage. Consumers must
retain exact root/base, table, section and complete-list evidence independently.
The existing table/list primitives do not acquire those relationships by receiving
a plausible numeric offset.

The complete view preserves each field's physical selection, original attribute,
normalized kind/value and any separate abbreviation provenance. It marks an
interval available only for a fully direct numeric pair. Each step inspects at
most 256 attributes or abbreviation entries. State and pinned dependencies are
caller-owned/accounted; the component introduces no allocations, I/O, production
capacity defaults or debugger commands. Failure exposes no completed view.
Source paths/correspondence, composed code-location preparation, publication,
runtime mapping and action authorization retain their separate gates.

### Indexed-table association (SE07, partial)

`tables` constructs a complete sequential header inventory for one explicitly
classified, retained `.debug_addr` or `.debug_rnglists` section. Two bounded
passes count/reserve and populate exact contribution records. Each record retains
its physical offset and checked extent; supported headers also retain the exact
entries/list offsets and independent offset/address widths. The constructor
performs no I/O. State, reader scratch and array capacity use the caller's
construction account; no production capacity default or full-file buffer is
introduced.

A safely framed unknown version remains an opaque contribution with explicit
unsupported coverage. Version decoding precedes version-5 layout requirements:
an unknown version with only a complete version field is unsupported, whereas
the same truncated version-5 header is malformed. Unknown bodies are not parsed
using a guessed layout. Nonzero numeric widths/selectors can retain header
framing without establishing numeric support. Malformed required framing or
resource/read failure exposes no completed inventory. Even an empty selected
section requires exact retained-descriptor checks.

Binding takes a complete direct code-root view and a complete table inventory
over the same retained backing. It requires the selected `addr_base` or
`rnglists_base` to equal an actual entries-array start, not an interior address
or plausible header. Missing bases report dependency; no zero/first-table default
is supplied. Each step examines at most 256 contributions and exposes no candidate
before completing its scope. A base within an opaque contribution remains
unsupported; an unmatched base in the fully examined supported scope is malformed.
Selected address width must agree with the CU. The table's own offset format
remains authoritative; it is not inferred from the CU or another contribution.

Empty contributions remain real records. Their entries offset can coincide with
the next contribution's header, so ordinary containing-range lookup cannot replace
exact entries-offset comparison. Equal raw attribute values have already retained
their physical uniqueness/conflict evidence in the direct code interpreter.
Binding preserves that exact base fact, unit context, table record and physical
section/contribution selection; it does not merge CUs sharing a table.

The entry reader selects one checked index from a found binding and the same
retained directory/backing. Construction performs no I/O. Bounded reader steps
revalidate the exact protected read-only descriptor and return only a complete
numeric entry with its captured binding. Unsupported widths/selectors remain
explicitly unavailable. Wrong/out-of-range requests allocate nothing; reservation,
allocation, integrity and read failures expose no successful prefix. Cancellation
retires owned state/scratch without closing a borrowed descriptor or granting a
retry. The outer owner pins all dependencies, revisions, interests and deadlines.

Header inventory does not validate every entry or list body. A decoded range
offset must still reach an independently proved list boundary and a completely
validated list before it supports a code-location claim. Address values remain
artifact metadata; source correspondence, executable storage, runtime mapping,
action authorization and frontend evidence ownership remain separate contracts.

### Code-location composition (SE07, partial)

`range_index` frames an explicitly selected range-list scope without interpreting
addresses. It retains each encoded entry's exact start/end and the terminating
list end. Every entry boundary can be selected independently, including an empty
end-entry suffix; starting there never inherits an omitted base-selection entry.
Operand-interior and scope-end offsets cannot become list starts. DWARF 5
end-of-list padding remains distinct empty entries. A DWARF 5 scope is exactly one
proved contribution's list area. A DWARF 4 scope requires an independently
established start and CU address width; the section has no header establishing a
single width for unrelated CUs. Neither decoder guesses that missing evidence.

All eight standard DWARF 5 encodings and legacy fixed pairs retain structural
framing. Terminated LEB values wider than 64 bits, nonstandard nonzero address
widths and segmented fixed operands can be framed without claiming numeric
support. Unknown opcodes have no guessed size and make the selected scope
unsupported. Missing operands/end markers are malformed. The complete scope must
finish before a boundary view is available; a valid earlier list cannot leak out
of a later failed scope. Byte inspection, record handling and backward linking
of terminating offsets each count toward the 256-action service bound. Large
lists need several slices even after all input bytes have arrived.

`code_locations` composes complete direct code/root views with selected table
entries. Exact unit/root identity, backing, base-attribute coordinates, table
selection and entry offsets must agree. It resolves supported indexed PCs,
checks high-PC endpoint/length arithmetic at the CU address width, and preserves
low-only locations separately from intervals. Root low-PC absence remains an
absent CU base; a validated zero remains available. Required missing entries are
dependencies, incompatible supplied identities are invalid, and impossible
numeric intervals are malformed. No bare-name or first-table fallback exists.

For range references, the composer combines a resolved direct offset or exact
`rnglistx` entry with the complete boundary index. Version-5 index bounds must
match the supplied proved contribution; an indexed reference must select that
same table. The resulting semantic-decoder request retains the selected offset,
exact terminating end, CU base availability and validated address-table context.
The existing range machine then resolves individual indexed operands and checks
arithmetic and overlap before publishing intervals. Framing alone does not
validate those semantics. Physical section roles, retained-content leases and
relocation applicability remain explicit outer-owner obligations.

Both components perform no allocation, I/O, discovery or debugger dispatch.
Stationary state, arenas, retained input evidence and copied results belong to
the caller's accounts; no production capacity default is introduced. Output
publication remains conditional on complete success. These metadata views are
not independently owned frontend prepared-action facts. Executable-storage
classification, instruction boundaries, standalone entry points, Source-byte
correspondence, runtime mapping and access authorization require their separate
evidence. The [Source preparation layer](#source-preparation-and-byte-correspondence-se07)
composes these metadata views; frontend publication, runtime and live actions
retain their later gates.

### Source-file provenance and paths (SE07, partial)

`source_file` selects an exact file record from complete same-content line
prologue/program views and the selected unit's explicit line reference. Initial
indexes start at 1 in versions 2–4 and at 0 in version 5. A row selection retains
its ordinal and encoded file number; end-sequence rows have no usable source
file. Declaration and call file zero remain unspecified, including version 5,
rather than selecting its real file entry zero. Coordinate interpretation follows
[DWARF 5 Sections 2.14 and 3.3.8](https://dwarfstd.org/doc/DWARF5.pdf).
Inherited coordinates must use the line reference of their physical source unit,
not the consumer's unit or a matching basename.

Legacy `define_file` events append after the validated initial files. A row may
select only an event already defined before that row's instruction. Each selected
initial/event record retains the line-section identity, physical offset, unit
reference and raw file index. Directory records remain independently identified.
Numeric timestamp/size availability records field presence; encoded zero remains
unknown. A checksum is a recorded 16-byte MD5 span, not a source match.

Selected inline, `strp` and `line_strp` paths become explicit requests to the
existing bounded string reader over the retained descriptor. Inline coordinates
are adjusted from field-relative payload offsets exactly once. Missing string
sections remain dependencies; unsupported framed path forms remain unsupported.
Unsupported optional numeric fields retain partial metadata; path-reference
support stays separately reported. Field selection examines at most
256 fields/transitions per call. No string copying, filesystem discovery, source
file opening or debugger dispatch occurs in this component.

`source_path` composes fully supplied text in a POSIX byte-path profile. Absolute
file/directory names do not need unrelated relative bases. Legacy compilation
context comes from the explicit unit `comp_dir`; version 5's explicit directory
zero supplies its recorded current compilation directory. Relative compilation
context requires a captured absolute linked-image directory or an explicit
reviewed mapping. This follows the [relative-path clarification](https://dwarfstd.org/issues/210628.1.html).
Private backing paths and the frontend's current directory are never implicit
bases. A missing file-directory index remains unknown for a relative file; it is
not silently replaced by zero. Different compilation contexts remain evidence
to reconcile in Source preparation, not interchangeable strings.

Composition preserves repeated separators, dot/dot-dot components, spaces and
non-ASCII bytes. It performs no canonicalization, symlink resolution, environment
or tilde expansion. Embedded NUL/CR/LF are unavailable for a Source argument.
Other producer path conventions require an explicitly supported profile or
reviewed mapping; no host/target syntax translation is inferred.

Mappings use case-sensitive byte prefixes ending at component boundaries.
Multiple matches remain ambiguous even when destinations agree. An explicitly
selected matching rule resolves the choice; no implicit first/longest selection
occurs. The complete recorded path remains available independently of a local
candidate. Unresolved relative paths and ambiguous mappings cannot yield an
actionable local candidate. Each step examines at most 256 bytes/rules/transitions.
Caller-supplied capacities include terminators; exhaustion exposes no successful
truncated path. The existing Source consumer limit still applies at integration.

The project-owned streaming MD5 component implements [RFC 1321](https://www.rfc-editor.org/rfc/rfc1321)
for DWARF source-digest comparison. Each update accepts at most 64 KiB. Exact
complete length is required at finalization; input/length failure poisons the
stream and cannot publish a prefix digest. MD5 is not the artifact fingerprint
or a security/integrity primitive; admission continues to use SHA-256 and exact
byte comparison. A matching recorded source digest must be bound to the exact
retained displayed bytes and their revision before correspondence is claimed.
A path, timestamp, size or digest alone grants no runtime or action authority.

These components introduce no allocations, I/O, production capacity defaults or
frontend publications. State, candidate buffers, input text, rules and retained
metadata belong to their callers' existing accounts and lifetimes. The following
preparation layer composes their evidence. Offline UI and live Source actions
retain SE11 and SE15 respectively.

### Source preparation and byte correspondence (SE07)

`source_context` reconciles paths built from the exact selected file's compilation
contexts. The default is line-table directory zero for version 5, otherwise the
physical unit's `comp_dir`. A separately supplied version-5 alternative retains
that same unit root's complete `DW_AT_comp_dir` provenance. Both candidates must
come from the existing path composer over the same file/directory selection;
all string dependencies and path buffers stay retained. Readiness covers this
captured candidate inventory, without validating an unexamined root attribute.
Consumers claiming full context reconciliation must retain that attribute lookup
outcome and supply every relevant found alternative. A missing or unsupported
default context does not erase an independently supported explicit unit choice.

Automatic agreement compares complete recorded **and** local path bytes in bounded
byte-pair steps. Two different recorded contexts remain ambiguous even if mappings
lead to the same local file. Different lexical spellings are not canonicalized.
An explicit choice selects exactly one complete candidate and preserves both
provenances. Missing context remains a dependency; ambiguous mappings remain
ambiguous. Neither outcome supplies a prepared local Source path.

`constants` now also interprets direct declaration file/line/column fields using
its existing uniqueness, numeric-class and implicit-constant checks. The
`source_prepare` coordinate composer retains each original constant and physical
owner. Already selected inherited owners use their own unit's file numbering;
this layer neither traverses relationships nor chooses between owners. A mixture
of different physical units is ambiguous, even when numeric indexes agree.
Distinct owners in one validated numbering context remain separately identified.
Declaration/call file zero and line zero are absent, without a file-zero or
nearest-line fallback. Missing columns remain distinct from encoded zero.

Source preparation combines the accepted path context, complete numeric program,
exact file selection, row or declaration/call coordinates and optional resolved
code metadata. A row request selects only that physical row; a declaration/call
request preserves every matching file/line row in program order. End-sequence
and unattributed rows never become compiled locations. Legacy dynamic files are
visible only after their defining instruction. Repeated addresses, operation
indexes, discriminators and row ordinals remain distinct.

Without code evidence, preparation supports recorded Source browsing and makes
no compiled-location claim. With code evidence, rows must lie inside an explicit
nonempty half-open low/high interval or a complete semantic range-list decode.
A list must match the exact request derived from the selected code's boundary
index, CU base and address-table dependencies, including its terminating end.
Disjoint intervals remain separate; low PC alone is insufficient. No first-range
entry point, instruction-boundary, executable-storage, runtime mapping or action
permission follows from a matching artifact row. Those claims still require
their own evidence and consumer gates.

The caller supplies the full output arena and receiving consumer limits. Existing
Source requires paths shorter than `TB_CODE_PATH` (4096 bytes including the
terminator) and lines representable by its `int` selection (`INT_MAX`). Preparation
fails on an oversized path, line or unrepresentable arena capacity; it never
truncates or narrows an actionable argument. Each service call performs at most
256 row/range/transition actions. Total matching can require rows × intervals
work; cancellation and deadlines remain outer-owner obligations. The complete
selected scope must finish before its view is available. Arena exhaustion or
cancellation exposes no accepted prefix and does not alter independently owned
published results.

`source_check` binds recorded MD5 comparison to an explicitly retained immutable
raw-source snapshot: nonreused identity, revision, byte pointer, complete length
and completeness state. These are the exact bytes from which displayed text is
derived, before escaping or line-ending presentation. The owner advances revision
before changing bytes or selection and retains the old lease until pending work
detaches. Pathname/stat observations cannot replace that lease. A changed lease
makes the result unavailable; changes observed during service finalize stale.
Helper lifetime is not a source-byte revision.

Hashing processes at most 64 KiB per service call and compares only a finalized
complete digest with the validated recorded 16-byte span. Missing digests and
incomplete raw snapshots permit explicitly qualified recorded browsing, never a
verified prefix. Complete snapshots distinguish match from mismatch. A match is
limited to the recorded MD5 profile and exact retained displayed bytes; it does
not establish atomic source acquisition, artifact/runtime correspondence or access
authority. MD5 remains unsuitable as a security primitive.

The existing `src/model/code.c` loader remains GDB-driven. Its renderer output is
escaped and its source buffer may be partial after read, byte or line limits;
those states cannot supply complete correspondence evidence. Native Source
integration in SE11 must retain its own raw-byte lease, honor existing path/line
limits, mark recorded versus matching/mismatching correspondence, and withdraw
matches when that lease changes. SE15 consumes exact artifact locations only
after runtime and action-specific checks; it manages multiple installed locations
and confirms nearby-line proposals separately. These foundations perform no local
file access, target request, breakpoint mutation or execution.

All three components use caller-owned/accounted state, inputs and full-capacity
arenas; they allocate nothing and publish no frontend guarantee. Their borrowed
views are metadata preparation, not independently retained prepared-action facts
or a public ABI. SE09/10/11/12 retain publication, sizing, UI and owned-fact work.
SE07 is complete within this supported envelope, with local verification and
maintainer acceptance recorded in the
[delivery closeout](validation.md#se07-delivery-closeout--2026-11-23).

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
