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
