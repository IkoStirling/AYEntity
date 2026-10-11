# Experimental deterministic 2D level

**Version:** 0.1.0
**Date:** 2026-10-11
**Status:** Active
**Owner:** AYEntity

`terrain.dlt` is an explicitly loaded initial scene for the deterministic Session/standard Host. It uses real solid AABB and filled triangular wedge bodies, a moving-wedge PingPong path, enabled characters, reciprocal layer/mask filtering and a trigger. The static terrain has a peak at X2, a valley at X4 and a plateau starting at X6. Hero10 starts on the lower flat; heroes11/13 start on the two static slopes; hero12 starts on moving wedge130. Character bodies do not collide with one another. Initial bodies have no ordinary solid penetration.

The `.dlt` format is registered as `experimental.deterministic-level2d`, magic `AYDLEVEL`, version1. This first version is not an AYResource, editor, import/cook or package format. Load the file explicitly before configuring a Session. The resource has no dependent files or asset paths, and provides no streaming, checkpoint import, live reload, rotation or dynamic binding authoring.

The public header is `AYEntity/DeterministicLevel2D.h`. Link `AYEntity::DeterminismKernel` (World/Host uses Determinism/DeterminismHost); the module/portable registration also supplies `AYEntity_Level2DChecks`. Necessary Windows Release level checks and standard Host Live128 passed. Native platform acceptance is deferred to the final VM; see [Stage30–32](../../../../AYDocs/DETERMINISTIC-TERRAIN-STAGE30-32.md).

## Loading and Host ownership

```cpp
ayt::entity::DetLevel2D level;
std::string error;
if (!ayt::entity::loadDetLevel2D("terrain.dlt", level, error)) {
    // Report the error; level is unchanged on failure.
}

// Copy level/options immutably into the normal Host recipe.configure callback.
recipe.config = ayt::entity::detLevelSessionConfig(level);
recipe.configure = [level](ayt::entity::DeterministicSession& session) {
    // Register application canonical input and presentation state first,
    // using IDs outside the ranges below. Then configure the level.
    std::string error;
    return ayt::entity::configureDetLevel2D(session, level, error);
};
```

`configureDetLevel2D` does not seal or advance the destination. The standard Host still owns the tick loop and presentation lifecycle. The controller facade installs the sole collision pose writer; paths generate prescribed velocity, while characters generate motion intent. Application input must run before path pre and character pre and use the registered input setters. Do not modify solved pose or overwrite path/character velocity after their pre callbacks.

First, decoding/configuration checks the entire initial description in a fresh owned Session through the real state factories, installers, initial geometry and seal. Destination registration errors may still leave partial setup; discard that destination rather than retrying it. This is an initial scene installer, not a transaction for an existing running world. Validation does not prove every future command or path position is reachable: existing runtime collision budgets and exact represented path-target checks still apply.

Characters start enabled, airborne and with zero own/body velocity; first pre recognizes actual support. Bound paths start enabled at their first zero offset with `origin=actor.position` and zero velocity. Support IDs/phase/results/ignored-platform references and hidden mutable state cannot be authored. All declared path policies must be bound to at least one MovingObstacle actor.

Default level options reserve path state/policy schemas8..23, content schemas32..47, path pre systems11..18, path post systems41..48, path validator/logic IDs45..52 and identity validator/logic60. Schema/system/validator/logic namespaces are separate and checked independently, with path pre before character pre and path post after character post. Applications can change `DetLevel2DOptions`; for example an application using schemas8/9 must move `pathSchemaBase`. Options change manifest registration identity even though the level semantic content hash remains the same.

## Text format

Input is strictly ASCII, at most65536 bytes. Space, tab, CR and LF separate tokens; a BOM, comments, unknown tokens, trailing tokens and nonfinite floats are rejected. Decimal integers use fixed unsigned32/unsigned64 or signed32 bounds. Booleans are exactly0/1. Every Float32 scalar is exactly eight hexadecimal digits holding software binary32 bits, including signed zero/subnormals; authoring does not parse native floating-point decimal values. Encoding emits canonical lowercase hex and LF text using the classic locale.

Records occur in this exact order. Vector components below are scalar bit tokens.

| Record | Fields following its keyword |
|---|---|
| `AYDLEVEL` | formatVersion=1 |
| `session` | applicationVersion, inputVersion, stepNumerator, stepDenominator |
| `collision` | bodySchema, historySchema, systemId, priority, eventType, maxBodies, maxTriggerPairs, maxContactIterations, extended, oneWayPlatforms, stepUp, maxStepHeightBits, motionPolicy, staticSlopes, movingSlopes, continuousTerrain |
| `character` | stateSchema, policySchema, preSystemId, postSystemId, validatorId, logicProfileId, maxHorizontalSpeedBits, accelerationBits, decelerationBits, gravityBits, jumpSpeedBits, maxFallSpeedBits, maxPlatformSpeedBits, groundProbeDistanceBits, groundContactToleranceBits, snapDistanceBits, leaveVelocity, advanced, coyoteTicks, jumpBufferTicks, dropThroughTicks, crouchEnabled, crouchHeightScaleBits, maxSlopeRisePerRunBits |
| `paths` | pathCount=0..8 |
| `path` | increasing nonzero pathID, playback(Once0/Loop1/PingPong2), waypointCount=2..24 |
| `point` | offsetXBits, offsetYBits, outgoingTravelTicks=1..65535, dwellTicks=0..65535; one record per waypoint |
| `actors` | actorCount=1..64 |
| `actor` | increasing nonzero stableID(u64), mode(Static1/Kinematic2/MovingObstacle3), shape(Aabb0/RightWedge1/LeftWedge2), positionX/YBits, halfX/YBits, offsetX/YBits, velocityX/YBits, layer, mask, trigger, oneWay, character, pathID(0 means unbound) |
| `end` | no fields and no later tokens |

The first waypoint offset must have canonical +0 bits. The existing path profile defines reverse travel durations and requires exact sampled target reachability. Bound paths require nontrigger MovingObstacle bodies with zero initial velocity; characters require solid Kinematic bodies with zero initial velocity. Shape and controller/policy combinations retain their existing validation: moving wedges require `movingSlopes`, continuous terrain requires it together with `staticSlopes`, and advanced characters require one-way policy. The resource uses collision8/controller7 explicitly and preserves older profiles and their bytes.

Complete canonical semantic bytes are limited to8192 bytes. Thus body/path/waypoint limits and the canonical limit apply together. Every policy/ID/record/Float32 bit, including layer/mask, enters a field-by-field little-endian semantic encoding. ASCII whitespace is excluded from identity. This encoding is an internal level fingerprint input, not an engine replay/checkpoint wire format.

`detLevelSessionConfig` gives the content FNV64 identity. Complete semantic bytes, their length and zero padding are also packed into immutable UInt64 typed-default/global chunks; those defaults enter the actual manifest and every actor copy. The pure validator rejects any changed global or actor-copy chunk, including restore. Therefore two different levels with the same64-bit content hash still have different manifests. This is content binding, not authentication. Runtime controls/poses remain registered simulation authority; the initial resource is not a recurring pose reset.

## Short executable

```powershell
AYEntity_Level2DChecks.exe --level terrain.dlt --ticks 128 --speed 3f800000
AYEntity_Level2DChecks.exe --checks --level terrain.dlt --output EXISTING_PARENT
```

The executable really loads the file, configures/seals a Session, submits a canonical speed command for each declared character each tick and reads actual committed results. `--ticks` is0..4096 (default128); `--speed` is a finite8-digit hex Float32 (default+1) subject to the level's normal input speed bound. Duplicate/unknown options fail. `--output` must name an existing parent and creates a fresh child, never overwrites prior runs. It writes canonical `loaded.dlt`, complete `loaded-final.state`, JSON `summary.json`, and `failure.json` on a caught failure. An advance failure also records its input tick, complete valid `failed-before.state` and actual canonical `failed-input.bin`; a faulted post-state is not invented. Process exit0 means this bounded run completed, not native/cross-machine authentication. `pendingEventRows` sums checkpoint pending-event rows and is not an exactly-once confirmed event count.

`--checks` additionally checks strict parsing/output atomicity, namespace conflicts,64-body and8-path endpoints, initial geometry, full content manifest/global/actor-copy binding and atomic invalid restore. An independent dyadic8-tick loaded scene checks position; full checkpoints compare actual direct/restore/real sealed `.rpl` playback/seek paths. The declared `terrain.dlt` resource has a separate64-tick hand calculation: hero12 starts ungrounded on moving wedge130, ownX+1 plus carrierX+1 gives worldX+2; wedge rise/run1/2 gives worldY+1/2. Each actual committed pose/own/carry/body tangent/support/normal/stamp is checked, then restore at32 continues with identical full checkpoints through64. The complete128-tick resource run retains all characters and their original +1 input. It uses the production replay adapter and does not redefine its wire format. The necessary CTest is `AYEntity_Level2DChecks`. No new long campaign or native execution is claimed.
