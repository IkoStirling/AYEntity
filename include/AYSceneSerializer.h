#pragma once
// AYSceneSerializer.h — P4-B World scene save/load (.ayscene v0).

#include <ayserializer/SerializeError.h>
#include <ayserializer/SerializerCore.h>

#include <string>

namespace ayt::entity
{

class World;

constexpr uint32_t kSceneSchemaVersion = 1;
constexpr const char* kSceneSchemaVersionField = "__schemaVersion";

bool saveScene(const World& world, const std::string& path,
               ayt::serializer::Format format = ayt::serializer::Format::Json);
bool loadScene(World& world, const std::string& path,
               ayt::serializer::SerializeError* outError = nullptr);

} // namespace ayt::entity
