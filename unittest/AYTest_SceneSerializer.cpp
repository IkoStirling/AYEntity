#include <AYEntity.h>
#include <AYSceneSerializer.h>
#include <AYComponentFactory.h>
#include <AYEntityModule.h>
#include <components/AYAnimationComponent.h>
#include <components/AYMeshComponent.h>
#include <components/AYSkeletonComponent.h>
#include <components/AYTransformComponent.h>
#include <components/AYHealthComponent.h>
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

TEST_SUITE_END
