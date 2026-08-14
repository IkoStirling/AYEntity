// WorldTest.cpp - World 管理器测试

#include <AYEntity.h>
#include <components/AYTransformComponent.h>
#include <components/AYMeshComponent.h>
#include <AYTest.h>

using namespace ayt::entity;

TEST_SUITE(World)

TEST_CASE(singleton)
{
    World::setActiveWorld(nullptr);
    World& w1 = World::instance();
    World& w2 = World::instance();
    CHECK_TRUE(&w1 == &w2);
    CHECK_TRUE(&w1 == &World::processWorld());
}

TEST_CASE(active_world_redirect)
{
    World::setActiveWorld(nullptr);
    CHECK(World::activeWorld() == nullptr);
    CHECK(&World::instance() == &World::processWorld());

    // Scene-owned Worlds use private ctor; redirect API is covered by
    // AYScene SceneManager tests. Here only clear/restore behavior.
    World::setActiveWorld(&World::processWorld());
    CHECK(World::activeWorld() == &World::processWorld());
    World::setActiveWorld(nullptr);
}

TEST_CASE(initialize_shutdown)
{
    CHECK_FALSE(World::instance().initialize() == false);
    World::instance().shutdown();
}

TEST_CASE(entity_lifecycle)
{
    World::instance().initialize();

    Entity* e = World::instance().createEntity();
    CHECK_NOT_NULL(e);

    World::instance().destroyEntity(e);

    World::instance().shutdown();
}

TEST_CASE(query_entities)
{
    World::instance().initialize();

    Entity* e1 = World::instance().createEntity();
    e1->addComponent<Transform>();

    Entity* e2 = World::instance().createEntity();
    e2->addComponent<Transform>();
    e2->addComponent<MeshComponent>();

    Entity* e3 = World::instance().createEntity();
    e3->addComponent<MeshComponent>();

    auto transformQuery = World::instance().query<Transform>();
    int count = 0;
    for (auto* e : transformQuery) {
        (void)e;
        count++;
    }
    CHECK_INT_EQ(count, 2);

    Entity::destroy(e1);
    Entity::destroy(e2);
    Entity::destroy(e3);

    World::instance().shutdown();
}

TEST_SUITE_END