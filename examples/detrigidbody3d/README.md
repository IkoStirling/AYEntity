# Deterministic rigid3D standard Host

**Version:** 0.2.0
**Date:** 2026-10-14
**Status:** Active
**Owner:** AYEntity

> Architecture: [AYEntity design](../../design.md), [Stage76–78](../../../../AYDocs/DETERMINISTIC-PHYSICS-NUMERICS-STAGE76-78.md).

Enable the default-OFF `AY_ENTITY_BUILD_DET_RIGID3D_EXAMPLE`, build
`AYEntity_DetRigid3DExample` and use a fresh existing output parent:

```text
AYEntity_DetRigid3DExample --ticks 64 --verify-restore --output OUT/live
AYEntity_DetRigid3DExample --ticks 64 --record OUT/recording --output OUT/record
AYEntity_DetRigid3DExample --ticks 64 --replay ACTUAL_RECORDING_PATH --output OUT/replay
```

Use `record.summary.json.recordingPath`, not an invented filename. Replay must use
matching code/configuration/content. `--verify-restore` is Live only; ticks2..1024.
Output `.state`, `.trace` and `.summary.json` are fresh-only; physical I/O failure
may leave partial new files. Example metadata paths must be printable ASCII.

This console sample assembles standard Entity Core/Runtime, module-owned
DeterminismHost and Scene. Default `--scenario rigid` creates a floor, dynamic box,
falling sphere, ball-socket pendulum, a motor/limited hinge rotor and slider capsule.
`--scenario ccd` instead creates a fast Dynamic sphere, thin Static box wall and
Static capsule, with one substep and optional linear sphere CCD.

Canonical input applies force through tick31. Both recipes register software
Float64 clock, vector and quaternion metadata, select manifest6 and advance with
an explicit software widening of authoritative ctx.dt(). Physics remains binary32;
binary64 metadata is independent registered logical state, not a replacement pose.
Trace includes every actor pose and binary64 global lane; full state includes all
registered state. Display/file output never feeds native values back into Sim.

For the CCD recipe, pass `--scenario ccd` in every Live/Record/Replay command above.
Scenario enters content identity: mismatched replays reject. Rigid3D profile2 uses
new body/policy/joint schema and logic identities; no implicit profile1 migration.
CCD admits only Static shapes plus Dynamic spheres and no joints; rotation is held
for geometry, sphere spin stays valid. End-boundary triggers are not swept events.
Iteration/hit budget failure is explicit; use the existing restore/fault boundary.

`AYEntity_RigidBody3DHostExample` runs both scenarios, each actual Live96 advances
(64 forward plus32 restore), Record64 and Replay64 with full final `.state` and
main `.trace` byte equality and final seek: six real processes. Restore/seek stay
on the Host owner thread. This is finite Windows Release evidence, not independent
whole-CP goldens, renderer/network/large-world or final native VM coverage.
See [Stage79–82](../../../../AYDocs/DETERMINISTIC-COMPLETION-STAGE79-82.md).
