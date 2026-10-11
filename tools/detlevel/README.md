# Deterministic level authoring tool

Build `AYEntity_DetLevelTool` in the root or portable checks project. Its executable
is `aydetlevel`. The same public Level decoder, immutable configuration and fresh
Session seal used by the runtime validate every command; this is not a separate
approximate parser.

```text
aydetlevel validate AYRuntime/AYEntity/examples/detlevel/terrain.dlt
aydetlevel inspect AYRuntime/AYEntity/examples/detinteraction/interaction.dlt
aydetlevel canonicalize edited.dlt fresh-canonical.dlt
aydetlevel float 0.1
```

`validate` and `inspect` emit a JSON receipt containing format/session versions,
the rational step, content identity, actor/path/target/rule counts, stable actor
IDs, exact position bits, roles, complete rule routing/timers, counters, objectives,
claims, claim permissions and objective actions. UInt64 IDs, counter values and
hashes are JSON strings so a JavaScript consumer cannot round them. Authoritative
Float32 values remain exact eight-digit hexadecimal software binary32 bits.

`canonicalize` writes a fresh file after full validation, preserving every
authoritative field and its explicit format version. It normalizes whitespace and
hexadecimal spelling. It never modifies or replaces the input or an existing
output; an I/O failure may leave a partial new file for explicit handling.
All commands have a zero exit on success and a nonzero exit with a JSON diagnostic
on argument, file, parse, reference, geometry, policy or budget errors.

Format1 contains session/collision/character, paths, actors and `end`. Format2
adds these sections between actors and `end`:

```text
actions <per-tick-budget> <target-count>
target <actor-id> <enabled-mask> <collision-control-0-or-1> <path-id-or-0>
rules <rule-count>
rule <id> <trigger-id> <actor-id> <phase> <target-id> <kind> <once> <cooldown> <delay>
```

Target IDs and rule IDs are strictly increasing. A nonzero target path is that
same actor's authored path binding. Phase1 is Enter and phase3 Exit; Stay is not
an authored rule. Kinds1..7 are collision enable/disable, path enable/disable,
pause/resume/restart. Once is consumed on scheduling, even if its later action is
normally refused. Cooldown/delay are logical tick counts0..65535. The collision
event arrives the next tick; delay starts when that event is processed. A blocked
close or restart away from origin leaves the target unchanged, without an
implicit retry.

Start from a shipped complete `.dlt`, edit its content, run `validate`, inspect
the result, then canonicalize to a fresh destination. Format2 can contain an
Actions table with zero rules, or both empty. Format1 rejects new fields rather
than silently discarding them. Format3 additionally authors complete bounded Progress
and Flow tables after `rules`:

```text
progress <counter-count> <objective-count> <claim-count>
counter <id> <minimum> <maximum> <initial>
objective <id> <condition-count>
condition <counter-id> <at-least>
claim <id> <trigger-id> <actor-id> <counter-id> <amount>
flow <binding-count> <objective-action-count>
binding <claim-id> <required-objective-or-0>
objectiveaction <id> <objective-id> <action-target-id> <kind> <once>
```

Tables use increasing nonzero IDs and closed references. An objective combines1..8
conditions with AND. Claims need actual Enter and successful collision disable;
only explicit bindings collect automatically. A closed permission or ordinary
denial needs a new Enter. Objective actions execute on false-to-true edges after
claims, with no implicit retry; once is consumed even on denial. Pure counters may
use a zero-target Actions authority. No counters requires all Progress/Flow tables
empty. Individual table maxima do not guarantee that their combined semantic bytes,
policy chunks and64-schema Session budget fit; `validate` performs real preflight.

`float` uses AYMath's bounded, pure-integer nearest-even decimal parser and returns
exact binary32 bits plus an exact decimal spelling that roundtrips those bits.
For example, `0.1` becomes `3dcccccd`; authored `.dlt` still persists these bits.
Nonfinite, overflow or out-of-budget input is refused with a JSON diagnostic.

Registration ranges remain an application choice
in `DetLevel2DOptions`, rather than arbitrary gameplay-authored system IDs.

Format4 retains every format3 table, adds `characterRespawn` as the final collision
row boolean, and appends before `end`:

```text
respawn <spawn-count> <actor-default-count> <region-count>
spawn <id> <actor-id> <x-bits> <y-bits>
defaultspawn <actor-id> <spawn-id>
region <id> <trigger-id> <actor-id> <kind> <spawn-id>
```

Spawn/default/region limits are8/8/16 and references are actor-specific. Kind0 is
Checkpoint and names a spawn; kind1 is Death and requires spawn0. Death uses the
active checkpoint after all delivered checkpoint Enter events are selected. A
simultaneous death wins over the one-tick manual flag. Each actor attempts at most
one request in a processing tick. Normal denial has no hidden retry; Stay does
not request again. Accepted endpoint Enter is an actual event on the next tick.
Respawn requires advanced/extended/one-way policy and at most55 trigger pairs;
false requires all three new tables empty. Formats1..3 reject the new flag/tables
and keep original canonical bytes/hash. Validate tests the actual combined Session
budget. Start from [the checkpoint example](../../examples/detcheckpoint/content.dlt).
`inspect` returns complete spawn/default/region tables plus their counts and opt-in.

Format5 retains the format4 grammar and adds the following before `end`:

```text
vitality <max-operations> <respawn-recovery> <health-count> <permission-count> <region-count>
health <actor> <counter> <maximum-hp> <initial-hp> <invulnerability-ticks> <restored-hp> <recover-manual>
vitalitypermission <id> <actor> <kind> <max-amount>
vitalityregion <id> <trigger> <actor> <kind> <amount>
```

Limits are8 health actors/16 direct permissions/16 region bindings, with sorted
nonzero IDs, dedicated min0/max>0 HP counters and positive amounts. Kind1 is Hurt,
kind2 Heal. Nonlethal Hurt protects[t,t+N); Heal does not revive dead HP. Recovery
off requires restoredHP0 and recoverManualfalse. Recovery on requires an actor's
authored Respawn binding and restoredHP in[1,max]; manual restoration is an explicit
per-actor boolean. HP-only content can use the existing zero-target Actions policy.
HP counters are a separate namespace from gameplay counters/objectives/claims.

Recovery derives an actual Vitality-system external-death permission. A lethal
transition is flushed once before RespawnFlow. Only a current selected queued
route and matching actual Accepted tick/spawn can restore HP after motion; normal
denial leaves HP0 and never retries automatically. Equal-HP recovery counts as a
restoration without a numerical write. `inspect` returns all health/permission/
region tables and policy flags, preserving full UInt64 values as JSON strings.
Authoring validates the actual combined policy/schema budgets; table maxima do
not guarantee that all features fit together. Formats1..4 keep their original
canonical bytes/hash and reject nondefault health content. Start with the
[vitality content](../../examples/detvitality/content.dlt); local Level5 CLI acceptance passed0.735695s; native final VM remains deferred.

This tool does not import a Scene/Tilemap, use native float parsing, hot reload
a running Session, migrate a save or cook/package an asset. Resource loading and
authoring stay outside simulation ticks. See
[the execution record](../../../../AYDocs/DETERMINISTIC-CONTENT-TEN-HOUR-20261011.md)
for actual implementation and necessary local acceptance status.


## Stage49: real periodic Level6

Format6 changes only the vitality authoring lines after the existing tables:

```text
vitality <max-operations> <respawn-recovery> <periodic-regions> <health-count> <permission-count> <region-count>
vitalityregion <id> <trigger> <actor> <kind> <amount> <period-ticks>
```

Period0 retains Enter-only;1..65535 repeats on due real Stay after real Enter.
Exit clears the episode, reentry starts afresh, normal denials consume the interval.
No independent polling or wall clock. Inspect exposes both boolean and intervals;
canonicalize preserves complete identity. Old1..5 keep bytes/hash and reject new
authority. Start with `examples/detperiodic/content.dlt`; actual short CLI passed.

## Stage50: explicit moving-trigger Level7 paths

Format7 adds one explicit field per path, before the count:

```text
path <id> <playback> <moving-trigger> <waypoint-count>
```

True requires a used AABB MovingObstacle trigger body, no one-way role.
False retains ordinary Path1 bindings. Inspect exposes `movingTrigger` for every
path; validation resolves actual Path1/2 and authorized Actions1/2/3. Canonicalize
preserves version7/full fields and never silently upgrades1..6. Old formats reject
nondefault flags and retain their canonical bytes/hash.

See [moving-region source](../../examples/detmovingregion/content.dlt) and its
[standard Host recipe](../../examples/detmovingregion/README.md). Initial validity
does not execute effects or prove future sampled motion. The first actual combined
CLI/Host attempt exposed an actor-only Path descriptor acquisition defect, now
corrected. The actual corrected tool/Host pair passed (CTest3.19s); old1..6 canonical entries and explicit new role inspection remained valid. Additional authored/resource acceptance is separate. Native final VM deferred.


## Stage52: Level8 finite attack content

Format8 preserves every Level7 section and adds these rows after vitality and before end:

```text
attacks <cancelOnRecovery:0|1> <ownerCount:u32>
attack <owner:u64> <halfX:bits8> <halfY:bits8> <offsetX:bits8> <offsetY:bits8> <warmup:u32> <active:u32> <recovery:u32> <targetCount:u32>
attacktarget <target:u64> <hurtPermission:u32> <amount:u64>
```

Each owner is followed by exactly targetCount rows. IDs are strictly ordered nonzero;
targets are not self, belong to the same bounded HP table and bind actual positive Hurt
permission/amount. World attack offset mirrors only X by facing. Warmup/recovery0..65535,
active1..65535; Float32 uses finite exact eight-hex bits. Max8 owners/8 listed targets
still share at most8 HP actors, hence max7 other targets. Empty owners require default
cancel=true. Old1..7 reject nondefault attacks and retain their canonical/text/hash.

Inspect returns attackOwnerCount/attackCancelOnRecovery and full attacks rows with
actor, halfExtentBits/offsetBits, three tick windows and target actor/permission/amount;
UInt64 actors/amounts are JSON strings. Validate and canonicalize use public Level/
Attack factories and actual fresh Session seal. Canonicalize preserves version8, full
fields and content identity into a fresh output. Combined64KiB text/8KiB semantics,
actual schema and nested policy capacities remain authoritative.

Start with [attack content](../../examples/detattack/content.dlt) and its
[standard Host](../../examples/detattack/README.md). Offline preflight does not run
attacks, future movement or saves and does not certify historical gameplay. Existing
cooked envelope1 carries the actual canonical Level8 text without a codec upgrade.
Necessary actual Release CLI passed1.06216s with complete Level8 tables and old1..7
canonical paths. Authoring0.463677s also exercised Level8 actual text/cooked roundtrip.
Existing Resource loose/ProjectBuild-Pak/CookShip-Pak compatibility passed0.975352s
(27CP/14rejects); the new attack content was not separately packaged/run through Pak.
No runtime fix, first diagnostic-free build failure preserved, native final VM deferred.

## Stage54: Level9 fixed-target enemy authoring

Format9 follows the complete Attack table with:

```text
brains <count:u32>
brain <actor:u64> <target:u64> <left:bits8> <right:bits8> <patrolSpeed:bits8> <chaseSpeed:bits8> <senseX:bits8> <senseY:bits8> <attackDistance:bits8> <initialRight:0|1>
```

Rows are stable actor-ID-sorted enabled solid Kinematic Character/HP/Attack owners;
the nonself primary target is already Attack-whitelisted. Counts/bounds/speeds/
sensing/roles and full nested policies use actual Brain preparation. Empty rows
install no Brain. Old1..8 reject nondefault rows, preserve old writer/semantic/hash
branches, and are never silently upgraded. Original64KiB/semantic8KiB/joint schema
budgets remain; maximum individual tables cannot all be assumed to fit.

Inspect adds `brainOwnerCount` and full `brains`: actor/target string IDs,
patrolLeftBits/patrolRightBits, patrolSpeedBits/chaseSpeedBits,
senseHalfExtentBits, attackDistanceBits and initialRight. Canonicalize preserves
explicit format9, complete authority and identity to a fresh destination. Center
sensing is not line of sight or navigation and does not narrow Attack damage targets.

Start from [brain content](../../examples/detbrain/content.dlt), validate, inspect
and canonicalize. Runtime uses the separate [standard Host](../../examples/detbrain/README.md),
where only Hero is externally controlled and Brain owns Enemy. Offline validation
does not execute enemies or establish future outcomes. Actual short Stage54 Release
CLI passed1.19754s, Level9 authoring1.24213s with real text/cooked roundtrip. Old8/
old7/Cooked/Resource compatibility passed, the latter using old samples rather
than new9 Enemy Pak execution. SourcesUnchanged, no production fix, matrix or
long-run; native final VM deferred. See [tool design](design.md) for boundaries.

## Stage56: finite Character Control authoring

Format10 appends after brains:

```text
controls <count:u32>
control <actor:u64> <dashSpeed:bits8> <durationTicks:u32> <cooldownTicks:u32> <groundOnly:0|1> <initialFacingRight:0|1>
```

Count0..8 sorted enabled solid Kinematic advanced Character owners are disjoint
from Brain owners. Exact positive dashSpeed<=maximum, duration1..65535 and
cooldown0..65535 use actual Control policy validation. Inspect exposes
`controlOwnerCount` plus complete `controls` fields: actor decimal string,
dashSpeedBits, durationTicks, cooldownTicks, groundOnly and initialFacingRight.
Canonicalize keeps format10/full identity/fresh output; old1..9 remain exact
branches and refuse new nonempty authority. No phase, input or runtime state is
authored, and Level/Control joint budgets are actual rather than independent sums.

Start with [Control content](../../examples/detcontrol/content.dlt) and validate,
inspect or canonicalize before Sim. The separate [Host](../../examples/detcontrol/README.md)
owns Hero inputs through Control and Enemy through Brain. Actual Stage56 CLI
passed1.37562s, Level10 .223617s with16tick/17CP/16hand/33CP comparison/25reject
and actual text/cooked roundtrip, standard Host six64tick paths13.2032s. Old9/7/
Cooked passed separately; existing Resource1.00207s is old-sample compatibility,
not new10 content Pak combat. Initial fresh seal is static acquisition, not
movement/save proof. New fixture ADL/source-vs-canonical failures and the
diagnostic-free first standard build failure are preserved; production unchanged.
Final native VM deferred, without new matrix or long campaign.

## Stage57: finite bits and editable character/dash starters

```text
aydetlevel float-bits 3FA00000
aydetlevel float-bits 80000000
aydetlevel create character fresh-character.dlt
aydetlevel create dash fresh-dash.dlt
aydetlevel validate fresh-dash.dlt
aydetlevel inspect fresh-dash.dlt
```

`float-bits` takes exactly eight hexadecimal digits, either letter case, without
`0x`, spaces or nonhex characters. It rejects NaN/Inf patterns and returns the
same JSON bits/textProfile/exactDecimal fields: `3FA00000` gives `3fa00000`/`1.25`,
and `80000000` retains `-0`. Existing finite-decimal `float` is unchanged. Finite
subnormals and signed zero use the existing exact software formatter; no native
float conversion or new numeric profile is introduced.

`create character` emits canonical format1. `create dash` emits format10 with
advanced Character, explicit one-way-capable collision and one groundOnly
Control1 owner (dashSpeed8, duration8, cooldown16). Both contain hero10 center(0,1/2), half(1/4,1/2), layer1/mask2,
and Static floor100 center(0,-1/2), half(8,1/2), layer2/mask1. These are exactly
two actors, with no Brain/Attack/HP/rules. Both use app/input1 and step1/64.
The returned inspection describes the created content. Its validity is checked
through actual public Level preparation/fresh Session seal before canonical text
is exclusively written. The caller still needs a matching application input/
Host recipe; these starters are not the existing app56/input7 detcontrol scene.
A groundOnly dash may be denied on the first pre before support is committed.

Output must be fresh and its parent must exist. Unknown kind, invalid content or
invalid arguments do not create output. An existing output is never replaced;
physical write/flush/close failure may leave a partial new file for explicit
handling. There is no automatic cleanup, save migration or crash-durability claim.
All commands run outside Sim; the tool does not provide a new tick/input owner.

Actual Stage57 local Release LevelToolChecks passed1.8485s, and standard Tool
build passed4.953s. The single finite gate exercised reverse1.25/signed-zero and
malformed/nonfinite rejection, both real create/validate/inspect starters/full
canonical byte roundtrips, existing-output and unknown-kind protection. Source
and executable pins match stage57-actual-freeze.json and before/after snapshots
are equal; no production retry/fix. Physical I/O fault injection was not run.
Historical gameplay receipts are separate; native final VM Deferred.

## Stage59: authored sight flag in Level11

Format11 extends the existing Brain header only:

```text
brains <owner-count> <lineOfSight-0-or-1>
brain <existing complete owner fields>
controls <existing control table>
```

True selects actual Brain2, false Brain1; empty owners require false. Earlier
formats1..10 retain false and reject nondefault policy. `inspect` returns
`brainLineOfSight` as a JSON boolean and the full existing Brain owner rows.
The flag is authored policy, never an executed visible/blocked result. Public
decode/prepare/factories/fresh seal validate complete identity; canonical output
preserves version11 and all fields without migration or hidden state.

```text
aydetlevel validate AYRuntime/AYEntity/examples/detsight/content.dlt
aydetlevel inspect AYRuntime/AYEntity/examples/detsight/content.dlt
aydetlevel canonicalize AYRuntime/AYEntity/examples/detsight/content.dlt fresh-sight.dlt
```

The [standard sight Host](../../examples/detsight/README.md) supplies a separate
door-input recipe. Offline authoring does not run rays, actions or gameplay.
Necessary local MSVC Release LevelToolChecks passed1.85419s with true Level11
inspect/canonical output and old branches, while new authoring1.10249s and actual
sight Host10.4039s passed. Seven unique cases include old10/7/Cooked and existing
Resource1.06179s; latter is old-sample compatibility, not new11 Pak runtime.
Complete old Control canonical bytes also match the accepted Stage56 output.
First target-name/fixture-keyword failures remain; only `action 101` -> `target 101`
source grammar was repaired, not production behavior or hand checks. host1
stays overall Failed, its genuine Resource pass was not repeated. Native final VM Deferred.

## Stage60: static authored-content SVG (local checks passed; native Deferred)

```text
aydetlevel preview AYRuntime/AYEntity/examples/detlevel/terrain.dlt fresh-terrain.svg
```

The command validates the entire input through the real Level pipeline, then
shows initial AABBs/filled wedges, trigger/one-way/Character roles, mask0 and
named paths instantiated at each bound actor's initial position. Paths use pose
plus relative waypoint offsets, not body.offset. It never ticks, loads a current
CP or predicts reachable paths, attacks, respawns or Brain decisions.

Output must be fresh with an existing parent. Software geometry/spans must be
finite and the completed SVG must fit1MiB. Exact decimal coordinates preserve
software endpoints; browser layout/pixels are not authoritative. JSON reports
scope authored-initial-content, simulationExecuted false, actorCount,
pathInstances/waypointCount (per binding), svgBytes, worldBoundsBits and
viewBoxBits. The latter is Y-flipped display layout, not new engine state.

Validation/overflow/budget failure creates no SVG; existing files are protected.
Physical write/close failure may retain a new partial file.

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

## Stage61: edit one initial actor (local checks passed; native Deferred)

```text
aydetlevel place source.dlt 10 -1.25 0.5 fresh-placed.dlt
aydetlevel resize-half fresh-placed.dlt 10 0.5 0.5 fresh-resized.dlt
aydetlevel inspect fresh-resized.dlt
```

The command accepts a complete nonzero decimal actor ID and two finite software
decimal values. place changes initial XY only; resize-half changes half extents
only and requires positive values after nearest-even rounding. Signed0 or a
tiny value rounded to0 cannot become a half extent. No locale/native float is used.

All formats supported by the actual decoder keep their explicit version and
every unselected field. Existing actor IDs must be present; real role/geometry/
budget validation can refuse the modified level. A path bound to the moved actor
gets the new initial origin without changing its relative points; absolute
respawn spawn positions do not move. Supply matching content/session/input code,
including the newly reported contentHash; the tool does not invent a Host recipe.

The entire temporary content validates/encodes before exclusive fresh output.
Existing source/output is never replaced; normal rejection creates no file,
while physical write/close failure may leave a partial fresh one. inspect actor
rows add exact halfExtentBits/offsetBits.

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

## Stage63: build ordinary Static obstacles (local verified; native VM Deferred)

```text
aydetlevel create terrain base.dlt
aydetlevel add-solid base.dlt 90 slope-right 4 0.5 1 0.5 2 1 right.dlt
aydetlevel add-solid right.dlt 50 slope-left -4 0.5 1 0.5 2 1 slopes.dlt
aydetlevel add-solid slopes.dlt 200 aabb 7 1 0.5 1 2 1 solids.dlt
aydetlevel place solids.dlt 200 6 1 placed.dlt
aydetlevel resize-half placed.dlt 200 0.5 0.75 resized.dlt
aydetlevel preview resized.dlt layout.svg
```

The exact add-solid interface is source/new-id/shape/X/Y/halfX/halfY/layer/mask/
fresh output (argc12 including argv0). New nonzero UInt64 decimal IDs are checked
for overflow/duplicates and inserted in order. Shape accepts only aabb,
slope-left or slope-right. Layer and mask are full UInt32 decimal; layer0 is
invalid, mask0 is a valid collision-disabled ordinary solid, never Disabled mode.
All four coordinates use actual finite software decimal conversion; positive
half extents must remain positive after software rounding. No native float.

New bodies are Static, nontrigger, non-one-way and not characters or paths,
with zero velocity/offset/ignored reference. For bounds min/max, right-rising
vertices are min,(max.x,min.y),max; left-rising vertices are min,(max.x,min.y),
(min.x,max.y). They are filled solid triangles with real geometry, not stairs
or bounding-box-only slopes. inspect reports exact fields; preview renders
the same authored filled polygon outside Sim, without executing gameplay.

The source must already select staticSlopes for either wedge; absence refuses
output rather than upgrading a policy. create terrain reuses character/floor10/
100 and explicitly enables only staticSlopes: format1/app1/input1/step1/64,
controller5/collision6, nonadvanced/non-one-way. It adds no moving/continuous
terrain, step-up, respawn or gameplay tables. character/dash starters are unchanged.
The caller supplies a matching input/Host recipe; initial validity does not
guarantee future walkability, terrain seams or motion/restore compatibility.

Every source field not selected remains in the canonical full content identity;
there is no implicit source-version migration. Body64/configured lower limits,
text64KiB/canonical8KiB and actual joint namespaces apply. Static-static overlap
keeps existing composition semantics; initial matching ordinary character/solid
interior penetration fails. The full Level validates/seals/encodes before fresh
exclusive output. Normal failure has no output; existing files are protected,
and physical I/O failure may leave a partial new file. No removal/shape replacement,
running-state writer, editor, Tilemap import or durability promise is added.

Actual local MSVC Release AYEntity_LevelToolChecks passed in 3.41s; the standard tool build completed in 6.188s. This finite gate exercised create/add/place/resize/preview, selected dyadic actor bits and actual filled-polygon points, sorted identities, full unselected canonical policy/original actors and canonical stability, legal mask0, a valid 64-body source and 16 protected normal refusals. The recorded source snapshots matched; the actual executable and artifacts are hash-bound. Evidence is out/detcontent36-40-20261011/stage63-actual-freeze.json and stage63-tool1/result.json. No Sim advance, future walkability, whole-checkpoint golden, authentication, new content Pak execution, broad matrix or native VM acceptance is claimed; native final VM remains Deferred. No production/oracle repair or prior gate reuse was needed for this actual invocation.
