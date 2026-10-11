# Deterministic level authoring tool

Build `AYEntity_DetLevelTool` in the root or portable checks project. Its executable
is `aydetlevel`. The same public Level decoder, immutable configuration and fresh
Session seal used by the runtime validate every command; this is not a separate
approximate parser.

```text
aydetlevel validate AYRuntime/AYEntity/examples/detlevel/terrain.dlt
aydetlevel inspect AYRuntime/AYEntity/examples/detinteraction/interaction.dlt
aydetlevel canonicalize edited.dlt fresh-canonical.dlt
```

`validate` and `inspect` emit a JSON receipt containing format/session versions,
the rational step, content identity, actor/path/target/rule counts, stable actor
IDs, exact position bits, roles and complete rule routing/timers. UInt64 IDs and
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
than silently discarding them. Registration ranges remain an application choice
in `DetLevel2DOptions`, rather than arbitrary gameplay-authored system IDs.

This tool does not import a Scene/Tilemap, round native decimal values, hot reload
a running Session, migrate a save or cook/package an asset. Resource loading and
authoring stay outside simulation ticks. See
[the execution record](../../../../AYDocs/DETERMINISTIC-CONTENT-TEN-HOUR-20261011.md)
for actual implementation and necessary local acceptance status.
