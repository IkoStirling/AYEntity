#pragma once
// AYEntity/SceneSerializer.h — P4-B World scene save/load (.ayscene v0).

#include <AYSerializer/SerializeError.h>
#include <AYSerializer/SerializerCore.h>

#include <cstdint>
#include <string>

namespace ayt::entity
{

class World;

constexpr uint32_t kSceneSchemaVersion = 3;
constexpr const char* kSceneSchemaVersionField = "__schemaVersion";
// H1 (lh-rh-split-entity audit 2026-08-24): embed the engine's
// coordinate-convention cache tag alongside __schemaVersion so a
// scene cooked under a different handedness / V-origin / winding
// is rejected at load time instead of silently producing mirrored
// geometry when the renderer / backend swaps. See
// AYMath/CoordinateConvention.h for the canonical tag source.
constexpr const char* kCoordinateConventionField = "__coordConvention";

/// Envelope-level schema step (from → from+1). Missing steps are no-ops
/// so older files load when only component-level MigrationManager matters.
using SceneSchemaMigrateFn = bool (*)(uint32_t fromVersion, uint32_t toVersion);

void registerSceneSchemaMigration(uint32_t fromVersion, uint32_t toVersion,
                                  SceneSchemaMigrateFn fn);

/// Run registered steps from `loadedVersion` up to `kSceneSchemaVersion`.
bool migrateSceneSchemaToCurrent(uint32_t loadedVersion);

bool saveScene(const World& world, const std::string& path,
               ayt::serializer::Format format = ayt::serializer::Format::Json,
               const std::string& actorAssetsRoot = {});
bool loadScene(World& world, const std::string& path,
               ayt::serializer::SerializeError* outError = nullptr,
               const std::string& actorAssetsRoot = {});

} // namespace ayt::entity
