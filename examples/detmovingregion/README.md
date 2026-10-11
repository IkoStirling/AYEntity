# Authored moving hazard and independent path controls

Stage50 loads `content.dlt` version7 through public Level acquisition. It uses the
real EngineModuleRuntime, Entity runtime, selected Host Scene and fixed-step Host.
Immutable input is captured before the frame; Sim runs canonical input→Actions→
Vitals/RespawnFlow→Path/Character→Collision→registered post observations. Presentation
reads committed copied state; no filesystem operation, mutable cursor or second
pose/tick owner is inside simulation.

The source authors an AABB MovingObstacle trigger path (actor200, Path2), a stationary
treatment trigger (210), two characters and floor. The moving hazard starts atx−2,
travels offset4 in8 ticks each PingPong leg with dt1/64. It is not a solid platform
and cannot carry the characters. Actions3 controls pause/resume independently of
mask off/on; mask0 does not freeze path phase. The default16tick script schedules
pause at3, resume5, mask off11/on13. Vitality consumes actual previous collision
Enter/Stay/Exit, never independent polling. Authored expected counters in
`--require-features` are runtime assertions, not a prior acceptance claim.

Build `AYEntity_DetMovingRegionExample` from the configured standard root project.
Run from the repository root with an existing output parent and fresh paths:

```powershell
AYEntity_DetMovingRegionExample --level AYRuntime/AYEntity/examples/detmovingregion/content.dlt --ticks 16 --require-features --output OUT/live --save-at 4 --save OUT/boundary4.dsv
AYEntity_DetMovingRegionExample --level AYRuntime/AYEntity/examples/detmovingregion/content.dlt --ticks 16 --load OUT/boundary4.dsv --output OUT/load
AYEntity_DetMovingRegionExample --level AYRuntime/AYEntity/examples/detmovingregion/content.dlt --ticks 16 --record OUT/moving.rpl --output OUT/record
```

Use `record.summary.json`'s actual `recordingPath` for `--replay FILE --output PREFIX`;
add `--seek 4` to replay from the saved logical boundary. Recorder segment naming is
not inferred from the requested base name. `--interactive` accepts right/left/idle
with optional tick counts, jump/drop/crouch/stand, respawn, hurt/heal amounts, pause,
resume, off, on, status/help/quit and Live-only `save FRESH.dsv`/`load FILE.dsv`.
Save/load run outside Sim; invalid loads preserve authority, and successful restore
resumes through the existing Host lifecycle. Record/Replay prohibit save/load rewinds.
Printable ASCII file paths and fresh outputs are the example's actual boundary.

`PREFIX.state` is complete canonical checkpoint bytes; `PREFIX.trace` records each
actual committed tick/hash, including repeated ticks after interactive load;
`PREFIX.summary.json` contains tick/callback/file lifecycle and copied path/action/
HP/region/recovery observations. Hashes identify rows, not authentication or full-byte
goldens. The module gate `AYEntity_MovingRegionHostExample` is designed to compare
Live/Record/Replay full state/trace, Load/Seek suffixes and actual stdin save/load.

Necessary actual acceptance: six initial local core gates passed; retain the first combined tool/Host actor-only descriptor failure. Corrected tool/Host two gates passed with unchanged source pins (CTest3.19s). The six16tick Host paths compare complete state/trace/load/seek/stdin save bytes. Actual hero Hurt attempts3/6/14 leftHP14/death0; treatment made8 real attempts and saturatedHP6; four actions preserved pause3/resume5 independently of mask off11/on13, final path elapsed6. Independent authored/resource gates remain separately pending. Native Deferred to Final VM; no new long-run or remote CI claim.
