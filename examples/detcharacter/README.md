# Deterministic character: standard Host example

**Version:** 0.2.0
**Date:** 2026-10-11
**Status:** Active
**Acceptance:** Default Stage22 historical local pass; Stage26 Release steps LocalPassed; native deferred to final VM
**Owner:** AYEntity

This executable builds a real `EngineModuleRuntime` graph with
`EntityComponentModule`, `EntityRuntimeModule` and
`DeterministicHostIntegrationModule`. It runs a Play Scene through the standard
Host fixed loop. It includes public engine headers only; it does not include
test fixtures or manually advance a portable Session.

See [Stage22](../../../../AYDocs/DETERMINISTIC-CHARACTER-CONTROLLER-STAGE22.md)
and the [SDK contract](../../../../AYDocs/sdk/contracts/2d.md#2dphysicsdeterministic-character-controller).
The explicit static-stair option is documented in
[Stage26](../../../../AYDocs/DETERMINISTIC-CHARACTER-STEP-UP-STAGE26.md).

## Build

Use a configured root build with Application, Scene and Entity 2D schemas enabled,
the engine's dependency toolchain, and `AY_ENTITY_BUILD_DET_CHARACTER_EXAMPLE=ON`
(or `BUILD_TESTING=ON`). Rendering, native physics and networking are not required
by this example. Build target:

```powershell
cmake --build HOST_BUILD_DIRECTORY --parallel 4 --target AYEntity_DetCharacterExample
```

`HOST_BUILD_DIRECTORY` is your configured root build. The executable links
`AYEntity::DeterminismHost`. With testing enabled, the integration gate is
`AYEntity_CharacterAdvancedHostExample`; it launches independent Live, Record,
Replay and Seek processes and compares their actual witnesses. Commands here
are reproduction recipes, not a statement that this revision passed them.

## Run

Create a fresh, existing ASCII output directory. The default run is Live with
512 simulation ticks at fixed step `1/64`. `--frame-divisor` changes presentation
frame scheduling between `1/64` and `1/128`; simulation policy stays `1/64`.

```powershell
AYEntity_DetCharacterExample --ticks 512 --frame-divisor 128 --output fresh/live
AYEntity_DetCharacterExample --record fresh/walk.rpl --ticks 512 --frame-divisor 64 --output fresh/record
AYEntity_DetCharacterExample --replay fresh/walk_000.rpl --ticks 512 --frame-divisor 128 --output fresh/replay
AYEntity_DetCharacterExample --replay fresh/walk_000.rpl --seek 256 --ticks 512 --frame-divisor 128 --output fresh/seek
```

Record uses the existing file recorder's rotation naming: `walk.rpl` normally
creates `walk_000.rpl`. Use the actual `recordingPath` returned in the summary.
Record base/rotated destinations and all output files must be fresh; existing
files are rejected. No output directories are created or existing files removed.
Replay consumes the recorded canonical commands and never invokes live input
acquisition. `--seek` is Replay-only and names the desired `nextTick`.
`--ticks` is an absolute final `nextTick`, not a count after Seek; Replay must
reach its completion at that tick. Supported range is `1..65536`.

## Input and presentation

The default script uses five separated characters:

| Actor | Scene and sampled input | Observed feature |
|---|---|---|
| 10 | One-way top platform; hold Drop at ticks32..40, then release | Actual ignored support transition and fall to lower floor |
| 11 | Walk off a narrow floor; hold Jump at ticks42..48 | Upward velocity transition during the registered coyote window |
| 12 | Fall toward a floor; hold Jump at ticks18..24 | Upward velocity transition after a registered buffered request |
| 13 | Hold Crouch through tick95, walk under a low ceiling, release | Foot-preserving small shape and blocked standing until clearance |
| 14 | Stand on a horizontal MovingObstacle | Grounded carrier velocity and actual pose movement |

The application polls script samples before each Host frame. `recipe.input`
encodes five versioned four-byte commands from that boundary, one per stable
character ID. System10 validates the commands and calls `detCharacterInput2D`;
controller pre20, collision30 and support post40 run next. Observer50 stores
application-owned statistics and copied public state in typed registered fields.
All Jump/Drop held values are explicitly false outside their sampling intervals.
No gameplay callback edits pose; collision remains the sole XY motion writer.
The sample shapes are dyadic. Crouch/stand applies only when the candidate's
foot minY bits exactly match the current foot and the represented shape has
positive area; otherwise the old stance/shape is kept. This does not promise
exact foot preservation for arbitrary float dimensions, use an ULP repair,
or create a second pose writer.

For custom input, pass `--commands ASCII_FILE` in Live/Record. Each row is:

```text
# tick actor speed jumpHeld crouchHeld dropHeld
0 10 0 0 0 0
32 10 0 0 0 1
41 10 0 0 0 0
```

Speed is an integer in `-2..2` world units/second; buttons are `0` or `1`.
Actors are10..14. Omitted rows explicitly sample neutral for that tick, including
released buttons. Duplicate tick/actor rows, extra tokens and out-of-range values
are rejected before Host startup. Bounds are1MiB/file,256 bytes/line and32768
nonempty rows. Script bytes are input material, not executable instructions.

Presentation prints copied committed positions to stdout every64 ticks. Native
float conversion is used only for that display. It cannot modify registered
state or feed values back into Sim.

## Witness output

`--output PREFIX` creates three fresh files:

- `PREFIX.state`: the complete `encodeDetCheckpoint` bytes at final `nextTick`.
- `PREFIX.trace`: one row per newly executed tick. The first two columns are
  decimal `nextTick` and full checkpoint hash; each actor then supplies ID,
  hexadecimal position X/Y bits, grounded, crouched, support ID and ignored ID.
- `PREFIX.summary.json`: schema1/status`passed`, initialTick, nextTick, traceRows,
  inputCallbacks, finalHash as a decimal **string**, actual recordingPath,
  builtInFeatureChecks and the features object. A failed run writes no passed summary.

Feature keys are `coyoteJumps`, `bufferedJumps`, `drops`, `crouchTicks`,
`blockedStandTicks`, `carryTicks` and `oneWayGroundedTicks`. They observe actual
public controller/body state and registered velocity transitions, rather than
counting requested input. They are application witnesses for this layout, not
an independent oracle or exhaustive certification of the engine algorithms.

Default Live/Record runs ending at or after512 require all seven observations
to be nonzero. Custom input disables this requirement. Replay preserves the
recorded statistics but does not infer which script was used; its
`builtInFeatureChecks` is false. The integration gate explicitly checks the
default recording's counters. Live/Record/Replay full state and trace must match;
Seek256 must match the same final state and the baseline trace suffix for ticks257..512.

CLI syntax errors return2; execution, I/O or failed feature checks return1;
successful runs and `--help` return0. Partial trace/state files from a failed
run are preserved: use another fresh prefix for a new attempt. This file-adapter
example supports printable ASCII paths; complete Unicode path plumbing is not
claimed. It does not install real device input, a renderer or network transport.

## Static stairs: explicit `--scenario steps`

The default and `--scenario advanced` retain the original Stage22 configuration,
registered fields, scene, script and output layout. `--scenario steps` selects an
independent application26/content/logic identity and controller3/collision4:
advanced and one-way support remain explicitly enabled, with
`collision.stepUp=true`, `maxStepHeight=1/2` and pair capacity59. Use the same
scenario for recording and replay; manifests reject an incompatible scene recipe.

```powershell
AYEntity_DetCharacterExample --scenario steps --ticks 512 --frame-divisor 128 --output fresh/steps
AYEntity_DetCharacterExample --scenario steps --record fresh/steps.rpl --ticks 512 --output fresh/step-record
AYEntity_DetCharacterExample --scenario steps --replay fresh/steps_000.rpl --ticks 512 --output fresh/step-replay
```

These are recipes, not execution evidence. The new directed gate
`AYEntity_CharacterStepsChecks` and standard Host gate
`AYEntity_CharacterStepsHostExample` passed the necessary Release checks;
there is no new sustained campaign, cross-configuration matrix or native VM result.

All five characters begin at centerY3/4, half(1/4,1/2), on a floor with top1/4.
The step size is1/64 and all geometry is dyadic:

| Actor | Ordinary static scene / sampled input | Expected application observation |
|---|---|---|
| 10 | Three adjoining width1 stairs at X1/2..7/2, tops1/2,3/4,1; speed2 at ticks0..95, then0 | Three committed steps and a stop at center(3,3/2) on the third stair |
| 11 | A wall top1, rise3/4 beyond the1/2 policy; speed2 at ticks0..127 | Ordinary horizontal blocking; no automatic step |
| 12 | A1/4-rise stair with a roof bottom11/8; same walking input | Upward clearance fails and the complete ordinary path remains |
| 13 | Flat reference floor; speed1 at ticks0..127 | Ordinary flat walking |
| 14 | Flat reference floor; neutral input | Ordinary support without a step |

The application does not request a teleport or author per-tick pose changes.
Controller pre owns the request and collision owns the entire baseline or accepted
up/across/down world path. Other bodies advance during the trial. Observer50
copies the public committed result and increments typed per-actor counts only
when `movement.stepped` is true; no unregistered mutable gameplay counter is used.

For `steps`, the trace appends a Boolean stepped value after each actor's ignored
ID. Presentation prints this copied flag and its registered cumulative count.
The existing seven Stage22 feature keys remain present but are unused/zero in
this scene. Summary additionally contains:

- `scenario: "steps"`, `controllerProfile:3`, `collisionProfile:4`.
- `steppedTicks`: total successful actor-ticks, not a unique obstacle count.
- `characterStepCounts`: actual typed counts keyed by character ID10..14.
- `builtInStepChecks`: true only for Live/Record built-in input ending at128 or later.

Those built-in checks require actor10's count at least3 and its final centerY bits
equal software3/2. The character stops after96 movement ticks so a512tick run
does not walk off the final platform. Custom scripts and Replay do not infer a
default-input history; their built-in check is false, while recorded typed counts
remain in the checkpoint. These observations are layout checks rather than an
independent full-checkpoint golden or proof for all Float32 policies.

The actual Stage26 Host gate ran Live once with `--scenario steps --ticks 128
--frame-divisor 128`: 128 committed trace rows/input callbacks, actual counts
actor10=3 and actors11..14=0, final centerY3/2 checked, finalHash string
`18097501496580734548`. JUnit testcase time0.393514s, CTest total0.51s; actual
configure/build/CTest all exited0. The executable is in the standard build root,
not a module subdirectory. No new default Stage22 Host, steps Record/Replay,
512tick, Debug/ASan or final VM execution is inferred from this one gate.
The directed Steps gate separately passed0.15571s with8ticks/9CProws/2events/
2rollbacks/4replayedTicks/1sealed rpl/3seeks; full CP bytes compare actual paths,
not an independent full-byte golden. See
[Stage26 actual evidence](../../../../AYDocs/DETERMINISTIC-CHARACTER-STEP-UP-STAGE26.md).
