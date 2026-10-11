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
