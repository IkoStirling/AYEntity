#pragma once
// AYEntity/SceneSerializer.h — P4-B World scene save/load (.ayscene v0).

#include <AYSerializer/SerializeError.h>
#include <AYSerializer/SerializerCore.h>

#include <cstdint>
#include <string>

namespace ayt::entity
{

class World;

constexpr uint32_t kSceneSchemaVersion = 2;
constexpr const char* kSceneSchemaVersionField = "__schemaVersion";

/// Envelope-level schema step (from → from+1). Missing steps are no-ops
/// so older files load when only component-level MigrationManager matters.
using SceneSchemaMigrateFn = bool (*)(uint32_t fromVersion, uint32_t toVersion);

void registerSceneSchemaMigration(uint32_t fromVersion, uint32_t toVersion,
                                  SceneSchemaMigrateFn fn);

/// Run registered steps from `loadedVersion` up to `kSceneSchemaVersion`.
bool migrateSceneSchemaToCurrent(uint32_t loadedVersion);

bool saveScene(const World& world, const std::string& path,
               ayt::serializer::Format format = ayt::serializer::Format::Json);
bool loadScene(World& world, const std::string& path,
               ayt::serializer::SerializeError* outError = nullptr);

} // namespace ayt::entity
