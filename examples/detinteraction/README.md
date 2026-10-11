# Deterministic interactive level (Stage35)

This console example uses the standard `EngineModuleRuntime` and module-owned
`DeterminismHost`. It loads the existing `.dlt` format and binds public Actions
and Interaction policies. All authority remains typed Session state. It has no
graphical renderer; the console reports the actual committed player position,
door, platform, collectible and last action result.

Build `AYEntity_DetInteractionExample` in the root project with `BUILD_TESTING=ON`,
or opt in with `AY_ENTITY_BUILD_DET_INTERACTION_EXAMPLE=ON`. From the root:

```text
AYEntity_DetInteractionExample --level AYRuntime/AYEntity/examples/detinteraction/interaction.dlt --interactive --ticks 512
```

Use `right 44`, `left 20`, `idle 8`, or `jump`. Each command advances that many
fixed ticks, with a one-tick default. Movement direction persists during `open`,
`close`, `pause`, `resume`, `enable`, `disable` and `restart`; these mechanism
commands act once and advance one tick. `status` and `help` do not tick.
`save` captures an in-memory Live checkpoint; `load` restores it at a frame
boundary and resumes. Record does not allow rewriting history. `quit` or EOF
ends the current session. Input is read before Host frames and encoded into
canonical tick commands; the simulation callbacks never read stdin/devices.

The sample's plate200 opens door110 after2 ticks. Leaving it pauses platform130;
entering plate210 resumes the platform after2 ticks. Collectible220 disables its
trigger once on Enter. Its subsequent Exit schedules door closure after8 ticks.
The platform uses the existing integer-tick path; collision remains the sole
pose writer. The platform is on a separate collision layer in this sample so
mechanism controls can be inspected while the player walks through the puzzle.

Collision controls set `mask=0` or the immutable enabled mask; body mode and path
binding remain intact. Closing onto an overlapping solid returns `Occupied` and
leaves the gate open. Restart away from the path origin returns
`RestartAwayFromOrigin`, preserving its phase. These are ordinary results, not
Session faults. Callers explicitly choose whether and when to retry. Once is
consumed on scheduling, including a subsequently denied action.

## Short scripted acceptance and replay

Without `--interactive`, the example walks right for160 ticks, attempts a path
restart at44 and closes the occupied door at88. The registered observer counts
actual door opening/closing, path pause/resume, collection and both denials.

```text
AYEntity_DetInteractionExample --level interaction.dlt --ticks 160 --require-features --check-restore --output freshLive
AYEntity_DetInteractionExample --level interaction.dlt --ticks 160 --require-features --record fresh.rpl --output freshRecord
AYEntity_DetInteractionExample --level interaction.dlt --ticks 160 --require-features --replay actual_000.rpl --seek 64 --output freshSeek
```

Paths must be printable ASCII. All output/record destinations must be fresh.
Read the actual recorder destination from `recordingPath` in the summary.
The outputs are complete `.state`, per-tick `.trace` and `.summary.json`.
Replay uses recorded packets and never invokes live input acquisition.
`--check-restore` compares complete checkpoint bytes across Live32→64 rewind.
Interactive restores can produce repeated tick numbers in a chronological trace.

`AYEntity_InteractionHostExample` runs five short processes: Live+restore,
Record, Replay, Seek64 and actual stdin commands with save/load. It compares
complete final checkpoint bytes across all five, Record/Replay per-tick hashes,
and the Seek suffix. Necessary local results are recorded in
[Stage33–35](../../../../AYDocs/DETERMINISTIC-INTERACTION-STAGE33-35.md).

No new file format, rule hot reload, arbitrary callback action, persistent
inventory, visual input binding or editor/AYResource/cook integration is implied.
Rule/target configuration is immutable C++ policy with full manifest identity.
