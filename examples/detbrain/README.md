# Authored deterministic enemy Host example

**Version:** 0.1.0
**Date:** 2026-10-12
**Status:** Active
**Owner:** AYEntity deterministic content maintainers
**Related:** [Brain header](../../include/AYEntity/DeterministicBrain2D.h) · [Level header](../../include/AYEntity/DeterministicLevel2D.h) · [Module design](../../design.md)

## §1 Build and acquisition

Enable `AY_ENTITY_BUILD_DET_BRAIN_EXAMPLE=ON` in an already supported standard
engine build (default OFF), then build `AYEntity_DetBrainExample`. This example
uses EngineModuleRuntime, the Entity-owned standard Host/Scene and immutable
[Level9 content](content.dlt), not a Unitfixture Session or a second tick loop.
The existing Level load/configure recipe installs Character, HP, Attack and Brain;
resolve exact getters, never install those dependencies twice. Actual local
Stage54 necessary local runtime acceptance passed; native final VM remains Deferred.

From the repository root, using a built executable and fresh paths:

```text
AYEntity_DetBrainExample --level AYRuntime/AYEntity/examples/detbrain/content.dlt --ticks 64 --require-features --output fresh-live --save-at 38 --save fresh38.dsv
AYEntity_DetBrainExample --level AYRuntime/AYEntity/examples/detbrain/content.dlt --ticks 64 --require-features --load fresh38.dsv --output fresh-load
AYEntity_DetBrainExample --level AYRuntime/AYEntity/examples/detbrain/content.dlt --ticks 64 --require-features --record fresh-record.rpl --output fresh-record
AYEntity_DetBrainExample --level AYRuntime/AYEntity/examples/detbrain/content.dlt --ticks 64 --interactive --output fresh-player
```

Use the recorder's returned `recordingPath` for `--replay ACTUAL_PATH`; optional
`--seek 38` continues that archive. Do not guess its physical segment filename.
Output creates `PREFIX.state`, `.trace` and `.summary.json`; all must be fresh,
paths printable ASCII. Full state is the canonical checkpoint, hashes are
diagnostic identities rather than independent state goldens or authentication.

## §2 Player and Enemy input ownership

App54/inputVersion6, step1/64 and helper logic80 bind six bytes at input system4:
speed+2, jump, crouch, drop, HeroHeld and HeroFacing. Only Hero10 receives external
input. Enemy20 has authored Brain3 fixed Hero target and remains enabled; no
Manual respawn or EnemyHeld byte/command exists. Order is Brain3 -> player4 ->
Attack5 -> Vitals6 -> Character/Collision -> presentation. Pre-frame polling
encodes canonical input; presentation reads copied committed state.

Default64tick content executes initial Enemy patrol, then center-sensed chase and
prepared attack; Hero jump38 is a real physical miss, later attacks damage and
kill Enemy. Brain Dead then releases desired motion/held, while the controller
still owns braking/gravity/carrier and collision owns pose. There is no navigation,
line of sight, automatic target search or direct HP/pose write. The primary target
selects sensing/facing only; Attack's complete whitelist remains damage authority.

Interactive commands are `right/left/idle [ticks]`, `jump`, `drop`, `attackright`,
`attackleft`, `faceleft/faceright`, `crouch/stand [ticks]`, `status`, `save FRESH.dsv`,
`load FILE.dsv`, `help`, `quit`. Movement/facing persist; attack/jump/drop pulses
release on later samples. They affect Hero only and never bypass canonical input.

## §3 Save, replay and precise observation scope

Default boundary38 retains the Enemy's real prepared warmup. Live save/load is
outside Sim and uses complete persistent checkpoint decode plus matching Host
controller restore/resume, reacquiring copies afterward. Record cannot rewind;
Replay accepts no live input. Interactive load retains actually observed repeated
tick rows rather than concealing them. Dead intent is zero desired speed, not an
instant physical freeze. Brain observedAttackProcessedTick/phase are from before
current Attack/HP/motion, not the later checkpoint phase.

The short `AYEntity_BrainHostExample` actually passed six paths in11.3003s: Live
save38, new process load38, Record, Replay, seek38 and stdin save/load. Complete
CP/trace/savebytes agree:64full rows,26load/seek rows,68stdin rows (including4
revisited), suffix index42 of26; Replay uses0input callbacks. `--require-features`
passed selected hand patrol/chase/window/jump miss/Hurt/death behavior. Final Hero
HP6/hurts1/attacks3/attempts3; EnemyHP0/dead/hurts3/attacks3/attempt1/cancel1,
Brain64decisions/3pulses/Dead. FinalHash9235648105896247954/contentHash2553473865409833715
are diagnostics; complete actual byte equality is execution equivalence, not an
independent state golden or authentication. Stage53 standalone core remains
separate evidence; this no-recovery content does not cover its unmatched branch.

## §4 Current acceptance

Necessary local Release seven gates passed: Level9 1.24213s, old8 .476128s, old7
.740938s, Cooked .18782s, standard Host11.3003s, CLI1.19754s and existing Resource
.986975s. Level9 summary16ticks/17CP/16hand rows/13CPcomparisons/28rejects includes
actual new text/cooked roundtrip; Resource is old-sample loose/ProjectBuild-Pak/
CookShip-Pak compatibility, not newly packaged Enemy content. Result/source
snapshots stage54-levels1/host1 are unchanged with no production behavior fix.
Actual six-path artifacts are under brain-host-example/run-20261011T205912-7efc9f2fb7dc
in the accepted standard-release build. No new Debug/ASan/backend matrix, long-run,
network transport or native VM result; final native VM Deferred.
