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

## §11 Stage64 — atomic finite path edits (local verified; native VM Deferred)

Exact fixed command layouts
are argc10/6/10 including argv0. Full unsigned ID parsing, software finite offsets
and bounded travel/dwell counts precede any local edit.

add-path creates/binds one complete two-point ordinary policy rather than writing
an orphan forbidden by Level. First offset is canonical positive zero, both travel
counts explicit/equal and both dwell counts zero. bind-path reuses ordinary Path1.
The common binder permits only unbound ordinary solid Static: it changes mode,
initial zero velocity and path ID, leaving every geometric/authority field intact.
Wedges require already selected movingSlopes; no policy inference or Path2/Actions
permission is introduced. edit-waypoint preserves count/playback/role/bindings,
including existing Path2, and keeps exact +0 first-point lanes. Shared policy
changes resolve per-actor pose origins; body.offset and absolute spawns are separate.

Complete actual Level prepare/fresh seal validates every used path and dependency,
then canonical encoding completes before fresh exclusive file creation. Current
valid1..11 versions and app/input/step/tables are preserved. New sorted indices
can produce new registration IDs, so this is content identity, not hot reload or
old-checkpoint compatibility. Existing8paths/24points/64bodies/text64KiB/semantic
8KiB and real combined namespaces remain. No filesystem transaction beyond
fresh-only protection is claimed; physical partial output remains possible.

Actual finite existing CLI checks cover all three playback spellings, actor-origin
versus body-offset, sharing and preserved tables/roles/current formats, canonical
stability, explicit moving-wedge policy, existing Path2 edits and protected limits.
Actual local MSVC Release AYEntity_LevelToolChecks passed in 4.32962s; the standard tool build completed in 5.938s. The finite authoring gate checked Once/Loop/PingPong spellings, shared per-actor origins versus body offsets, one-point edits and previews, an explicitly enabled moving wedge, existing Path2 role preservation, complete modern Level11 tables, canonical stability, a valid eight-path source and 20 protected normal refusals. Source snapshots matched; the actual executable and raw artifacts are hash-bound in out/detcontent36-40-20261011/stage64-actual-freeze.json and stage64-tool1/result.json. No production/oracle repair or old gate reuse was needed for this actual invocation. No Sim tick, executed trajectory, future sampled reachability, collision safety, whole-checkpoint golden, Host/Pak/matrix, history authentication or native VM acceptance is implied; native final VM Deferred.

## §12 Stage65 explicit static expansion planner (local verified; native VM Deferred)

One bounded mapping token reuses full-token editActorId; shifts reuse software
editDecimal. Resolve every source/destination/role/reference/translation before
even local target insertion, sort the merged table, then call the actual complete
validator/encoder and existing writeFresh. No whole main replacement or common
prefab class is introduced.

stamp-solids donor.dlt target.dlt shiftX shiftY old:new[,old:new...] fresh.dlt
has argc8 including argv0. Both ordinary files independently load as complete
validated Level. The map token is1..1024 bytes/1..16 full nonzero decimal UInt64
pairs: exactly one colon per entry, no empty entry, overflow, duplicate donor/new
ID, missing donor or existing target ID. Map order is irrelevant; merged actor
IDs are sorted. Shifts use real finite software FloatText and software addition.
Actual rounding and signed-zero rules apply, so zero shift does not universally
preserve source position bits. A nonfinite translated pose is rejected.

Only unbound ordinary nontrigger/non-one-way Static AABB or filled left/right
wedges qualify, never Character or a path user. All authored actor references
are rejected: Action targets; Interaction trigger/actor/action target; Progress
claim trigger/actor and objective action target; Respawn spawn/binding/regions;
Vitality actor/permission/region; Attack owner/targets; Brain owner/target; Control
owner. Claim/objective/path/counter/rule IDs in separate namespaces are not
misread as actor references. This closes known authored authority, without
certifying unknown application code or remapping gameplay graphs.

The eligible complete initial body is copied, changing only explicit actor ID
and software-translated position. Shape, half extents, offset, layer/mask and
zero velocity/default roles persist. Every destination format/app/input/step,
policy/table and existing actor remains intact. Wedges require destination
staticSlopes already enabled; donor policy is never imported or auto-upgraded.
Actual full merged Level validation/fresh seal and canonical encoding precede
exclusive fresh output. Max64/configured lower bodies, text64KiB/canonical8KiB,
represented geometry/initial penetration and joint namespaces remain binding.
Static-static overlap retains the existing composition semantics.

Output is flat expanded initial content in the existing full identity, not a
prefab/reference/hot reload handle; later donor edits do not propagate. Existing
source/target/output are protected, normal refusal writes no file and physical
I/O failure may leave a partial fresh file. Existing .dlt/.dlc loading recipes
consume it unchanged. No .act/detprefab format, Resource loader, public facade,
permission remap, running-state pose/HP writer, Sim advance or future motion/
walkability/restore migration guarantee is added. inspect/preview stay offline;
actual filled polygons are shown without simulation or history authentication.

Actual local MSVC Release AYEntity_LevelToolChecks passed in 5.13788s; the standard tool build completed in 6.0s. The finite offline gate checked independent dyadic translated body fields, actual three-shape polygon/SVG vertices, permuted mapping and canonical reencoding whole-byte equality, complete unselected destination policy/actors and 22 protected normal refusals. Source snapshots matched; the actual executable and raw artifacts are hash-bound in out/detcontent36-40-20261011/stage65-actual-freeze.json and stage65-tool1/result.json. The product gate passed on its first actual invocation. JUnit stdout was truncated to 1024 characters; only the private freeze reader was adjusted to bind the genuine LastTest.log from the same CLI run directory. No product/oracle repair or gate rerun occurred. No Sim tick, Host, executed future movement, prefab lifecycle, whole-checkpoint golden, unknown application/historical authentication or native VM acceptance is implied; native final VM Deferred. No new capability/module/coverage or matrix.

## §13 Stage66 offline/runtime responsibility boundary (local verified; native VM Deferred)

The finite external recipe is a consumer of existing complete Level factories and strict fresh-only tool commands. A gameplay template supplies authority; complete canonical byte comparison closes recipe output identity before a standard Host acquires it. Existing tool process evidence and separate runtime lifecycle evidence are retained individually. No callback, file mutation or parser becomes a simulation writer.

Acquire one complete immutable Level before constructing the standard Host. Configure its exact matching Character, Path, Actions, HP, Attack, Brain and Control dependencies once; caller-owned Brain/Control actor sets must be disjoint. The sample input protocol belongs to its registered application and is not inferred from the resource. Offline authoring does not grant runtime permissions or own pose/HP/path state. The standard Host/controller remains the sole tick/restore lifecycle owner; presentation and application observations read committed state.

Stage66 finite actual scope: seven actual authoring commands matched the entire independently authored canonical resource before two focused Host invocations. Actual Live128+CP64 continuation64 and Record128 total320 advances. Complete CP execution equality and selected hand observations are distinct from independent whole-CP goldens. Physical recording existence does not prove playback, seek, historical authentication or native VM execution; native final VM Deferred.

Actual local MSVC Release AYEntity_WorkshopHostExample passed in 11.1761s; the affected original AYEntity_ControlHostExample passed in 13.4204s. Seven actual offline authoring commands succeeded and the entire generated canonical content equaled independently authored workshop.dlt before Host acquisition. Live128 plus captured CP64 continuation64 and Record128 performed320 actual advances; final complete CP bytes, every original tick trace and all64 restored suffix CP comparisons agreed. The committed workshop summary records heroHP=18, heroHurts=1, attackStarts=2, attackAttempts=1, brainPulses=2 and finalHash=10463999747462011456 (diagnostic UInt64 string). Original Control complete-byte compatibility was separately checked. Source snapshots were stable within their separate real execution episodes. Final Workshop executable bytes were unchanged; the earlier Control executable SHA remains an honest historical pre-test observation whose path was subsequently relinked, not a current-file byte verification. Exact raw authoring/Host/legacy evidence is in out/detcontent36-40-20261011/stage66-actual-freeze.json. The native-encoded LastTest.log is retained as raw bytes, not JSON/UTF-8 authority. Selected hand observations plus full CP execution equality are not independent whole-CP goldens; closed recording existence is not playback/seek/history authentication. No new interactive, broad matrix, Pak or native VM acceptance is claimed; native final VM Deferred.

The subsequent corrected-content Workshop attempt also remains failed: its selected absolute-X hand check rejected at CP121 (JUnit3.71437s). The earlier expected static cap was a false premise because Enemy is the existing actual Kinematic body, not Static. The only final Host change corrected this narrow hand oracle: absolute XY is checked through CP120; CP121..128 instead check both Y=1.5, EnemyX minus HeroX=0.5 and monotone Hero movement against actual previousPosition. No kernel, Enemy role, permission, profile or runtime behavior was changed. Its already successful seven-command recipe and configure were reused unchanged; only the necessary final Host build and sole Workshop gate continued. The directed earlier CP41 diagnostic retains 40 complete tick-trace rows (1..40), then reports the CP41 mismatch; it is a failed diagnostic prefix, not an extra passed41-tick gate. Both original failure episodes and diagnostic bytes remain bound separately from the final passing execution; no soak or native acceptance is inferred.

The first genuine composite gate is retained: the original Control case passed while Workshop failed its selected hand check at CP41. Diagnostics showed legitimate ordinary-floor X/Y corner contact at fraction0 resolving with the existing fixed X priority; Session had no execution fault. The content revision preserves the original explicitly selected continuousTerrain/terrain8 policy and every kernel/oracle rule. It changes only the new floor layout and stable authored ID so initial strict-X overlap and the intended carrier priority are explicit. The corrected complete template, independently authored expected text and seven-command recipe are bound to the final actual descriptors, not the original failed content hashes. A necessary sole Workshop continuation is new content validation; the already passed original Control case is not repeated, and its composite remains overall failed.

The earlier private pre-test CTest path-discovery refusal is retained in the resumedPreTestFailure receipt. Its completed authoring/configure/build steps and unchanged executable were reused after correcting only the OUT evidence discovery; no product gate or successful tool recipe was repeated.

## §14 Stage67 initial filter and one-way planner (local verified; native VM Deferred)

Existing editActorId/editDecimal/solidWord provide full-token ID/FloatText/UInt32
parsing. A local complete Level is changed, sorted when adding, and passed to the
actual full validator/encoder before existing exclusive writeFresh. The static
one-way role is explicit rather than an inferred shape/policy upgrade.

edit-filter source.dlt actor-id layer mask fresh.dlt has argc7;
add-oneway source.dlt new-id x y halfX halfY layer mask fresh.dlt has argc11,
including argv0 and command. IDs are fully consumed nonzero decimal UInt64;
layer/mask are fully consumed decimal UInt32. Floats use the actual finite
software binary32 decimal parser, including normal rounding/signed-zero rules.
Half extents must be represented strictly positive; nonfinite/overflow or an
underflow-to-zero half extent is refused.

edit-filter changes only one existing actor's initial body.layer/mask, preserving
its mode/shape/geometry/offset/velocity/trigger/oneWay/character/path, all selected
policies/tables and source format/app/input/step. Active layer0 is invalid; mask0
remains active geometric content, not Disabled.
Retained proxy geometry does not promise an unconditional filtered query hit:
reciprocal collision/raycast filters still exclude a mask0 body.
Full actual Level validation checks authored permissions: an Actions-controlled
initial mask can only0 or its
existing literal collisionMask. Another mask is refused; authority is never
rewritten to make an edit succeed.

add-oneway requires the source's explicit advanced+extended+oneWayPlatforms
policy. Existing create dash supplies it; ordinary character and terrain
starters do not. There is no automatic policy/version upgrade and no artificial
format>=10 restriction on otherwise valid explicit-policy content. The sorted
new unique body is ordinary nontrigger Static AABB, oneWay=true, with zero
offset/velocity, no Character/path/ignored identity. No wedge, trigger, moving
platform or gameplay permission is inferred. Runtime one-way response remains
the existing relative downward top-face crossing with strict X overlap; side,
bottom and already-inside motion is allowed, queries inspect complete AABB
geometry. Existing one-way inside and Static/Static overlap semantics remain;
the authoring tool does not promise general overlap rejection or repair.

Both commands load/validate complete ordinary source content, edit local
authoring only, then actual full initial Session validation and canonical
encoding precede exclusive fresh output. Bodies64/configured lower capacity,
source64KiB/canonical8KiB, represented geometry and joint namespace/authority
bounds remain binding. Existing source/protected output are never replaced;
normal refusals create no destination, physical I/O failure may leave a partial
fresh file under the existing writer contract. All unselected fields/tables and
versions persist. Full existing content identity includes the changed body.

inspect appends actor.oneWay Bool; old fields remain. Preview already renders
true box/top line and one-way role without simulation advance. A mask0 platform
still appears in geometric preview. Adding one-way does not supply a drop-through
timer/input or grant Actions collision-control permission for that role.
No new capability/format/facade, physics profile, Scene/runtime writer, editor,
hot reload, replay migration or future movement/walkability guarantee is added.
Existing .dlt/.dlc acquisition and canonical bits remain the current recipe.

The single existing CLI gate covers selected hand geometry/roles, full policy
preservation, canonical stability, UInt32 maximum layer/mask0, genuine authority
refusal,64-body source validation then65th rejection and protected negative
parser/role/capacity/argc outputs. No additional Sim/Host/campaign matrix.
Actual local MSVC Release AYEntity_LevelToolChecks passed in 6.08132s; the standard tool build completed in 6.485s. The finite offline gate verified selected dyadic box/top fields and role flags, full unselected policy/actors, canonical stability, mask0 active geometric content with reciprocal filters, actual authorized-door refusal, bounded-body capacity and protected argument/role refusals. Matching source snapshots, the actual executable, raw CLI artifacts and genuine JUnit/LastTest log are bound in out/detcontent36-40-20261011/stage67-actual-freeze.json. The document reader does not run product tests. No Sim tick, Host/movement/replay execution, whole-checkpoint golden, unknown application/historical authentication or native VM acceptance is implied; native final VM Deferred.

## §15 制作入门

[确定性关卡制作入门](AUTHORING.md)给出实际执行过的矩形、坡面、编辑、共享路径、实体组
复用与单向平台步骤。27条finite制作命令均通过，完整源码与工具身份相同。
它制作初始内容；运行与恢复证据由组合Host示例提供，native最终VM仍延期。

## §16 Stage70 — guarded complete property edits (local verified; native VM Deferred)

Use the next available numeric section at additive installation, retaining every earlier section/evidence byte. The private two branches have no new include/facade/parser. Full-token IDs and finite software decimals are read before the selected-row edit.

The argument counts including argv0 are7 and6. Source must be a complete
valid current Level1..11. IDs are full-token nonzero unsigned decimal: actor
UInt64 and path UInt32. Offsets use the existing finite ASCII software FloatText
parser, including preserved signed zero; no native float intermediate is used.

edit-offset changes only the selected body's geometry offset. Its pose, shape,
half/layers/mask/role, path binding, all waypoint offsets and absolute respawn
spawns remain. A bound path still starts at actor.position, never body.offset.
The represented initial geometry can become invalid or overlap other solids;
actual full validation decides before any output is created. Preview shows exact
software endpoints of that new initial geometry, not a current checkpoint.

edit-playback changes only one existing immutable path playback field, for every
sharing actor. It preserves path ID/count/points/travel/dwell/movingTrigger role,
all bindings and full permission tables, including an already authored Path2.
Once/Loop/PingPong are the actual existing Path modes; Loop includes its real
last-to-first leg rather than a promised teleport. This does not execute a path,
restart/import its cursor, authorize Actions or prove future sampled reachability.

Format/application/input/step and all unrelated content remain explicit. A real
fresh Session/manifest validates the edited complete Level and canonical encoding
finishes before exclusive fresh output. Changed content resolves a new full
contentHash/manifest, not old-checkpoint compatibility or live hot reload.
Existing64-body/8-path/24-point,64KiB text/8KiB semantic and joint Session limits
still apply; no table size or capacity is upgraded. Normal validation/argument
rejection creates no output. Physical I/O failure can leave a partial new file.
No core/wire/API module or capability entry is added. Actual local MSVC Release AYEntity_LevelToolChecks passed in 6.84019s; the standard tool build completed in 6.141s. Eight successful property CLI calls and eleven normal refusals checked selected independent software geometry/offset fields, path sharing and playback edits, complete unselected source policy/actors, canonical stability and protected normal refusals were checked by that finite offline gate. Source snapshots, the executable, raw CLI outputs and genuine JUnit/LastTest log are bound in out/detcontent36-40-20261011/stage70-actual-freeze.json. The document reader executes no product. No Sim tick, current checkpoint, Host/path movement, restart/reachability, unknown application or historical authentication is implied. No new capability, entry, module, wire/profile or test target is added; native final VM Deferred.
