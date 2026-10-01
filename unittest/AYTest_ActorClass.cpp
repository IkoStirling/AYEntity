#include <AYEntity.h>
#include <AYEntity/ActorClassAsset.h>
#include <AYEntity/EntityModule.h>
#include <AYEntity/SceneSerializer.h>
#include <AYEntity/components/ActorInstanceComponent.h>
#include <AYEntity/components/HealthComponent.h>
#include <AYEntity/components/TransformComponent.h>
#include <AYTest.h>

#include <filesystem>
#include <fstream>
#include <chrono>
#include <iterator>
#include <unordered_set>

using namespace ayt::entity;

TEST_SUITE(ActorClass)

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
    const fs::path classFile = root / "actors" / "Enemy.ayactor";
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
    CHECK(instantiateActorClass(*first, asset, "actors/Enemy.ayactor",
                                root.string(), &error));
    const std::string firstId = first->getComponent<ActorInstanceComponent>()->instanceId;
    first->getComponent<Transform>()->setPosition(3.0f, 0.0f, 0.0f);
    first->getComponent<HealthComponent>()->currentHp = 25;

    Entity* second = world.createEntity();
    second->setName("Enemy B");
    CHECK(instantiateActorClass(*second, asset, "actors/Enemy.ayactor",
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
    const auto basePath = root / "actors/Base.ayactor";
    const auto childPath = root / "actors/Child.ayactor";
    const auto grandPath = root / "actors/Grand.ayactor";
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
    child.parentPath = "actors/Base.ayactor";
    child.propertiesJson = R"({"hp":150})";
    child.removedProperties.push_back("speed");
    child.components.push_back({"HealthComponent", R"({"maxHp":150})"});
    CHECK(saveActorClassAsset(childPath.string(), child, &error));
    ActorClassAsset effective;
    CHECK(resolveActorClassAsset(root.string(), "actors/Child.ayactor",
                                 effective, &error));
    CHECK(effective.scriptPath == base.scriptPath);
    CHECK(effective.propertiesJson.find("speed") == std::string::npos);
    CHECK(effective.propertiesJson.find("150") != std::string::npos);
    Entity* entity = world.createEntity();
    CHECK(instantiateActorClass(*entity, child, "actors/Child.ayactor",
                                root.string(), &error));
    CHECK_INT_EQ(entity->getComponent<HealthComponent>()->currentHp, 100);
    CHECK_INT_EQ(entity->getComponent<HealthComponent>()->maxHp, 150);

    ActorClassAsset grand;
    grand.id = "Grand";
    grand.parentPath = "actors/Child.ayactor";
    grand.removedComponents.push_back("HealthComponent");
    CHECK(saveActorClassAsset(grandPath.string(), grand, &error));
    CHECK(resolveActorClassAsset(root.string(), "actors/Grand.ayactor",
                                 effective, &error));
    CHECK(effective.components.empty());
    base.parentPath = "actors/Grand.ayactor";
    CHECK(saveActorClassAsset(basePath.string(), base, &error));
    CHECK_FALSE(resolveActorClassAsset(root.string(), "actors/Child.ayactor",
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
    CHECK(saveActorClassAsset(path.string(), loaded, &error));
    {
        std::ifstream migrated(path);
        const std::string serialized((std::istreambuf_iterator<char>(migrated)),
                                     std::istreambuf_iterator<char>());
        CHECK(serialized.find("\"schemaVersion\": 2")
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
    CHECK(saveActorClassAsset((root / "actors/Enemy.ayactor").string(),
                              actor, &error));
    Entity* original = world.createEntity();
    original->setName("Existing Enemy");
    CHECK(instantiateActorClass(*original, actor, "actors/Enemy.ayactor",
                                root.string(), &error));
    const auto scene = root / "worlds/Bad.ayscene";
    CHECK(saveScene(world, scene.string()));
    std::ifstream input(scene);
    std::string encoded((std::istreambuf_iterator<char>(input)), {});
    input.close();
    const auto position = encoded.find("actors/Enemy.ayactor");
    CHECK(position != std::string::npos);
    if (position != std::string::npos)
        encoded.replace(position, std::string("actors/Enemy.ayactor").size(),
                        "actors/Missing.ayactor");
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
    CHECK(saveActorClassAsset((root / "actors/CrowdEnemy.ayactor").string(),
                              actor, &error));
    const auto start = std::chrono::steady_clock::now();
    std::unordered_set<std::string> identities;
    for (int index = 0; index < kCount; ++index) {
        Entity* entity = world.createEntity();
        CHECK(instantiateActorClass(*entity, actor,
            "actors/CrowdEnemy.ayactor", root.string(), &error));
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
