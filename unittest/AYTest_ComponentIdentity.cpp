#include <AYEntity.h>
#include <AYEntity/EntityModule.h>
#include <AYEntity/SceneSerializer.h>
#include <AYEntity/components/SpriteComponent.h>
#include <AYEntity/components/OrthoCameraComponent.h>
#include <AYEntity/components/TransformComponent.h>
#include <AYTest.h>
#include <fstream>
#include <cstdio>
using namespace ayt::entity;
TEST_SUITE(SceneComponentIdentity)
TEST_CASE(scene_roundtrip_defers_dependency_hooks_until_explicit_components_are_read)
{
    auto& world = World::instance();
    world.initialize();
    registerEntityComponents();
    auto* entity = world.createEntity();
    entity->setName("LateTransform");
    entity->addComponent<SpriteComponent>()->position = {1, 2, 3};
    entity->createComponent<OrthoCameraComponent>();
    auto* transform = entity->addComponent<Transform>();
    transform->position = {17, 23, 31};
    const auto transformId = entity->componentInstance(transform)->id;
    const char* path = "late_transform.scn";
    CHECK(saveScene(world, path));
    ayt::serializer::SerializeError error;
    CHECK(loadScene(world, path, &error));
    CHECK(error.ok());
    entity = world.findEntity("LateTransform");
    CHECK_NOT_NULL(entity);
    if (entity) {
        transform = entity->getComponent<Transform>();
        CHECK_NOT_NULL(transform);
        if (transform) {
            CHECK(entity->componentInstance(transform)->id == transformId);
            CHECK_FLOAT_EQ(transform->position.x, 17.0f, 0.001f);
        }
        CHECK_INT_EQ(static_cast<int>(entity->getComponents<Transform>().size()), 1);
    }
    // A malformed replacement must still restore the sprite-first snapshot.
    std::ofstream(path) << R"({"__schemaVersion":4,"entities":[{"name":"Bad","components":[
        {"$type":"Transform","$instanceId":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"},
        {"$type":"Transform","$instanceId":"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"}]}]})";
    CHECK_FALSE(loadScene(world, path, &error));
    CHECK(error.message.find("restoration failed") == std::string::npos);
    entity = world.findEntity("LateTransform");
    CHECK_NOT_NULL(entity);
    if (entity) {
        CHECK(entity->findComponentById(transformId) != nullptr);
        auto* restored = entity->getComponent<Transform>();
        CHECK_NOT_NULL(restored);
        if (restored) CHECK_FLOAT_EQ(restored->position.x, 17.0f, 0.001f);
    }
    std::remove(path);
    world.shutdown();
}

TEST_CASE(legacy_scene_still_synthesizes_missing_transform)
{
    auto& world = World::instance();
    world.initialize();
    registerEntityComponents();
    const char* path = "legacy_missing_transform.scn";
    std::ofstream(path) << R"({"__schemaVersion":3,"entities":[{"name":"Legacy","components":[
        {"$type":"SpriteComponent","position":[7,8,9]}]}]})";
    ayt::serializer::SerializeError error;
    CHECK(loadScene(world, path, &error));
    auto* entity = world.findEntity("Legacy");
    CHECK_NOT_NULL(entity);
    if (entity) {
        auto* transform = entity->getComponent<Transform>();
        CHECK_NOT_NULL(transform);
        if (transform) CHECK_FLOAT_EQ(transform->position.x, 7.0f, 0.001f);
    }
    std::remove(path);
    world.shutdown();
}

TEST_SUITE_END
