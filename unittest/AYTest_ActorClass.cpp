#include <AYEntity.h>
#include <AYEntity/ActorClassAsset.h>
#include <AYEntity/EntityModule.h>
#include <AYEntity/SceneSerializer.h>
#include <AYEntity/ComponentRegistry.h>
#include <AYEntity/components/ActorInstanceComponent.h>
#include <AYEntity/components/HealthComponent.h>
#include <AYEntity/components/TransformComponent.h>
#include <AYTest.h>
#include <AYSerializer/SerializerCore.h>

#include <filesystem>
#include <fstream>
#include <chrono>
#include <iterator>
#include <unordered_set>

using namespace ayt::entity;

namespace {
class ActorMultiProbe final : public IComponent {
public:
    const char* getName() const override { return "ActorMultiProbe"; }
    Int32 value = 0;
};

bool registerActorMultiProbe() {
    ComponentDescriptor descriptor;
    descriptor.name = "test.ActorMultiProbe";
    descriptor.displayName = "Actor Multi Probe";
    descriptor.type = typeid(ActorMultiProbe);
    descriptor.size = sizeof(ActorMultiProbe);
    descriptor.alignment = alignof(ActorMultiProbe);
    descriptor.multiplicity = ComponentMultiplicity::Multiple;
    descriptor.sceneSerializable = true;
    descriptor.add = [](Entity& entity) -> IComponent* {
        return entity.createComponent<ActorMultiProbe>();
    };
    descriptor.get = [](Entity& entity) -> IComponent* {
        return entity.getComponent<ActorMultiProbe>();
    };
    descriptor.has = [](const Entity& entity) -> bool {
        return entity.hasComponent<ActorMultiProbe>();
    };
    descriptor.remove = [](Entity& entity) { entity.removeComponent<ActorMultiProbe>(); };
    descriptor.serialize = [](ayt::serializer::ISerializer& s, const IComponent& c) {
        Int32 value = static_cast<const ActorMultiProbe&>(c).value;
        s.field("value", value);
    };
    descriptor.deserialize = [](ayt::serializer::ISerializer& s, IComponent& c) {
        s.field("value", static_cast<ActorMultiProbe&>(c).value);
    };
    return ComponentRegistry::instance().registerComponent(std::move(descriptor)).succeeded();
}
} // namespace

TEST_SUITE(ActorClass)

TEST_CASE(authored_transform_slot_survives_inheritance_and_scene_overrides)
{
    namespace fs = std::filesystem;
    auto& world = World::instance();
    world.initialize();
    registerEntityComponents();
    const fs::path root = fs::temp_directory_path() / "ay_actor_transform_slot" / "Assets";
    fs::create_directories(root / "actors");
    const std::string slot = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
    ActorClassAsset base;
    std::string error;
    CHECK(parseActorClassAsset(R"({"type":"ay.actorClass","schemaVersion":3,"id":"RootActor",
        "components":[{"$type":"Transform","$instanceId":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"}]})",
        base, &error));
    CHECK(saveActorClassAsset((root / "actors/Base.act").string(), base, &error));
    ActorClassAsset child;
    child.id = "ChildActor";
    child.parentPath = "actors/Base.act";
    child.components.push_back({"Transform", R"({"position":[4,5,6]})", slot, "Root"});
    CHECK(saveActorClassAsset((root / "actors/Child.act").string(), child, &error));
    auto* entity = world.createEntity();
    entity->setName("AuthoredRoot");
    CHECK(instantiateActorClass(*entity, child, "actors/Child.act", root.string(), &error));
    auto* transform = entity->getComponent<Transform>();
    CHECK_NOT_NULL(transform);
    if (transform) {
        CHECK(entity->findComponentById(slot) == transform);
        CHECK_FLOAT_EQ(transform->position.x, 4.0f, 0.001f);
        transform->position.x = 42;
    }
    const auto scene = root / "roundtrip.scn";
    CHECK(saveScene(world, scene.string()));
    ayt::serializer::SerializeError sceneError;
    CHECK(loadScene(world, scene.string(), &sceneError));
    entity = world.findEntity("AuthoredRoot");
    CHECK_NOT_NULL(entity);
    if (entity) {
        transform = entity->getComponent<Transform>();
        CHECK_NOT_NULL(transform);
        if (transform) {
            CHECK(entity->findComponentById(slot) == transform);
            CHECK_FLOAT_EQ(transform->position.x, 42.0f, 0.001f);
        }
    }
    child.components.clear();
    child.removedComponents.push_back(slot);
    CHECK(saveActorClassAsset((root / "actors/Child.act").string(), child, &error));
    ActorClassAsset resolved;
    CHECK_FALSE(resolveActorClassAsset(root.string(), "actors/Child.act", resolved, &error));
    world.shutdown();
    fs::remove_all(root.parent_path());
}

TEST_CASE(actor_multi_slots_inherit_override_and_roundtrip)
{
    namespace fs = std::filesystem;
    World& world = World::instance();
    world.initialize();
    registerEntityComponents();
    CHECK(registerActorMultiProbe());
    const fs::path root = fs::temp_directory_path()
        / "ay_actor_multi_slots" / "Assets";
    fs::create_directories(root / "actors");
    fs::create_directories(root / "worlds");
    const std::string firstId = makeComponentInstanceId();
    const std::string secondId = makeComponentInstanceId();
    ActorClassAsset base;
    base.id = "MultiBase";
    base.components.push_back({"test.ActorMultiProbe", R"({"value":10})",
                               firstId, "First"});
    base.components.push_back({"test.ActorMultiProbe", R"({"value":20})",
                               secondId, "Second"});
    std::string error;
    CHECK(saveActorClassAsset((root / "actors/Base.act").string(), base, &error));
    ActorClassAsset child;
    child.id = "MultiChild";
    child.parentPath = "actors/Base.act";
    child.components.push_back({"test.ActorMultiProbe", R"({"value":25})",
                                secondId, {}});
    CHECK(saveActorClassAsset((root / "actors/Child.act").string(), child, &error));
    ActorClassAsset effective;
    CHECK(resolveActorClassAsset(root.string(), "actors/Child.act", effective, &error));
    CHECK_INT_EQ(static_cast<int>(effective.components.size()), 2);
    Entity* entity = world.createEntity();
    entity->setName("MultiActor");
    CHECK(instantiateActorClass(*entity, child, "actors/Child.act",
                                root.string(), &error));
    auto* first = static_cast<ActorMultiProbe*>(entity->findComponentById(firstId));
    auto* second = static_cast<ActorMultiProbe*>(entity->findComponentById(secondId));
    CHECK_NOT_NULL(first);
    CHECK_NOT_NULL(second);
    CHECK_INT_EQ(first->value, 10);
    CHECK_INT_EQ(second->value, 25);
    first->value = 13;
    CHECK(entity->removeComponentById(secondId));
    auto* third = entity->createComponent<ActorMultiProbe>();
    third->value = 30;
    const std::string thirdId = entity->componentInstance(third)->id;
    const auto scene = root / "worlds/Multi.ayscene";
    CHECK(saveScene(world, scene.string()));
    ayt::serializer::SerializeError sceneError;
    CHECK(loadScene(world, scene.string(), &sceneError));
    CHECK(sceneError.ok());
    entity = world.findEntity("MultiActor");
    CHECK_NOT_NULL(entity);
    first = static_cast<ActorMultiProbe*>(entity->findComponentById(firstId));
    third = static_cast<ActorMultiProbe*>(entity->findComponentById(thirdId));
    CHECK_NOT_NULL(first);
    CHECK_NOT_NULL(third);
    CHECK_INT_EQ(first->value, 13);
    CHECK_INT_EQ(third->value, 30);
    CHECK(entity->findComponentById(secondId) == nullptr);
    world.shutdown();
    fs::remove_all(root.parent_path());
}

TEST_CASE(actor_class_scene_roundtrip_preserves_identity_and_field_overrides)
{
    namespace fs = std::filesystem;
    World& world = World::instance();
    world.initialize();
    registerEntityComponents();

    const fs::path root = fs::temp_directory_path()
        / "ay_actor_class_roundtrip" / "Assets";
    fs::create_directories(root / "actors");
    fs::create_directories(root / "worlds");
    const fs::path classFile = root / "actors" / "Enemy.act";
    const fs::path sceneFile = root / "worlds" / "Encounter.ayscene";
    ActorClassAsset asset;
    asset.id = "Enemy";
    asset.propertiesJson = R"({"hp":100})";
    asset.components.push_back({"HealthComponent",
        R"({"currentHp":100,"maxHp":100})"});
    std::string error;
    CHECK(saveActorClassAsset(classFile.string(), asset, &error));

    Entity* first = world.createEntity();
    first->setName("Enemy A");
    CHECK(instantiateActorClass(*first, asset, "actors/Enemy.act",
                                root.string(), &error));
    const std::string healthSlotId =
        first->componentInstance(first->getComponent<HealthComponent>())->id;
    const std::string firstId = first->getComponent<ActorInstanceComponent>()->instanceId;
    first->getComponent<Transform>()->setPosition(3.0f, 0.0f, 0.0f);
    first->getComponent<HealthComponent>()->currentHp = 25;

    Entity* second = world.createEntity();
    second->setName("Enemy B");
    CHECK(instantiateActorClass(*second, asset, "actors/Enemy.act",
                                root.string(), &error));
    const std::string secondId = second->getComponent<ActorInstanceComponent>()->instanceId;
    CHECK(firstId != secondId);
    asset.components[0].payloadJson = R"({"currentHp":100,"maxHp":180})";
    CHECK(saveActorClassAsset(classFile.string(), asset, &error));
    CHECK(saveScene(world, sceneFile.string()));
    ayt::serializer::SerializeError sceneError;
    CHECK(loadScene(world, sceneFile.string(), &sceneError));
    CHECK(sceneError.ok());
    first = world.findEntity("Enemy A");
    second = world.findEntity("Enemy B");
    CHECK_NOT_NULL(first);
    CHECK_NOT_NULL(second);
    CHECK(first->getComponent<ActorInstanceComponent>()->instanceId == firstId);
    CHECK(first->componentInstance(first->getComponent<HealthComponent>())->id == healthSlotId);
    CHECK(second->getComponent<ActorInstanceComponent>()->instanceId == secondId);
    CHECK_FLOAT_EQ(first->getComponent<Transform>()->position.x, 3.0f, 0.001f);
    CHECK_INT_EQ(first->getComponent<HealthComponent>()->currentHp, 25);
    CHECK_INT_EQ(first->getComponent<HealthComponent>()->maxHp, 180);
    CHECK_INT_EQ(second->getComponent<HealthComponent>()->maxHp, 180);

    world.shutdown();
    fs::remove(sceneFile);
    fs::remove(classFile);
    fs::remove(root / "worlds");
    fs::remove(root / "actors");
    fs::remove(root);
    fs::remove(root.parent_path());
}

TEST_CASE(actor_class_inherits_data_and_rejects_cycles)
{
    namespace fs = std::filesystem;
    World& world = World::instance();
    world.initialize();
    registerEntityComponents();
    const fs::path root = fs::temp_directory_path()
        / "ay_actor_class_inheritance" / "Assets";
    fs::create_directories(root / "actors");
    const auto basePath = root / "actors/Base.act";
    const auto childPath = root / "actors/Child.act";
    const auto grandPath = root / "actors/Grand.act";
    std::string error;
    ActorClassAsset base;
    base.id = "Base";
    base.scriptPath = "actors/Base.logia";
    base.propertiesJson = R"({"hp":100,"speed":1})";
    base.components.push_back({"HealthComponent",
        R"({"currentHp":100,"maxHp":100})"});
    CHECK(saveActorClassAsset(basePath.string(), base, &error));

    ActorClassAsset child;
    child.id = "Child";
    child.parentPath = "actors/Base.act";
    child.propertiesJson = R"({"hp":150})";
    child.removedProperties.push_back("speed");
    child.components.push_back({"HealthComponent", R"({"maxHp":150})"});
    CHECK(saveActorClassAsset(childPath.string(), child, &error));
    ActorClassAsset effective;
    CHECK(resolveActorClassAsset(root.string(), "actors/Child.act",
                                 effective, &error));
    CHECK(effective.scriptPath == base.scriptPath);
    CHECK(effective.propertiesJson.find("speed") == std::string::npos);
    CHECK(effective.propertiesJson.find("150") != std::string::npos);
    Entity* entity = world.createEntity();
    CHECK(instantiateActorClass(*entity, child, "actors/Child.act",
                                root.string(), &error));
    CHECK_INT_EQ(entity->getComponent<HealthComponent>()->currentHp, 100);
    CHECK_INT_EQ(entity->getComponent<HealthComponent>()->maxHp, 150);

    ActorClassAsset grand;
    grand.id = "Grand";
    grand.parentPath = "actors/Child.act";
    grand.removedComponents.push_back("HealthComponent");
    CHECK(saveActorClassAsset(grandPath.string(), grand, &error));
    CHECK(resolveActorClassAsset(root.string(), "actors/Grand.act",
                                 effective, &error));
    CHECK(effective.components.empty());
    base.parentPath = "actors/Grand.act";
    CHECK(saveActorClassAsset(basePath.string(), base, &error));
    CHECK_FALSE(resolveActorClassAsset(root.string(), "actors/Child.act",
                                       effective, &error));
    CHECK(error.find("cycle") != std::string::npos);

    world.shutdown();
    fs::remove_all(root.parent_path());
}

TEST_CASE(actor_schema_one_remains_readable)
{
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / "ay_actor_schema_one";
    fs::create_directories(root);
    const auto path = root / "Legacy.ayactor";
    std::ofstream(path) << R"({"type":"ay.actorClass","schemaVersion":1,"id":"Legacy","script":"","properties":{},"components":[]})";
    ActorClassAsset loaded;
    std::string error;
    CHECK(loadActorClassAsset(path.string(), loaded, &error));
    CHECK(loaded.id == "Legacy");
    CHECK(loaded.parentPath.empty());
    const auto canonical = root / "Legacy.act";
    CHECK(saveActorClassAsset(canonical.string(), loaded, &error));
    {
        std::ifstream migrated(canonical);
        const std::string serialized((std::istreambuf_iterator<char>(migrated)),
                                     std::istreambuf_iterator<char>());
        CHECK(serialized.find("\"schemaVersion\": 3")
              != std::string::npos);
    }
    fs::remove_all(root);
}

TEST_CASE(failed_actor_scene_load_restores_previous_world)
{
    namespace fs = std::filesystem;
    World& world = World::instance();
    world.initialize();
    registerEntityComponents();
    const fs::path root = fs::temp_directory_path()
        / "ay_actor_load_rollback" / "Assets";
    fs::create_directories(root / "actors");
    fs::create_directories(root / "worlds");
    ActorClassAsset actor;
    actor.id = "Enemy";
    actor.components.push_back({"HealthComponent",
        R"({"currentHp":77,"maxHp":100})"});
    std::string error;
    CHECK(saveActorClassAsset((root / "actors/Enemy.act").string(),
                              actor, &error));
    Entity* original = world.createEntity();
    original->setName("Existing Enemy");
    CHECK(instantiateActorClass(*original, actor, "actors/Enemy.act",
                                root.string(), &error));
    const auto scene = root / "worlds/Bad.ayscene";
    CHECK(saveScene(world, scene.string()));
    std::ifstream input(scene);
    std::string encoded((std::istreambuf_iterator<char>(input)), {});
    input.close();
    const auto position = encoded.find("actors/Enemy.act");
    CHECK(position != std::string::npos);
    if (position != std::string::npos)
        encoded.replace(position, std::string("actors/Enemy.act").size(),
                        "actors/Missing.act");
    std::ofstream(scene, std::ios::trunc) << encoded;
    ayt::serializer::SerializeError sceneError;
    CHECK_FALSE(loadScene(world, scene.string(), &sceneError));
    original = world.findEntity("Existing Enemy");
    CHECK_NOT_NULL(original);
    if (original)
        CHECK_INT_EQ(original->getComponent<HealthComponent>()->currentHp, 77);
    world.shutdown();
    fs::remove_all(root.parent_path());
}

TEST_CASE(actor_large_scene_roundtrip_within_debug_budget)
{
    namespace fs = std::filesystem;
    constexpr int kCount = 500;
    World& world = World::instance();
    world.initialize();
    registerEntityComponents();
    const fs::path root = fs::temp_directory_path()
        / "ay_actor_large_scene" / "Assets";
    fs::create_directories(root / "actors");
    fs::create_directories(root / "worlds");
    ActorClassAsset actor;
    actor.id = "CrowdEnemy";
    std::string error;
    CHECK(saveActorClassAsset((root / "actors/CrowdEnemy.act").string(),
                              actor, &error));
    const auto start = std::chrono::steady_clock::now();
    std::unordered_set<std::string> identities;
    for (int index = 0; index < kCount; ++index) {
        Entity* entity = world.createEntity();
        CHECK(instantiateActorClass(*entity, actor,
            "actors/CrowdEnemy.act", root.string(), &error));
        identities.insert(entity->getComponent<ActorInstanceComponent>()->instanceId);
    }
    CHECK_INT_EQ(identities.size(), static_cast<std::size_t>(kCount));
    const auto scene = root / "worlds/Crowd.ayscene";
    CHECK(saveScene(world, scene.string()));
    ayt::serializer::SerializeError sceneError;
    CHECK(loadScene(world, scene.string(), &sceneError));
    CHECK_INT_EQ(world.getAllEntities().size(), static_cast<std::size_t>(kCount));
    for (Entity* entity : world.getAllEntities()) {
        auto* instance = entity->getComponent<ActorInstanceComponent>();
        CHECK(instance != nullptr && identities.contains(instance->instanceId));
    }
    const auto elapsed = std::chrono::steady_clock::now() - start;
    CHECK(elapsed < std::chrono::seconds(10));
    world.shutdown();
    fs::remove_all(root.parent_path());
}

TEST_SUITE_END
