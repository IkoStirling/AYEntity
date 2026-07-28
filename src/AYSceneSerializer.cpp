// AYSceneSerializer.cpp — P4-B .ayscene v0 save/load.

#include "AYSceneSerializer.h"
#include "AYComponentFactory.h"
#include "AYEntityModule.h"
#include "AYEntityImpl.h"
#include "AYWorld.h"

#include <AYSerializer.h>

#include <string>
#include <vector>

namespace ayt::entity
{
namespace
{

constexpr const char* kTypeField = "$type";

void clearWorldEntities(World& world) {
    const std::vector<Entity*> entities = world.getAllEntities();
    for (Entity* entity : entities) {
        world.destroyEntity(entity);
    }
}

bool writeSceneEnvelope(ayt::serializer::ISerializer& s, const World& world) {
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
            if (component == nullptr || !ComponentFactory::isSceneSerializable(component->getName())) {
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
                       ayt::serializer::SerializeError* outError) {
    registerEntityComponents();

    s.beginObject(nullptr);

    Int32 wireVersion = 0;
    if (static_cast<ayt::serializer::TokenType>(s.peekFieldTokenType(kSceneSchemaVersionField)) ==
        ayt::serializer::TokenType::Field) {
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
                IComponent* component = ComponentFactory::addComponent(*entity, typeName.c_str());
                if (component == nullptr) {
                    s.reportError(ayt::serializer::SerializeError::Code::UnknownType,
                                  std::string("unknown scene component type: \"") + typeName + '"');
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

bool saveScene(const World& world, const std::string& path, ayt::serializer::Format format) {
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

bool loadScene(World& world, const std::string& path, ayt::serializer::SerializeError* outError) {
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
