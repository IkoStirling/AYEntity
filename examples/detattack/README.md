# Authored attacks in the standard Host

**Version:** 0.1.0
**Date:** 2026-10-12
**Status:** Active
**Owner:** AYEntity

This Stage52 example loads [content.dlt](content.dlt) format8 through the public Level
facade and `detLevelAttackConfig2D`. It uses real EngineModuleRuntime, Entity runtime,
Host Scene and fixed-step owner, with app52/input5/step1/64. Before each frame immutable
input is mapped into one canonical eight-byte packet. Sim runs input4→Attack5→Vitals6
→Flow7→character/collision/post. Presentation reads committed copied state. There is
no portable-only fake Host, filesystem work inside Sim or second pose/tick owner.

The source contains heroes10/20, floor100, dedicated HP counters/permissions and
actor-specific spawns. Hero has W2/A2/R2 attack, enemy W6/A2/R3. The default24tick
script presses hero0/8/14 and enemy0/12, jumps5, and requests manual recovery10. Each
other tick explicitly releases attack buttons. Facing locks when a cycle starts;
future facing changes do not rewrite it. The selected `--require-features` assertions
check two actual hero Hurt attempts, a real enemy jump miss, later enemy Hurt and
one equal-HP accepted Manual restore cancelling an open attack. These are expected
content facts now passed by the short local gate; they are not generic combat coverage.

## Build and run

From a configured standard root build, enable `AY_ENTITY_BUILD_DET_ATTACK_EXAMPLE=ON`
(default OFF), then build `AYEntity_DetAttackExample`. BUILD_TESTING also registers the
`AYEntity_AttackHostExample` harness when the target exists. Run from the repository
root with an existing output parent, fresh files and printable ASCII paths:

```powershell
AYEntity_DetAttackExample --level AYRuntime/AYEntity/examples/detattack/content.dlt --ticks 24 --require-features --output OUT/live --save-at 4 --save OUT/boundary4.dsv
AYEntity_DetAttackExample --level AYRuntime/AYEntity/examples/detattack/content.dlt --ticks 24 --load OUT/boundary4.dsv --output OUT/load
AYEntity_DetAttackExample --level AYRuntime/AYEntity/examples/detattack/content.dlt --ticks 24 --record OUT/attack.rpl --output OUT/record
```

Use the actual `record.summary.json` recordingPath with `--replay FILE --output PREFIX`;
add `--seek 4` for recorded boundary continuation. Never guess the rotated physical
filename from the requested base. Live-only `--load` uses full fresh Session/Host
compatibility validation; timed save requires scripted Live and paired `--save-at N
--save FRESH.dsv`, after the starting checkpoint and within the requested end. Record
stops explicitly to seal; Record/Replay reject persistent save/load rewinds. Replay
acquires no live input. Save decoder validity alone is not application compatibility.

The eight-byte input is:0 speed+2 (0..4),1 jump,2 crouch,3 drop,4 Manual,
5 HeroHeld,6 HeroFacingRight,7 EnemyHeld (enemy faces left). Remaining fields are
booleans. The application mapping is separately registered as logic74/profile1;
packet source10/sequence0/type5 is canonical data, not producer authorization.

## Interactive commands

Run `--interactive --ticks 24 --output OUT/interactive` to acquire actual stdin outside
Sim. Commands: right/left/idle/crouch/stand with an optional tick count; jump, drop,
respawn, attackright, attackleft, duel, enemyattack, faceleft, faceright; status/help/
quit; Live-only `save FRESH.dsv` and `load FILE.dsv`. Movement, crouch and facing persist.
Attack/jump/drop/manual flags are one-tick samples; following ticks explicitly release.
A second held command without an intervening false sample remains held and does not
create a new edge. Busy/cancelled/dead edges have no buffer or retry. `--require-features`
is only appropriate for the exact built-in script or equivalent harness stdin sequence.

Restore runs through Host controller and reacquires committed copies, without keeping
presentation/entity handles. A rejected interactive compatible-lifecycle load preserves
authority and restores the caller's prior resumed state; incompatible CLI starting load
exits rather than manufacturing a continuation. File errors/fresh partial outputs remain
visible; output prefixes and recording/save destinations are never intentionally reused.

## Evidence and limits

`PREFIX.state` holds the complete canonical final checkpoint; `PREFIX.trace` has each
actual committed tick/hash, including repeated ticks after interactive load;
`PREFIX.summary.json` contains exact lifecycle/HP/attack/cancellation/recovery counters,
with content/final hashes as strings. Actual finalHash7319424162791214917 and
contentHash15844624149800906331 identify the passed source. Final heroHP18/hurts1/
attacks3/attempts2/cancels1/restores1/write0; enemyHP14/hurts2/attacks2/attempts1.
Script/record/replay have24trace rows, load/seek20, interactive28; the interactive
load4 continuation begins at rowindex8, Replay has0live input calls.
Hashes identify rows, not authenticity or a full
independent state golden. Boundary4 is within a genuine attack window; the harness
passed six real Live/load/Record/Replay/seek/stdin paths, complete state equality, full
Live/Record/Replay trace equality, load/seek suffixes and scripted/stdin savebytes.

Necessary local Release Host passed6.14051s; Level8 authoring0.463677s (3cases/
29rejects/16handRows/29completeCP comparisons), old70.931608s/Cooked0.180456s and
CLI1.06216s passed. Existing Resource compatibility0.975352s has27CP/14rejects;
this is not a separate new attack-content Pak execution. Source pins were unchanged.
First unchanged Actions TU compile exit1 without diagnostic remains in stage52-host1;
same-source retry passed without runtime changes. Receipts are stage52-levels1 and
stage52-host2 under out/detcontent36-40-20261011. Stage51 core keeps its own scope.
No new long campaign, Debug/ASan matrix,
GitHub CI, remote publication or native final VM execution. Native Deferred to Final VM.
The overarching ten-hour content task remains Active. This is sampled melee overlap,
not sword sweep, wall occlusion, projectile/ranged weapon or ordinary Combat migration.
See [module design](../../design.md) and [SDK contract](../../../../AYDocs/sdk/contracts/2d.md#2dsimulationdeterministic-attack).
