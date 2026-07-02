// EntityHandleTest.cpp - 实体句柄测试

#include <AYEntityHandle.h>
#include <AYTest.h>

using namespace ayt::entity;

TEST_SUITE(EntityHandle)

TEST_CASE(handle_allocation)
{
    EntityHandlePool::instance().reset();

    EntityHandlePool& pool = EntityHandlePool::instance();

    EntityHandle h = pool.allocate(1);
    CHECK_TRUE(h.isValid());
    CHECK_INT_EQ(h.id, 1);
    CHECK_INT_EQ(h.version, 0);
}

TEST_CASE(handle_release_increments_version)
{
    EntityHandlePool::instance().reset();

    EntityHandlePool& pool = EntityHandlePool::instance();

    EntityHandle h1 = pool.allocate(5);
    CHECK_INT_EQ(h1.version, 0);

    pool.release(h1);

    EntityHandle h2 = pool.allocate(5);
    CHECK_INT_EQ(h2.version, 1);
    CHECK_TRUE(h2 != h1);
}

TEST_CASE(handle_validation)
{
    EntityHandlePool::instance().reset();

    EntityHandlePool& pool = EntityHandlePool::instance();

    EntityHandle h = pool.allocate(10);
    CHECK_TRUE(pool.isValid(h));

    pool.release(h);

    CHECK_FALSE(pool.isValid(h));
}

TEST_SUITE_END