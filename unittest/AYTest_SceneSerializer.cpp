#include <AYEntity.h>
#include <AYEntity/SceneSerializer.h>
#include <AYEntity/ComponentFactory.h>
#include <AYEntity/EntityModule.h>
#include <AYEntity/components/AnimationComponent.h>
#include <AYEntity/components/ColliderComponent.h>
#include <AYEntity/components/MeshComponent.h>
#include <AYEntity/components/SkeletonComponent.h>
#include <AYEntity/components/TransformComponent.h>
#include <AYEntity/components/HealthComponent.h>
#include <AYTest.h>

#include <cstring>
#include <cstdio>

using namespace ayt::entity;

namespace {

Entity* createCharacterAuthoringEntity(const char* name)
{
    Entity* entity = World::instance().createEntity();
    if (entity == nullptr) {
        return nullptr;
    }
    entity->setName(name);
    entity->addComponent<Transform>();

    auto* mesh = entity->addComponent<MeshComponent>();
    mesh->meshPath = "meshes/hero.aymesh";
    mesh->materialPath = "materials/hero.aymat";
    mesh->skinned = true;

    auto* skel = entity->addComponent<SkeletonComponent>();
    skel->skeletonPath = "skeletons/hero_Skeleton.ayskel";

    auto* anim = entity->addComponent<AnimationComponent>();
    anim->clipPath = "animations/hero_dance.ayanm";
    anim->autoplay = true;
    anim->looping = true;
    anim->playRate = 1.0f;

    return entity;
}

} // namespace

TEST_SUITE(SceneSerializer)

TEST_CASE(scene_save_load_character_paths_roundtrip)
{
    World::instance().initialize();
    registerEntityComponents();

    Entity* original = createCharacterAuthoringEntity("Hero");
    CHECK_NOT_NULL(original);

    const char* path = "test_scene_character.ayscene";
    CHECK(saveScene(World::instance(), path));

    World::instance().destroyEntity(original);
    CHECK_INT_EQ(static_cast<int>(World::instance().getAllEntities().size()), 0);

    ayt::serializer::SerializeError err;
    CHECK(loadScene(World::instance(), path, &err));
    CHECK(err.ok());

    CHECK_INT_EQ(static_cast<int>(World::instance().getAllEntities().size()), 1);
    Entity* loaded = World::instance().findEntity("Hero");
    CHECK_NOT_NULL(loaded);
    CHECK_TRUE(loaded->hasComponent<Transform>());
    CHECK_TRUE(loaded->hasComponent<MeshComponent>());
    CHECK_TRUE(loaded->hasComponent<SkeletonComponent>());
    CHECK_TRUE(loaded->hasComponent<AnimationComponent>());

    auto* mesh = loaded->getComponent<MeshComponent>();
    CHECK_TRUE(std::strcmp(mesh->meshPath.c_str(), "meshes/hero.aymesh") == 0);
    CHECK_TRUE(std::strcmp(mesh->materialPath.c_str(), "materials/hero.aymat") == 0);
    CHECK_TRUE(mesh->skinned);

    auto* skel = loaded->getComponent<SkeletonComponent>();
    CHECK_TRUE(std::strcmp(skel->skeletonPath.c_str(), "skeletons/hero_Skeleton.ayskel") == 0);

    auto* anim = loaded->getComponent<AnimationComponent>();
    CHECK_TRUE(std::strcmp(anim->clipPath.c_str(), "animations/hero_dance.ayanm") == 0);
    CHECK_TRUE(anim->autoplay);
    CHECK_TRUE(anim->looping);
    CHECK_FLOAT_EQ(anim->playRate, 1.0f, 0.0001f);

    std::remove(path);
    World::instance().shutdown();
}

TEST_CASE(scene_v1_file_without_extra_component)
{
    World::instance().initialize();
    registerEntityComponents();

    Entity* entity = World::instance().createEntity();
    entity->setName("Partial");
    entity->addComponent<Transform>();

    const char* path = "test_scene_partial.ayscene";
    CHECK(saveScene(World::instance(), path));

    World::instance().shutdown();
    World::instance().initialize();
    registerEntityComponents();

    ayt::serializer::SerializeError err;
    CHECK(loadScene(World::instance(), path, &err));
    CHECK(err.ok());

    Entity* loaded = World::instance().findEntity("Partial");
    CHECK_NOT_NULL(loaded);
    CHECK_TRUE(loaded->hasComponent<Transform>());
    CHECK_FALSE(loaded->hasComponent<HealthComponent>());

    std::remove(path);
    World::instance().shutdown();
}

TEST_CASE(component_factory_by_name_roundtrip)
{
    World::instance().initialize();
    registerEntityComponents();

    Entity* entity = World::instance().createEntity();
    CHECK_NOT_NULL(ComponentFactory::addComponent(*entity, "MeshComponent"));
    CHECK_TRUE(entity->hasComponentByName("MeshComponent"));
    CHECK_NOT_NULL(ComponentFactory::getComponent(*entity, "MeshComponent"));
    CHECK_FALSE(ComponentFactory::addComponent(*entity, "UnknownComponent"));

    World::instance().shutdown();
}

namespace {
int g_sceneMigSteps = 0;
bool migrateScene1To2(uint32_t from, uint32_t to)
{
    if (from != 1 || to != 2) {
        return false;
    }
    ++g_sceneMigSteps;
    return true;
}
} // namespace

TEST_CASE(scene_schema_migration_e2e_v1_to_v2)
{
    // Envelope E2E: older __schemaVersion=1 file runs registered 1→2 step
    // before entities are materialized (kSceneSchemaVersion == 2).
    g_sceneMigSteps = 0;
    registerSceneSchemaMigration(1, 2, migrateScene1To2);

    World::instance().initialize();
    registerEntityComponents();

    const char* path = "test_scene_migrate_v1.ayscene";
    {
        FILE* f = std::fopen(path, "wb");
        CHECK_NOT_NULL(f);
        const char* json =
            "{\n"
            "  \"__schemaVersion\": 1,\n"
            "  \"entities\": [\n"
            "    {\n"
            "      \"id\": 1,\n"
            "      \"name\": \"Migrated\",\n"
            "      \"components\": []\n"
            "    }\n"
            "  ]\n"
            "}\n";
        std::fputs(json, f);
        std::fclose(f);
    }

    ayt::serializer::SerializeError err;
    CHECK(loadScene(World::instance(), path, &err));
    CHECK(err.ok());
    CHECK_INT_EQ(g_sceneMigSteps, 1);

    Entity* loaded = World::instance().findEntity("Migrated");
    CHECK_NOT_NULL(loaded);

    std::remove(path);
    World::instance().shutdown();
}

TEST_CASE(scene_save_load_collider_component_roundtrip)
{
    // Collider (2026-08-19): first vector-of-shape-spec component in scenes.
    // Exercises the ColliderShapeSpec struct wire type + enum serialization
    // + vector field round trip.
    World::instance().initialize();
    registerEntityComponents();

    Entity* original = World::instance().createEntity();
    original->setName("Box");
    original->addComponent<Transform>();
    auto* collider = original->addComponent<ColliderComponent>();
    auto& box = collider->addShape();
    box.halfExtents = ayt::math::FVector3(1.5f, 2.5f, 3.5f);
    box.isTrigger = true;
    box.friction = 0.9f;
    auto& cap = collider->addShape();
    cap.shape = ColliderShapeType::Capsule;
    cap.radius = 0.3f;
    cap.height = 1.4f;
    cap.restitution = 0.05f;

    const char* path = "test_scene_collider.ayscene";
    CHECK(saveScene(World::instance(), path));

    World::instance().destroyEntity(original);
    CHECK_INT_EQ(static_cast<int>(World::instance().getAllEntities().size()), 0);

    ayt::serializer::SerializeError err;
    CHECK(loadScene(World::instance(), path, &err));
    CHECK(err.ok());

    Entity* loaded = World::instance().findEntity("Box");
    CHECK_NOT_NULL(loaded);
    auto* loadedCollider = loaded->getComponent<ColliderComponent>();
    CHECK_NOT_NULL(loadedCollider);
    CHECK_INT_EQ(static_cast<int>(loadedCollider->shapes.size()), 2);
    CHECK(loadedCollider->shapes[0].shape == ColliderShapeType::Box);
    CHECK_FLOAT_EQ(loadedCollider->shapes[0].halfExtents.x, 1.5f, 0.0001f);
    CHECK_FLOAT_EQ(loadedCollider->shapes[0].halfExtents.z, 3.5f, 0.0001f);
    CHECK(loadedCollider->shapes[0].isTrigger);
    CHECK_FLOAT_EQ(loadedCollider->shapes[0].friction, 0.9f, 0.0001f);
    CHECK(loadedCollider->shapes[1].shape == ColliderShapeType::Capsule);
    CHECK_FLOAT_EQ(loadedCollider->shapes[1].radius, 0.3f, 0.0001f);
    CHECK_FLOAT_EQ(loadedCollider->shapes[1].height, 1.4f, 0.0001f);
    CHECK_FLOAT_EQ(loadedCollider->shapes[1].restitution, 0.05f, 0.0001f);

    std::remove(path);
    World::instance().shutdown();
}

TEST_SUITE_END
