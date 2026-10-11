# Authored checkpoint, death and manual respawn content

Enable `AY_ENTITY_BUILD_DET_CHECKPOINT_EXAMPLE` or `BUILD_TESTING`. The executable
`AYEntity_DetCheckpointExample` uses the actual standard Host module lifecycle,
software Float32 movement and the complete `.dlt4` initial-level configuration.
The file retains keyed exit objectives, a collision door, trigger collection and
real translating platform support, and adds an actor-specific checkpoint/death route.

```powershell
AYEntity_DetCheckpointExample --level content.dlt --ticks 128 --require-features
AYEntity_DetCheckpointExample --level content.dlt --ticks 128 --interactive
AYEntity_DetCheckpointExample --level content.dlt --output fresh/live --save-at 64 --save fresh/boundary.dsv
AYEntity_DetCheckpointExample --level content.dlt --output fresh/load --load fresh/boundary.dsv
AYEntity_DetCheckpointExample --level content.dlt --output fresh/record --record fresh/session.rpl
AYEntity_DetCheckpointExample --level content.dlt --output fresh/replay --replay ACTUAL_RECORDING_PATH --seek 64
```

The default script moves right, enters a checkpoint and then a low death strip.
It resumes ordinary support after accepted relocation, jumps over that strip,
collects both keys and finishes through the opened door. Tick104 requests one
manual respawn; subsequent input is idle. A second character stands on the real
platform so pause/resume and support carrying also participate in the same pipeline.
The example's finite statistics observe actual states; the `.dlt` owns permissions,
progress and route policy. No C++ callback fabricates a trigger or outcome.

Interactive commands are `right [ticks]`, `left [ticks]`, `idle [ticks]`, `jump`,
`drop`, `respawn`, `crouch [ticks]`, `stand [ticks]`, `status`, `save FRESH.dsv`,
`load FILE.dsv`, `help` and `quit`. Movement remains until changed. Jump/drop/respawn
are one-tick samples. Save/load run outside Sim in Live mode; all output files are
fresh. `status` includes active spawn and actual checkpoint/death/manual attempt counts.
Queued requests are distinct from actual accepted/denied collision outcomes.

Configure a fresh Session/Host from `detLevelSessionConfig(level)` and call
`configureDetLevel2D` once; it installs the sole Respawn-owned pipeline and routes.
Read `detLevelRespawnConfig2D` / `detLevelRespawnFlowConfig2D` for matching policy.
Do not install a second character/collision facade. Canonical input6 precedes
RespawnFlow7, ProgressFlow8, Interaction9 and all path/character pre systems.
Input source10/type2 contains exactly five bytes: speed+2, jump, crouch, drop, manual.
The complete application/route identity is explicit; replay acquires no live input.

The collision first completes ordinary whole-world movement, then tests the standing
destination. Accepted relocation clears movement/support/windows and interpolation
history; ordinary next-tick pre evaluates real support. An occupied/capacity denial
keeps normal movement and needs a new Enter or new manual pulse. Stay does not retry.
Checkpoint Enter caused by the accepted endpoint is a real event and can select the
same checkpoint again. Keys and objective progress survive relocation.

`AYEntity_CheckpointHostExample` runs six short actual processes: Live/save,
fresh process load, Record, Replay, Seek and stdin save/load. It compares final full
checkpoint bytes, per-tick trace, restored suffixes and the two actual boundary64
save files. This is focused local content validation, without native VM, long soak,
arbitrary Scene authoring, live reload or a general damage/life system.
