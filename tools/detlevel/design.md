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
