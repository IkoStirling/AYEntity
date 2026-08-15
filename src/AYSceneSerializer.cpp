// AYSceneSerializer.cpp — P4-B .ayscene v0 save/load.

#include "AYEntity/SceneSerializer.h"
#include "AYEntity/ComponentFactory.h"
#include "AYEntity/EntityModule.h"
#include "AYEntity/EntityImpl.h"
#include "AYEntity/World.h"

#include <AYSerializer.h>

#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace ayt::entity
{
namespace
{

constexpr const char* kTypeField = "$type";

struct SceneMigKey {
    uint32_t from = 0;
    uint32_t to = 0;
    bool operator==(const SceneMigKey& o) const
    {
        return from == o.from && to == o.to;
    }
};

struct SceneMigKeyHash {
    size_t operator()(const SceneMigKey& k) const
    {
        return (static_cast<size_t>(k.from) << 32) ^ static_cast<size_t>(k.to);
    }
};

std::mutex& sceneMigMu()
{
    static std::mutex m;
    return m;
}

std::unordered_map<SceneMigKey, SceneSchemaMigrateFn, SceneMigKeyHash>& sceneMigMap()
{
    static std::unordered_map<SceneMigKey, SceneSchemaMigrateFn, SceneMigKeyHash> map;
    return map;
}

void clearWorldEntities(World& world)
{
    const std::vector<Entity*> entities = world.getAllEntities();
    for (Entity* entity : entities) {
        world.destroyEntity(entity);
    }
}

bool writeSceneEnvelope(ayt::serializer::ISerializer& s, const World& world)
{
    s.beginObject(nullptr);
    Int32 schemaVersion = static_cast<Int32>(kSceneSchemaVersion);
    s.field(kSceneSchemaVersionField, schemaVersion);

    s.beginArray("entities");
    for (Entity* entity : world.getAllEntities()) {
        if (entity == nullptr || !entity->isValid()) {
            continue;
        }

        s.beginObject(nullptr);
        UInt32 fileId = entity->getId();
        s.field("id", fileId);
        std::string name = entity->getName() ? entity->getName() : "";
        s.field("name", name);

        s.beginArray("components");
        for (IComponent* component : entity->getComponents()) {
            if (component == nullptr
                || !ComponentFactory::isSceneSerializable(component->getName())) {
                continue;
            }

            s.beginObject(nullptr);
            std::string typeName = component->getName();
            s.field(kTypeField, typeName);
            ComponentFactory::serializeComponent(s, *component);
            s.endObject();
        }
        s.endArray();

        s.endObject();
    }
    s.endArray();
    s.endObject();
    return true;
}

bool readSceneEnvelope(ayt::serializer::ISerializer& s, World& world,
                       ayt::serializer::SerializeError* outError)
{
    registerEntityComponents();

    s.beginObject(nullptr);

    Int32 wireVersion = 0;
    if (static_cast<ayt::serializer::TokenType>(s.peekFieldTokenType(kSceneSchemaVersionField))
        == ayt::serializer::TokenType::Field) {
        s.field(kSceneSchemaVersionField, wireVersion);
    }
    if (wireVersion <= 0) {
        wireVersion = static_cast<Int32>(kSceneSchemaVersion);
    }
    if (static_cast<uint32_t>(wireVersion) > kSceneSchemaVersion) {
        s.reportError(ayt::serializer::SerializeError::Code::InvalidInput,
                      "unsupported scene schema version");
        if (outError) {
            *outError = s.lastError();
        }
        s.endObject();
        return false;
    }
    if (!migrateSceneSchemaToCurrent(static_cast<uint32_t>(wireVersion))) {
        s.reportError(ayt::serializer::SerializeError::Code::InvalidInput,
                      "scene schema migration failed");
        if (outError) {
            *outError = s.lastError();
        }
        s.endObject();
        return false;
    }

    s.beginArray("entities");
    while (s.hasMoreArrayElements()) {
        s.beginObject(nullptr);

        UInt32 fileId = 0;
        std::string name;
        s.field("id", fileId);
        s.field("name", name);

        Entity* entity = world.createEntity();
        if (entity == nullptr) {
            s.reportError(ayt::serializer::SerializeError::Code::InvalidInput,
                          "failed to create entity during scene load");
            if (outError) {
                *outError = s.lastError();
            }
            s.endObject();
            s.endArray();
            s.endObject();
            return false;
        }
        if (!name.empty()) {
            entity->setName(name.c_str());
        }
        (void)fileId;

        s.beginArray("components");
        while (s.hasMoreArrayElements()) {
            s.beginObject(nullptr);

            std::string typeName;
            s.field(kTypeField, typeName);
            if (typeName.empty()) {
                s.reportError(ayt::serializer::SerializeError::Code::UnknownType,
                              "component entry missing $type");
            } else {
                IComponent* component =
                    ComponentFactory::addComponent(*entity, typeName.c_str());
                if (component == nullptr) {
                    s.reportError(ayt::serializer::SerializeError::Code::UnknownType,
                                  std::string("unknown scene component type: \"") + typeName
                                      + '"');
                } else {
                    ComponentFactory::deserializeComponent(s, typeName.c_str(), *component);
                }
            }

            s.endObject();
        }
        s.endArray();

        s.endObject();
    }
    s.endArray();
    s.endObject();

    if (!s.lastError().ok()) {
        if (outError) {
            *outError = s.lastError();
        }
        return false;
    }
    return true;
}

} // namespace

void registerSceneSchemaMigration(uint32_t fromVersion, uint32_t toVersion,
                                  SceneSchemaMigrateFn fn)
{
    if (!fn || toVersion != fromVersion + 1) {
        return;
    }
    std::lock_guard<std::mutex> lock(sceneMigMu());
    sceneMigMap()[{fromVersion, toVersion}] = fn;
}

bool migrateSceneSchemaToCurrent(uint32_t loadedVersion)
{
    if (loadedVersion == kSceneSchemaVersion) {
        return true;
    }
    if (loadedVersion > kSceneSchemaVersion) {
        return false;
    }
    for (uint32_t v = loadedVersion; v < kSceneSchemaVersion; ++v) {
        SceneSchemaMigrateFn fn = nullptr;
        {
            std::lock_guard<std::mutex> lock(sceneMigMu());
            auto it = sceneMigMap().find({v, v + 1});
            if (it != sceneMigMap().end()) {
                fn = it->second;
            }
        }
        if (fn && !fn(v, v + 1)) {
            return false;
        }
    }
    return true;
}

bool saveScene(const World& world, const std::string& path, ayt::serializer::Format format)
{
    registerEntityComponents();

    auto serializer = ayt::serializer::createSerializer(format, true);
    if (!serializer) {
        return false;
    }

    if (!writeSceneEnvelope(*serializer, world)) {
        return false;
    }
    return serializer->saveToFile(path);
}

bool loadScene(World& world, const std::string& path, ayt::serializer::SerializeError* outError)
{
    auto serializer = ayt::serializer::createSerializer(ayt::serializer::Format::Json);
    if (!serializer) {
        if (outError) {
            outError->code = ayt::serializer::SerializeError::Code::InvalidInput;
            outError->message = "failed to create JSON serializer";
        }
        return false;
    }

    if (!serializer->loadFromFile(path)) {
        if (outError) {
            outError->code = ayt::serializer::SerializeError::Code::InvalidInput;
            outError->message = "failed to read scene file";
        }
        return false;
    }

    clearWorldEntities(world);
    return readSceneEnvelope(*serializer, world, outError);
}

} // namespace ayt::entity
