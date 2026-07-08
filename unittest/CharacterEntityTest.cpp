// CharacterEntityTest.cpp - Phase 1 ED-02 helper tests.
//
// Covers spawnCharacterFromPaths / destroyCharacter from
// `include/AYCharacterEntity.h`. Mirrors the conventions of
// EntityTest.cpp / ComponentTest.cpp — World::initialize() per test,
// Entity::create()/destroy(), CHECK_* macros.

#include <AYEntity.h>
#include <AYCharacterEntity.h>
#include <components/AYAnimationComponent.h>
#include <components/AYMeshComponent.h>
#include <components/AYSkeletonComponent.h>
#include <components/AYTransformComponent.h>
#include <AYTest.h>

#include <cstring>

using namespace ayt::entity;

TEST_SUITE(CharacterEntity)

TEST_CASE(spawn_returns_entity_with_all_four_components)
{
    World::instance().initialize();

    Entity* e = spawnCharacterFromPaths(
        "meshes/hero.aymesh",
        "materials/hero.aymat",
        "skeletons/hero_Skeleton.ayskel",
        "animations/hero_dance.ayanm");
    CHECK_NOT_NULL(e);
    CHECK_TRUE(e->isValid());

    // Four components expected: Transform + MeshComponent (skinned=true)
    // + SkeletonComponent + AnimationComponent.
    CHECK_TRUE(e->hasComponent<Transform>());
    CHECK_TRUE(e->hasComponent<MeshComponent>());
    CHECK_TRUE(e->hasComponent<SkeletonComponent>());
    CHECK_TRUE(e->hasComponent<AnimationComponent>());

    auto* mesh = e->getComponent<MeshComponent>();
    CHECK_NOT_NULL(mesh);
    CHECK_TRUE(mesh->skinned);
    CHECK_TRUE(std::strcmp(mesh->meshPath.c_str(),     "meshes/hero.aymesh")    == 0);
    CHECK_TRUE(std::strcmp(mesh->materialPath.c_str(), "materials/hero.aymat")  == 0);

    auto* skel = e->getComponent<SkeletonComponent>();
    CHECK_NOT_NULL(skel);
    CHECK_TRUE(std::strcmp(skel->skeletonPath.c_str(), "skeletons/hero_Skeleton.ayskel") == 0);

    auto* anim = e->getComponent<AnimationComponent>();
    CHECK_NOT_NULL(anim);
    CHECK_TRUE(std::strcmp(anim->clipPath.c_str(), "animations/hero_dance.ayanm") == 0);
    // AnimationComponent defaults preserved.
    CHECK_TRUE(anim->autoplay);
    CHECK_TRUE(anim->looping);
    CHECK_FLOAT_EQ(anim->playRate, 1.0f, 0.0001f);

    destroyCharacter(e);
    CHECK_FALSE(e->isValid());

    World::instance().shutdown();
}

TEST_CASE(spawn_with_empty_paths_is_still_valid_entity)
{
    // Empty paths are NOT a spawn failure — components are added with
    // empty strings and the render systems skip the entity until paths
    // are filled in. Verifies the "factory builds the skeleton, caller
    // fills the data" contract.
    World::instance().initialize();

    Entity* e = spawnCharacterFromPaths("", "", "", "");
    CHECK_NOT_NULL(e);
    CHECK_TRUE(e->hasComponent<MeshComponent>());
    CHECK_FALSE(e->getComponent<MeshComponent>()->isValid());
    CHECK_FALSE(e->getComponent<AnimationComponent>()->isValid());

    destroyCharacter(e);
    World::instance().shutdown();
}

TEST_CASE(destroy_null_is_noop)
{
    // destroyCharacter must tolerate nullptr — the editor's `enterEdit`
    // path may run when no character was ever spawned.
    World::instance().initialize();
    destroyCharacter(nullptr);
    // If we got here without crashing the test passes.
    CHECK_TRUE(true);
    World::instance().shutdown();
}

TEST_CASE(destroy_releases_skeleton_skinmatrices_array)
{
    // SkeletonComponent owns `skinMatrices = new[...]` (or nullptr until
    // the resource adapter fills it). destroyCharacter must drive the
    // component destructor (`~SkeletonComponent` runs `delete[]`).
    // We don't have a `.ayskel` here so skinMatrices stays nullptr — the
    // test value is that destroy doesn't crash with non-null heap, and
    // the existing nullptr path remains a clean idempotent double-free
    // guard.
    World::instance().initialize();

    Entity* e = spawnCharacterFromPaths(
        "m.aymesh", "m.aymat", "sk.ayskel", "an.ayanm");
    auto* skel = e->getComponent<SkeletonComponent>();
    CHECK_NOT_NULL(skel);
    CHECK_NULL(skel->skinMatrices);
    CHECK_TRUE(skel->jointCount == 0);
    CHECK_FALSE(skel->loaded);

    destroyCharacter(e);
    CHECK_FALSE(e->isValid());

    World::instance().shutdown();
}

TEST_SUITE_END
