# Optional cooked deterministic levels

Build with `AY_ENTITY_LEVEL_RESOURCES=ON` and the existing resource authoring
targets enabled. This adds `AYEntity::DeterministicLevelResources`; the default
deterministic kernel and resource core do not acquire reverse runtime dependencies.

`.dlt` is initial-level authoring. The actual importer validates its complete
Level content, canonicalizes the existing software Float32 bit text and writes
the bounded `.dlc` envelope. Single-file imports revalidate instead of trusting
timestamp sidecars; ProjectBuild retains its SHA content-addressed cache and
checks the complete cached level against its current source before publication.
Its importer contract and sole logical output/type are checked too; cached
metadata cannot redirect a level into a different resource namespace.
The importer preserves the source-root logical hierarchy, so
`Assets/levels/sub/room.dlt` becomes `levels/sub/room.dlc`.

Set the project build profile's `levels/**/*.dlt` rule to `Cook` and storage to
`Loose` or `Pak`. Authoring `.dlt` is not a runtime ResourceManager type and raw
authoring publication is rejected by the optional integration. ProjectBuild's
existing tool/library writes the real `Content/resources.db` and chunk Pak;
CookShip packages an already-cooked `.dlc` tree. Both validate final envelopes
before publishing; CookShip rejects invalid level input before replacing its
previous package/database. These checks do not make filesystem mutation during
publication transactional: keep source and cooked trees quiescent while cooking.

The small runnable example loads through the real ResourceManager and advances
the existing owned Session with canonical empty input:

```powershell
AYEntity_DetLevelResourceExample --asset levels/sub/room.dlc --root path/to/LooseContent --ticks 8
AYEntity_DetLevelResourceExample --asset levels/sub/room.dlc --database path/to/Content/resources.db --ticks 8
```

For gameplay, perform the same acquisition during standard Host configuration:
call `initializeLoaders`, then `registerDetLevelResources2D(error)` before
concurrent loads; obtain the Host's resource manager, load `DetLevelResource2D`,
check its failed load state and retain `level()`. Set the Host recipe config to
`detLevelSessionConfig(*level)` and configure with `configureDetLevel2D`.
Custom application registration options are checked when configuring. Register
canonical application input separately, with the documented system ordering.

Retained `shared_ptr<const DetLevel2D>` snapshots survive resource unload/reload.
Failure preserves the old resource snapshot. Reload does not replace content in
a running Session, rewrite a Scene, import a checkpoint or replay, or perform a
simulation callback's file IO. Construct a fresh matching Session when changing
content. The envelope's FNV64 values detect damage and identify content; they do
not authenticate it. Full Level semantic bytes still enter the normal manifest.

The single integration target `AYEntity_LevelResource2DChecks --output EXISTING_PARENT`
creates a fresh test project/profile and exercises actual import, ProjectBuild,
CookShip, loose load and Pak-only load with source/loose files removed. It checks
nine complete checkpoint rows for each of three resource execution paths against
the direct Level path, alongside an independent eight-tick dyadic motion oracle.
It checks registry conflicts, invalid reload retention, bounded/invalid envelopes,
wrong valid cached content, stale plans and preservation of prior ship bytes.
It retains its bounded project/summary/state evidence under the supplied directory.
These are necessary integration checks, not a native-platform or whole-engine
physics certification.
