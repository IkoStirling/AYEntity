// AYSceneSerializer.cpp — P4-B .ayscene v0 save/load.

#include "AYEntity/SceneSerializer.h"
#include "AYEntity/ComponentFactory.h"
#include "AYEntity/ComponentRegistry.h"
#include "AYEntity/EntityImpl.h"
#include "AYEntity/World.h"
#include "AYEntity/ActorClassAsset.h"
#include "AYEntity/components/ActorInstanceComponent.h"

#include <AYSerializer.h>
#include <AYIO/File.h>
#include <AYMath/CoordinateConvention.h>

#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>
#include <cstdio>

namespace ayt::entity
{
namespace
{

constexpr const char* kTypeField = "$type";
constexpr const char* kInstanceIdField = "$instanceId";
constexpr const char* kDisplayNameField = "$displayName";

void writeIdentity(ayt::serializer::ISerializer& s, const Entity& entity,
                   const IComponent& component) {
    const auto* instance = entity.componentInstance(&component);
    std::string id = instance ? instance->id : std::string{};
    std::string label = instance ? instance->displayName : std::string{};
    s.field(kInstanceIdField, id);
    s.field(kDisplayNameField, label);
}

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

bool writeSceneEnvelope(ayt::serializer::ISerializer& s, const World& world,
                        const std::string& scenePath,
                        const std::string& actorAssetsRoot)
{
    s.beginObject(nullptr);
    Int32 schemaVersion = static_cast<Int32>(kSceneSchemaVersion);
    s.field(kSceneSchemaVersionField, schemaVersion);
    // H1 (lh-rh-split-entity audit 2026-08-24): embed the engine's
    // canonical coordinate-convention cache tag so a future
    // backend/convention swap can reject mismatched scenes at load
    // time. See CoordinateConvention.h for the tag source of truth.
    std::string coordConvention = ayt::math::EngineCoordinateConvention::cacheTag;
    s.field(kCoordinateConventionField, coordConvention);

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
        if (auto* actor = entity->getComponent<ActorInstanceComponent>()) {
            ActorInstanceComponent snapshot;
            snapshot.classPath = actor->classPath;
            snapshot.instanceId = actor->instanceId;
            snapshot.propertyOverridesJson = actor->propertyOverridesJson;
            snapshot.classDefaultsJson = actor->classDefaultsJson;
            snapshot.assetsRoot = actorAssetsRoot.empty()
                ? assetsRootForScene(scenePath) : actorAssetsRoot;
            std::string error;
            if (snapshot.assetsRoot.empty()
                || !captureActorOverrides(*entity, snapshot, &error)) {
                std::fprintf(stderr, "[AYSceneSerializer] Actor save failed: %s\n",
                             error.empty() ? "Scene must be under Assets" : error.c_str());
                return false;
            }
            s.beginObject(nullptr);
            std::string typeName = "ActorInstanceComponent";
            s.field(kTypeField, typeName);
            writeIdentity(s, *entity, *actor);
            ComponentFactory::serializeComponent(s, snapshot);
            s.endObject();
            s.endArray();
            s.endObject();
            continue;
        }
        for (IComponent* component : entity->getComponents()) {
            if (component == nullptr) {
                continue;
            }
            const char* registeredTypeName =
                ComponentFactory::registeredTypeName(*component);
            if (registeredTypeName == nullptr
                || !ComponentFactory::isSceneSerializable(registeredTypeName)) {
                continue;
            }

            s.beginObject(nullptr);
            std::string typeName = registeredTypeName;
            s.field(kTypeField, typeName);
            writeIdentity(s, *entity, *component);
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
                       const std::string& scenePath,
                       const std::string& actorAssetsRoot,
                       ayt::serializer::SerializeError* outError)
{
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

    // H1 (lh-rh-split-entity audit 2026-08-24): validate the scene's
    // coordinate-convention tag against the engine's canonical tag
    // before doing any per-component work. A mismatch means the scene
    // was cooked under a different handedness / V-origin / winding
    // and would silently produce mirrored geometry when the renderer
    // backend swaps — reject here. Missing tag = back-compat with
    // scenes written before the field existed; warn but accept.
    std::string coordConvention;
    if (static_cast<ayt::serializer::TokenType>(s.peekFieldTokenType(kCoordinateConventionField))
        == ayt::serializer::TokenType::Field) {
        s.field(kCoordinateConventionField, coordConvention);
        if (!ayt::math::EngineCoordinateConvention::matchesAssetTag(coordConvention.c_str())) {
            s.reportError(
                ayt::serializer::SerializeError::Code::InvalidInput,
                std::string("scene coordinate convention mismatch: file='")
                    + coordConvention + "' engine='"
                    + ayt::math::EngineCoordinateConvention::cacheTag
                    + "'. Scene was cooked under a different handedness / V-origin / winding.");
            if (outError) {
                *outError = s.lastError();
            }
            s.endObject();
            return false;
        }
    } else {
        std::fprintf(stderr,
                     "[AYSceneSerializer] WARNING: scene file lacks '%s' field; "
                     "loaded with engine default '%s'. Re-save to embed the tag.\n",
                     kCoordinateConventionField,
                     ayt::math::EngineCoordinateConvention::cacheTag);
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
            std::string instanceId;
            std::string displayName;
            if (wireVersion >= 4) {
                s.field(kInstanceIdField, instanceId);
                s.field(kDisplayNameField, displayName);
            } else {
                instanceId = legacyComponentInstanceId(typeName);
            }
            if (typeName.empty()) {
                s.reportError(ayt::serializer::SerializeError::Code::UnknownType,
                              "component entry missing $type");
            } else {
                const auto* descriptor = ComponentRegistry::instance().find(typeName);
                const bool duplicateSingle = descriptor
                    && descriptor->multiplicity == ComponentMultiplicity::Single
                    && entity->hasComponentByName(typeName.c_str());
                // A legacy Sprite/Camera may synthesize Transform before an
                // explicit Transform entry later in the same old Scene.
                IComponent* component = duplicateSingle
                    ? (wireVersion < 4 && typeName == "Transform"
                        ? entity->getComponentByName(typeName.c_str()) : nullptr)
                    : ComponentFactory::addComponent(*entity, typeName.c_str());
                if (component == nullptr) {
                    s.reportError(ayt::serializer::SerializeError::Code::UnknownType,
                                  std::string("duplicate or unknown scene component type: \"") + typeName
                                      + '"');
                } else if (!entity->setComponentInstanceId(component, instanceId)
                           || !entity->setComponentDisplayName(component, displayName)) {
                    s.reportError(ayt::serializer::SerializeError::Code::InvalidInput,
                                  "invalid or duplicate component instance identity: " + typeName);
                } else {
                    if (ComponentFactory::deserializeComponent(
                            s, typeName.c_str(), *component)) {
                        ComponentFactory::afterSceneDeserialize(
                            *entity, typeName.c_str(), *component);
                        if (typeName == "ActorInstanceComponent") {
                            auto& actor = static_cast<ActorInstanceComponent&>(*component);
                            actor.assetsRoot = actorAssetsRoot.empty()
                                ? assetsRootForScene(scenePath) : actorAssetsRoot;
                            std::string actorError;
                            if (!expandActorInstance(*entity, actor, &actorError)) {
                                s.reportError(
                                    ayt::serializer::SerializeError::Code::InvalidInput,
                                    "Actor instance expansion failed: " + actorError);
                            }
                        }
                    }
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

bool saveScene(const World& world, const std::string& path,
               ayt::serializer::Format format, const std::string& actorAssetsRoot)
{
    auto serializer = ayt::serializer::createSerializer(format, true);
    if (!serializer) {
        return false;
    }

    if (!writeSceneEnvelope(*serializer, world, path, actorAssetsRoot)) {
        return false;
    }

    // Serialize to an in-memory buffer first, then flush via
    // `ayt::io::File::atomicWrite`. This protects the on-disk scene file
    // from corruption if the editor crashes (or the host loses power)
    // mid-write: a partial JSON/XML/binary file would otherwise replace
    // the previously good copy and leave the user with no recoverable
    // version. atomicWrite uses a `.tmp + rename` pattern under the same
    // directory so the swap is atomic on every platform AYIO targets.
    //
    // Empty serialized output means an empty world — treated as a save
    // failure rather than a no-op, because atomicWrite rejects zero-byte
    // writes (its contract is "delete" rather than "create empty file").
    // Callers that genuinely want to clear a scene must remove the file
    // explicitly; silently producing a 0-byte scene would corrupt the
    // document the next time the user opens it.
    const std::string serialized = serializer->output();
    if (serialized.empty()) {
        return false;
    }
    return ayt::io::File::atomicWrite(path, serialized.data(), serialized.size());
}

bool loadScene(World& world, const std::string& path,
               ayt::serializer::SerializeError* outError,
               const std::string& actorAssetsRoot)
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

    // Keep a restorable in-memory snapshot before changing the live World.
    // Malformed or missing Actor dependencies must not leave a half-loaded Scene.
    auto backup = ayt::serializer::createSerializer(ayt::serializer::Format::Json, true);
    if (!backup || !writeSceneEnvelope(*backup, world, path, actorAssetsRoot)) {
        if (outError) {
            outError->code = ayt::serializer::SerializeError::Code::InvalidInput;
            outError->message = "failed to snapshot current scene before load";
        }
        return false;
    }
    const std::string snapshot = backup->output();
    clearWorldEntities(world);
    if (readSceneEnvelope(*serializer, world, path, actorAssetsRoot, outError))
        return true;
    clearWorldEntities(world);
    auto restore = ayt::serializer::createSerializer(ayt::serializer::Format::Json);
    if (restore) restore->deserialize(snapshot);
    if (!restore || !restore->lastError().ok()
        || !readSceneEnvelope(*restore, world, path, actorAssetsRoot, nullptr)) {
        if (outError) outError->message += "; previous scene restoration failed";
    }
    return false;
}

} // namespace ayt::entity
