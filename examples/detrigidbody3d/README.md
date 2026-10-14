# Deterministic rigid3D standard Host

**Version:** 0.1.0
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
DeterminismHost and Scene. It creates a floor, dynamic box, falling sphere and
ball-socket pendulum. Canonical input applies pendulum force through tick31;
the physics solver owns position/rotation. A registered software Float64 clock
advances exactly1/64 per tick and selects manifest5. Physics remains binary32.
Display/file output does not feed native values back into Sim.

`AYEntity_RigidBody3DHostExample` actually passed Live96 advances (64forward plus
32restore), Record64 and Replay64 with full final `.state` and main `.trace`
byte equality and final replay seek. Restore/seek stay on the Host owner thread.
This is finite Windows Release evidence, not whole-CP independent goldens,
renderer/network/large-world coverage or final native VM acceptance.
