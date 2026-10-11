# Deterministic terrain: standard Host example

**Version:** 0.1.0
**Date:** 2026-10-11
**Status:** Active
**Owner:** AYEntity

This executable loads the public [terrain.dlt](../detlevel/terrain.dlt) resource
through `loadDetLevel2D`, and installs it through the standard Host recipe.
It creates a real EngineModuleRuntime, Entity modules, Play Scene and
DeterministicHostIntegrationModule. The standard Host owns fixed ticks and Scene
lifecycle. Simulation callbacks capture immutable policy; native float is used
only to print committed positions.

See the [level format](../detlevel/README.md),
[Stage30–32](../../../../AYDocs/DETERMINISTIC-TERRAIN-STAGE30-32.md) and
[SDK contract](../../../../AYDocs/sdk/contracts/2d.md#2dsimulationdeterministic-level).

## Build and run

Use a configured root build with Application, Scene and Entity 2D schemas,
the engine dependency toolchain, and `AY_ENTITY_BUILD_DET_TERRAIN_EXAMPLE=ON`
or `BUILD_TESTING=ON`. Linkage is `AYEntity::DeterminismHost`.

```powershell
cmake --build HOST_BUILD_DIRECTORY --target AYEntity_DetTerrainExample
AYEntity_DetTerrainExample --level AYRuntime/AYEntity/examples/detlevel/terrain.dlt --ticks 128 --frame-divisor 128 --require-features --output fresh/terrain
```

Run from the engine root or supply the resource's absolute path. `fresh` must
already exist and all output files must be new. `--ticks` is the absolute final
nextTick, in1..65536. This bounded sample requires the authored1/64 step;
`--frame-divisor 64|128` changes presentation scheduling only. File-adapter paths
must be printable ASCII. Invalid or duplicate options fail.
This sample's canonical command encoder bounds character IDs to32 bits; the level
loader itself supports full unsigned64 stable IDs. Its application schemas24/25
and systems10/61 require nonconflicting authored registration ranges.

The script polls input before each Host frame, then encodes commands for stable
character IDs. Hero10 walks right from the lower floor, heroes11/13 walk in
opposite directions for64 ticks and then stop, and hero12 has zero own input
while riding the translating wedge. Input system10 precedes paths11..18,
character pre20, collision30, post40 and paths41..48. Observer61 copies actual
committed state into application schemas24/25 and counts real static-slope
walking and moving-slope carry. It does not write poses or gameplay input.

Outputs are complete `.state`, per-tick/all-actor `.trace` and `.summary.json`.
`--require-features` checks that both observed features actually occurred.
The CTest gate `AYEntity_TerrainHostExample` runs one Live128 scene and validates
its profiles, tick/input/trace counts, feature counts and nonempty outputs.
Final local acceptance is recorded in the Stage30–32 document.

## Existing replay adapter

The CLI also accepts `--record fresh.rpl`, or `--replay actual_000.rpl` with
optional `--seek tick`. These use the existing Host recorder, reader and rotation
naming; inspect `recordingPath` in the summary for the actual destination.
Record and Replay are mutually exclusive; Seek is Replay-only. Replay never
polls live input and must reach recorded completion at the requested nextTick.
This stage's Host acceptance is Live128; these additional example modes have
not received a new Record/Replay/Seek integration gate. Level2DChecks separately
exercises actual short Session recording, playback and seek.

No native VM, GitHub CI or long-running campaign is part of this stage's local
acceptance. The resource is an Experimental initial-level format; it has no
editor, AYResource, cook/package, streaming or automatic Tilemap collision path.
