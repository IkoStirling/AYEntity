// EntityTest.cpp - 实体测试

#include <AYEntity.h>
#include <AYTest.h>

using namespace ayt::entity;

TEST_SUITE(Entity)

TEST_CASE(create_and_destroy)
{
    World::instance().initialize();

    Entity* e = Entity::create();
    CHECK_NOT_NULL(e);
    CHECK_TRUE(e->isValid());
    CHECK_TRUE(e->getId() != INVALID_ID);

    Entity::destroy(e);
    CHECK_FALSE(e->isValid());

    World::instance().shutdown();
}

TEST_CASE(entity_with_name)
{
    World::instance().initialize();

    Entity* e1 = Entity::create();
    e1->setName("Player");

    Entity* found = World::instance().findEntity("Player");
    CHECK_NOT_NULL(found);
    CHECK_INT_EQ(found->getId(), e1->getId());

    Entity::destroy(e1);

    World::instance().shutdown();
}

TEST_CASE(entity_multiple)
{
    World::instance().initialize();

    Entity* e1 = Entity::create();
    Entity* e2 = Entity::create();
    Entity* e3 = Entity::create();

    CHECK_TRUE(e2->getId() > e1->getId());
    CHECK_TRUE(e3->getId() > e2->getId());

    auto entities = World::instance().getAllEntities();
    CHECK_INT_EQ((int)entities.size(), 3);

    Entity::destroy(e2);
    entities = World::instance().getAllEntities();
    CHECK_INT_EQ((int)entities.size(), 2);

    Entity::destroy(e1);
    Entity::destroy(e3);

    World::instance().shutdown();
}

TEST_CASE(find_entity_by_id)
{
    World::instance().initialize();

    Entity* e1 = World::instance().createEntity();
    e1->setName("TestEntity");

    Entity* found = World::instance().findEntity(e1->getId());
    CHECK_NOT_NULL(found);
    CHECK_INT_EQ(found->getId(), e1->getId());

    Entity::destroy(e1);
    World::instance().shutdown();
}

TEST_SUITE_END