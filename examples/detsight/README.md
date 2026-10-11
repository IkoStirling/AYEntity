# Authored sight and action-controlled door Host example

**Version:** 0.1.0
**Date:** 2026-10-12
**Status:** Active
**Owner:** AYEntity deterministic content maintainers
**Related:** [Level header](../../include/AYEntity/DeterministicLevel2D.h) · [Brain header](../../include/AYEntity/DeterministicBrain2D.h) · [Module design](../../design.md)

## §1 Build and acquisition

Enable `AY_ENTITY_BUILD_DET_SIGHT_EXAMPLE=ON` in an already supported standard
engine build (default OFF), then build `AYEntity_DetSightExample`. It uses real
EngineModuleRuntime, standard Entity Host/Scene and immutable [Level11 content](content.dlt).
The normal load -> detLevelSessionConfig -> configureDetLevel2D recipe installs
matching Character, Actions, dedicated HP, Attack and Brain2; resolve its exact
getters and never install a second facade or tick owner. Necessary local runtime acceptance
passed; final native VM remains Deferred.

Use the built executable from the repository root with fresh printable ASCII paths:

```text
AYEntity_DetSightExample --level AYRuntime/AYEntity/examples/detsight/content.dlt --ticks 64 --require-features --output fresh-live --save-at 9 --save fresh9.dsv
AYEntity_DetSightExample --level AYRuntime/AYEntity/examples/detsight/content.dlt --ticks 64 --require-features --load fresh9.dsv --output fresh-load
AYEntity_DetSightExample --level AYRuntime/AYEntity/examples/detsight/content.dlt --ticks 64 --require-features --record fresh-record.rpl --output fresh-record
AYEntity_DetSightExample --level AYRuntime/AYEntity/examples/detsight/content.dlt --ticks 64 --interactive --output fresh-console
```

Use the recorder's returned `recordingPath` with `--replay ACTUAL_PATH`; optional
`--seek 9` continues that archive. Do not guess a segment filename. `--output`
creates fresh `PREFIX.state`, `.trace` and `.summary.json`: full canonical final
CP, per-row nextTick/hash and copied diagnostics. Hashes are UInt64 strings in
JSON, not independent CP goldens or authentication. Existing output/save/record
targets are protected; physical failure can retain a partial fresh file.

## §2 Canonical input and real authority

App59/inputVersion8, step1/64, type8/source10/sequence0 and logic83 bind one full
bool door-open byte0/1. External acquisition occurs before each Host frame;
input4 executes actual authorized CollisionEnable/Disable for door101, mask1/0.
No Hero movement, jump, Manual respawn or Hero Attack command is mapped. Enemy20
is owned by Brain3; real Attack5 and HP6 keep their complete authored authority.
The application callback writes no pose/body/HP or Character/Attack intent.
Console commands are `closed/open/idle [ticks]`, `status`, `save FRESH.dsv`,
`load FILE.dsv`, `help`, `quit`; open/closed state persists as an explicit full
canonical sample, and idle resubmits it. Presentation reads committed copies.

The sample's dyadic bodies stand on reciprocal solid floor100. Door101 is a
real solid occluder between their pose centers, not a synthetic visibility flag.
Both desired patrol/chase speeds are zero; real controller/collision behavior
still executes. The scripted sample is closed at0..7 and open from8 onward.
Brain3 runs before input4: it still captures the closed door at tick8, then the
registered action opens it. The actual selected independent hand sequence is blocked
0..8, first idle request9, subsequent finite opportunities9+9k and actual Hurt
at12+9k. Final64 expectations are HeroHP88/hurts6, EnemyHP100, requests7,
attacks7/attempts6/cancel0 and Warmup. These finite selected checks passed in all
six actual Host processes; they are not independent whole-CP goldens. Sight never wall-filters or cancels an
already launched Attack; the real melee bounds/HP permission determine damage.

## §3 Save/restore and historical observations

CP9 has current door mask0 but the recorded Brain decision is SightBlocked from
the earlier capture. Both facts are valid; pure restore verifies registered
closure, not historical ray authenticity, and does not reraycast current geometry
to replace that observation. The next decision captures the now-open scene.

Live file I/O is outside Sim. Full save decode and matching Host restore/resume
reacquire copied observations; initial load failure exits, interactive rejection
preserves prior running/pause behavior. Record cannot rewind or load a save;
Replay acquires no live input. Actual interactive repeated rows remain in trace.
The focused gate actually passed six paths: Live64/save9, fresh load9 (55 continuation rows),
Record64, Replay64, seek9 (55 rows), and real stdin68 rows including4 revisits.
The stdin recipe is closed8 -> open -> save9 -> idle4 -> load9 -> idle55; compare
its suffix at index13 with the55-row full-run suffix. Full CP/save byte equality
and per-tick traces show those executions' closure, not an independent whole-CP
golden, historical visibility proof or executable authentication.

## §4 Current acceptance and limits

Necessary local MSVC Release seven unique cases passed: Level11 1.10249s,
old CharacterControl10 .204423s/MovingTrigger7 .69038s/Cooked .115796s, actual
SightHost10.4039s, CLI1.85419s and existing Resource1.06179s. New authored fixture
is8ticks/9CP,16selected hand rows/23advances/9actual CP comparisons/16rejects,
real file/text/cooked and restore continuation. Host Live/Record/Replay64rows,
load/seek55rows and stdin68rows (suffix13 of55) passed with six entire final CPs,
twoSave9 bytes and full/suffix traces equal; Replay had0live input callbacks.
FinalHash16680796770313425061/content2906767469645805791 are diagnostic UInt64
strings. Final HeroHP88/hurts6, EnemyHP100/attacks7/attempts6/cancel0,
Brain64decisions/7pulses/AttackRequested(mode3) matched the selected hand checks.
Actual artifacts: out/detcharacter23-20261011/standard-release/sight-host-example/
run-20261011T221905-23c4a9e9ae70; exact source/exe/JUnit/input/state/trace/save
records are bound by out/detcontent36-40-20261011/stage59-actual-freeze.json.

First levels1 harness named unknown AYEntity_LevelControl10Checks and failed
before product tests; corrected existing CharacterControl10 target then passed.
First host1 is still overall Failed: the fixture source mistakenly used `action 101`
instead of the established `target 101` grammar, which stopped Host/CLI preflight.
Only that source literal was repaired; production/hand checks were unchanged,
original OUT and raw failed results retained. Resource had genuinely passed in
host1 and was not repeated. Sources match before/after each batch, while this
fixture content changed between batches, so cross-batch full snapshot equality
is not claimed. Stage58 direct sight/default-file evidence stays separate.
Existing Resource is old-sample compatibility, not a separately packaged
Level11 scene. No Debug/ASan/backend matrix/long/native/VM execution is claimed. No navigation,
FOV, automatic target selection or launched-Attack occlusion is implemented.
Old formats1..10 false retain defaults/canonical semantics; Level11 explicitly
selects the already implemented Brain1/2 profiles. The ten-hour content task is
Active; final native VM Deferred.
