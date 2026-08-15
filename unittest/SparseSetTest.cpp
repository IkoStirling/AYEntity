// SparseSetTest.cpp - SparseSet 存储测试

#include <AYEntity/SparseSet.h>
#include <AYTest.h>

using namespace ayt::entity;

TEST_SUITE(SparseSet)

TEST_CASE(basic_operations)
{
    SparseSet<int> set;

    // Empty check
    CHECK_TRUE(set.size() == 0);
    CHECK_FALSE(set.has(1));

    // Add
    int val = 42;
    set.add(1, &val);
    CHECK_TRUE(set.size() == 1);
    CHECK_TRUE(set.has(1));

    int* got = set.getComponent(1);
    CHECK_NOT_NULL(got);
    CHECK_INT_EQ(*got, 42);

    // Remove
    set.remove(1);
    CHECK_TRUE(set.size() == 0);
    CHECK_FALSE(set.has(1));
}

TEST_CASE(sparse_set_compaction)
{
    SparseSet<int> set;

    // Add multiple
    int v1 = 10, v2 = 20, v3 = 30;
    set.add(1, &v1);
    set.add(5, &v2);
    set.add(10, &v3);

    CHECK_INT_EQ((int)set.size(), 3);

    // Remove middle
    set.remove(5);
    CHECK_INT_EQ((int)set.size(), 2);
    CHECK_FALSE(set.has(5));
    CHECK_TRUE(set.has(1));
    CHECK_TRUE(set.has(10));

    // Verify compaction - 10 should still be accessible
    CHECK_NOT_NULL(set.getComponent(10));
}

TEST_CASE(clear_all)
{
    SparseSet<int> set;

    int v1 = 10, v2 = 20;
    set.add(1, &v1);
    set.add(2, &v2);

    CHECK_INT_EQ((int)set.size(), 2);

    set.clear();
    CHECK_TRUE(set.size() == 0);
    CHECK_FALSE(set.has(1));
    CHECK_FALSE(set.has(2));
}

TEST_CASE(for_each_iteration)
{
    SparseSet<int> set;

    int v1 = 10, v2 = 20, v3 = 30;
    set.add(1, &v1);
    set.add(2, &v2);
    set.add(3, &v3);

    int sum = 0;
    int count = 0;
    set.forEach([&sum, &count](uint32_t entityId, void* comp) {
        (void)entityId;
        sum += *(int*)comp;
        count++;
    });

    CHECK_INT_EQ(count, 3);
    CHECK_INT_EQ(sum, 60);
}

TEST_SUITE_END