# Deterministic level authoring tool design

**Version:** 0.1.0
**Date:** 2026-10-12
**Status:** Active
**Owner:** AYEntity deterministic content maintainers
**Related:** [Module design](../../design.md) · [Tool README](README.md) · [SDK contract](../../../../AYDocs/sdk/contracts/tooling.md#toolingsimulationdeterministic-content)

## §1 Runtime and authoring boundary

`aydetlevel` is an offline adapter over the public Level decoder/validator/writer,
not a second grammar implementation or tick owner. It reads source before Sim,
uses actual facade factories and fresh scratch installation/seal, then emits JSON
or a fresh canonical text file. Validation establishes currently representable
content/identity, not future movement, damage, recorded gameplay or native success.
Stage54 supports formats1..9; necessary local short acceptance passed separately from preflight.

## §2 Complete identity and output

Format9 appends sorted Brain bindings after Attack. Real prepared Character/Attack/
HP and Brain policy descriptors validate stable IDs, enabled owner roles, Hurt
whitelist, software bounds/sensing/speed bits, namespace collisions and aggregate
capacity. AYDLV009 binds full field-wise content, not only a hash. Formats1..8 keep
their branches and reject nondefault new authority rather than migrating it.
Existing text64KiB/Level semantic8KiB limits and actual joint64-schema budgets
apply; nested Brain18KiB/19 chunks is a separate upper bound, not extra capacity.

Inspect returns exact `brains` rows and `brainOwnerCount`; actor/target UInt64
strings avoid JSON integer rounding, and Float32 lanes remain eight-digit bits.
Primary target sensing does not replace the full Attack whitelist. Canonicalize
preserves the explicit format and full semantic identity while normalizing source
spelling/whitespace. Numeric content uses the software Float32 text facade; no
native float conversion grants simulation authority.

## §3 Files, failures and lifecycle

All commands run outside Sim. Read/parse/preflight failure reports a nonzero exit
and JSON diagnostic. Fresh-only output never overwrites source or an existing
destination; an I/O failure can retain a partial new file for explicit handling.
It does not claim power-loss durability, input authentication or save migration.
Runtime file/resource/cooked acquisition and checkpoint restore keep their public
contracts; authoring does not invoke a live Scene or install a second Host.

## §4 Evidence scope and maintenance

Stage53 core Brain acceptance and Stage52 attack/CLI records remain historical.
Actual Stage54 receipts stage54-levels1/host1 passed LevelBrain9Checks1.24213s,
BrainHostExample11.3003s and CLI1.19754s, with old8/7/Cooked/Resource compatibility.
Level9 fixture performed new content text/cooked roundtrip; Resource is only old
sample compatibility, not new9 Enemy Pak execution. SourcesUnchanged and no
production behavior fix. Selected hand behavior, full actual CP/path equivalence
and file/text/cooked comparison are distinct,
and none is an independent whole-state golden or history authentication.
Native final VM remains Deferred; no broad matrix or long campaign is implied.
Update this document/README/SDK when grammar, prepared policy, file protections,
typed output, runtime acquisition or evidence scope changes.

## §5 Stage56 complete Control authoring boundary

Level10 tools remain adapters over the public parser/preparation, not a parallel
Control implementation. Full controls table follows brains and uses exact software
Float32 bits and finite windows/Booleans. Initial roles require advanced enabled
ordinary Kinematic Character and owner disjointness with Brain; optional Respawn
is the exact prepared complete policy, not a hash or inferred HP-recovery option.
Actual factories reserve used namespaces and real fresh Session seal enforces
aggregate limits. Old1..9 branches/default bytes stay unchanged, new nonempty
rows refuse old formats rather than upgrade their identity.

Inspect shows all controls fields/UInt64 actor strings/exact bits; canonicalize
preserves explicit version/complete content to fresh output. No command executes
the finite window, polls future geometry, mutates inputs, owns a tick, decodes
future CP history or creates a save. Actual Level10/CLI/standard Host runtime is
actualLocalPassed: CLI1.37562s/newLevel10 .223617s/standardHost13.2032s; actual
newtext/cooked is Levelfixture, Resource1.00207s is only old-sample compatibility.
Stage55core evidence stays separate. Receipts stage56-levels2/levels3/host2 and stage56-actual-freeze.json bind
per-run unchanged sources. levels2 remains overall Failed, with three genuine old
case passes retained without rerun; only new10 was retried. First new fixture
C2677/C2661 came from quoted-helper ADL (renamed jsonQuote); later CRLF source1007
was wrongly equated with canonical LF980, corrected using independent previously
accepted Stage54 canonical bytes/FNV3155852323544543593 while retaining original
source/content fingerprints. Production behavior/hand oracle unchanged. First
standard build exit1 had no compiler diagnostic; cause remains unconfirmed.
All three failed receipts remain. The new test changed across batches, so whole
source snapshots are not claimed identical between them. Stage55 standalone
core evidence remains separate historical scope.

## §6 Stage57 bounded authoring-only acquisition

The existing CLI adapts software text and public Level factories. `float-bits`
requires exact8 hexadecimal spelling and finite binary32, then formats that same
bit pattern without native floating conversion; `float` retains its previous
parser/rounding. No numeric/profile/checkpoint/save/replay wire changes follow.

The two source-defined starters are initial content, not an application factory.
Both author reciprocal solid Kinematic10/Static100 bodies at exact dyadic standing
positions; format1 selects ordinary Character, format10 selects explicit advanced
Character/one-way policy and one groundOnly Control owner. application/input1 and
step1/64 belong to their content identity. No Enemy, HP, attacks, progress, rules,
mutable dash phase or input mapping is inferred. A caller needs matching input
and Host ownership. The first pre can legitimately refuse groundOnly dash when
initial committed support has not yet been established.

Complete validation/seal and canonical encoding precede fresh exclusive output;
existing targets never get replaced. Flush and close failures retain diagnostics
and can leave a partial new file for explicit handling, without deletion or
durability claims. Existing format1..10 serialization branches are unchanged.
Actual local Stage57 LevelToolChecks passed1.8485s; standard Tool build4.953s.
The existing finite gate exercised reverse1.25/-0 and malformed/nonfinite cases,
both real fresh starters/create/validate/inspect, full canonical byte roundtrip,
and no-overwrite/invalid-kind paths. Source/executable pins and before/after
snapshots match stage57-actual-freeze.json; no production retry/fix. Physical
flush/close fault injection was not run. No simulation matrix or new Host
recipe is accepted by these static authoring checks; native final VM Deferred.

## §7 Stage59: explicit sight identity without runtime observations

The tool uses the normal public Level11 decoder and selected Brain1/2 factories.
It adds an explicit JSON policy boolean, not a second grammar/query algorithm or
an observed visibility result. Text `brains count flag` and AYDLV011 semantics
encode the full value before unchanged Brain rows/control tables. False selects
Brain1, true requires nonempty owners; older branches reject true and preserve
their exact defaults/serialization. Joint budget/namespace checks use the actual
selected descriptors and complete nested identities in a fresh Session.

Canonicalization preserves all authority/version to exclusive fresh output;
partial physical failure semantics remain. No ray, target action, prepared
Attack or checkpoint is generated by preflight. The separate standard Host may
restore an earlier blocked decision after the door mask changed: source policy
validation and recorded observation closure do not authenticate historical
visibility. Actual necessary local CLI1.85419s/Level11 1.10249s/standard sight Host10.4039s
passed. Seven unique cases include affected old10/7/Cooked and existing Resource
compatibility; old Control canonical bytes match accepted Stage56 output. No
numeric/starter behavior or new Resource packaged execution claim is expanded.
Snapshots remain unchanged within each batch; fixture keyword changed across
batches. First target-name failure had no product tests, first Host/CLI grammar
rejection remains, and genuine Resource pass from overall-Failed host1 was
retained without rerun. Production/hand checks unchanged; native final VM Deferred.

## §8 Stage60 private static SVG construction (local checks passed; native Deferred)

A private builder consumes a completely checked Level and no Session callback.
It materializes world endpoints with public software AABB/filled-wedge types;
four absolute AABB vertices avoid recomputing max from a rounded visual width.
Each path is instanced at actor.position+point.offset, independent of body.offset.
The renderer's Yflip does not mutate content; exact decimal formatting never
passes through native float. The enclosing viewBox is a display-only bound.

The builder limits64 actors,64 path instances,24 points each/1536 total and1MiB
complete SVG. Finite endpoints and aggregate spans are checked before SVG/I/O;
zero-span layout has a1-unit fallback, never invented collision area. Fixed SVG
templates and escaped labels use no script/href/external style. Full SVG and
receipt exist before the established fresh-only writer is called. Existing
output refusal and possible physical-failure partial fresh files are unchanged.

No core facade, current checkpoint reader, second parser or tick owner is added.
Actual local MSVC Release `AYEntity_LevelToolChecks` passed2.26624s; the
standard production Tool build passed5.438s. The single finite gate performed
seven preview invocations: four successful outputs and three explicit refusals.
Selected hand vertices/world and viewBox bits, path origin independent of body
offset, mask0, repeated terrain whole-SVG byte equality and XML parsing of all
four outputs passed. The terrain output has5518 bytes,11 actors,1 path instance
and2 waypoints, with contentHash `1216874329789544449`.
Evidence is `stage60-actual-freeze.json` / `stage60-tool2`. The earlier
`stage60-tool1` ran the old Stage59 CLI after a closure-plan EOF/guarded-install
refusal and is explicitly excluded from preview acceptance. Only the closure
plan was repaired before the genuine installed-preview gate; no product behavior
or hand oracle changed. Raw excluded and accepted records are preserved.
No simulation tick/current CP, browser-rendering acceptance, future reachability,
independent full-CP golden or native/transport authentication is established.
Physical flush/close fault injection was not run; final native VM Deferred.

## §9 Stage61 finite property edit adapter (local checks passed; native Deferred)

The private ID helper uses unsigned decimal from_chars with full-token/end and
nonzero checks; numeric helpers call the unchanged finite FloatText parser.
Argument indexing is fixed: source/actor/X/Y/destination, with no alternate
locale syntax or native floating intermediate. Positive half checks use rounded
software values, so signed0/subnormal underflow cannot evade the constraint.

The source is decoded into a local complete Level. Only position or body.half
is assigned, then full validate/fresh seal and encode precede fresh-only I/O.
This preserves explicit format/application/input/step and all authority, and
delegates geometry/role/joint budgets to existing actual factories. Path origin
is derived from the changed actor position, while independent absolute spawn
coordinates are untouched. Same exact assignments produce the same canonical
bytes/content identity; a different exact value changes authored identity.

Actor inspection gains halfExtentBits/offsetBits beside positionBits; no new
standing-capture state or current-CP view is exposed.

Actual local MSVC Release `AYEntity_LevelToolChecks` passed2.41834s, with
production Tool build5.891s. Selected independent dyadic place(-1.25,0.5) and
resize-half(0.5,0.5) inspect bits, changed content identity, retained unselected
tables, full edited/canonical/no-change byte equality and four unknown-actor/
nonfinite/nonpositive-half/existing-output refusals passed. The genuine evidence
is `stage61-actual-freeze.json` / `stage61-tool1`; source/executable pins and
before/after snapshots are bound there, without production behavior/oracle fix.
This finite offline gate does not separately execute an edited bound path or
absolute-spawn gameplay, Sim/current CP, physical I/O faults or browser rendering.
No hot reload, save migration, runtime pose writer, new numeric/wire profile or
historical authentication is added; final native VM Deferred.

## §10 Stage63 finite Static insertion adapter (local verified; native VM Deferred)

Private parsing reuses Stage61's full-token nonzero UInt64 editActorId and actual
finite FloatText editDecimal; a new full-token UInt32 from_chars helper accepts
layer/mask decimal without overflow or locale dependence. argc12 is fixed, and
all unused authoring body fields are default zero/false. Sorted lower_bound
insertion rejects duplicates. Shape tokens select actual AABB or filled left/
right wedge, never a proxy/custom physics algorithm.

The source's explicit staticSlopes is required for wedges; absent policy is
rejected without modification. Existing versions and unselected authority
remain. New create terrain calls the character starter and changes only that
flag, selecting controller5/collision6 under the original format1. No advanced/
oneWay/moving/continuous/step/respawn/gameplay options are inferred. Layer0 and
represented geometry are decided by real full validation; mask0 remains valid.

Count64 and configured lower limits, text64KiB/canonical8KiB, literal policy,
initial reciprocal-mask penetration and actual joint budgets are checked by the
complete real Level preparation/seal. Static-static overlap stays the existing
composition semantics. All canonical bytes exist before exclusive fresh write.
Normal failures do not mutate files, while physical partial-new-output scope
stays disclosed. New identity is initial content, not checkpoint migration.

The finite actual gate composes create/add/place/resize/preview, verifies exact
selected polygon points and shape IDs, complete nonactor policy lines/original
actors, sorted new IDs, canonical stability/mask0 and necessary protected failures.
Actual local MSVC Release AYEntity_LevelToolChecks passed in 3.41s; the standard tool build completed in 6.188s. This finite gate exercised create/add/place/resize/preview, selected dyadic actor bits and actual filled-polygon points, sorted identities, full unselected canonical policy/original actors and canonical stability, legal mask0, a valid 64-body source and 16 protected normal refusals. The recorded source snapshots matched; the actual executable and artifacts are hash-bound. Evidence is out/detcontent36-40-20261011/stage63-actual-freeze.json and stage63-tool1/result.json. No Sim advance, future walkability, whole-checkpoint golden, authentication, new content Pak execution, broad matrix or native VM acceptance is claimed; native final VM remains Deferred. No production/oracle repair or prior gate reuse was needed for this actual invocation.
