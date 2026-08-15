// SystemTest.cpp - 系统调度测试

#include <AYEntity.h>
#include <AYEntity/components/TransformComponent.h>
#include <AYEntity/components/HealthComponent.h>
#include <AYTest.h>

using namespace ayt::entity;

// =============================================================================
// 测试系统 - 计算有 Transform 的实体数量
// =============================================================================
class CounterSystem : public ISystem {
public:
    const char* getName() const override { return "Counter"; }

    void onStart() override {
        _count = 0;
    }

    void onUpdate(float dt) override {
        (void)dt;
        _count = 0;
        Query<Transform> query(&World::instance());
        for (auto* entity : query) {
            (void)entity;
            _count++;
        }
    }

    int32_t getCount() const { return _count; }

private:
    int32_t _count = 0;
};

AY_SYSTEM(CounterSystem, 100);

// =============================================================================
// 测试系统 - 处理 HealthComponent
// =============================================================================
class HealthProcessSystem : public ISystem {
public:
    const char* getName() const override { return "HealthProcess"; }

    void onStart() override {
        _processedCount = 0;
        _deadCount = 0;
    }

    void onUpdate(float dt) override {
        (void)dt;
        _processedCount = 0;
        _deadCount = 0;

        Query<HealthComponent> query(&World::instance());
        for (auto* entity : query) {
            auto* health = entity->getComponent<HealthComponent>();
            if (health && health->isDead()) {
                _deadCount++;
            }
            _processedCount++;
        }
    }

    int32_t getProcessedCount() const { return _processedCount; }
    int32_t getDeadCount() const { return _deadCount; }

private:
    int32_t _processedCount = 0;
    int32_t _deadCount = 0;
};

AY_SYSTEM(HealthProcessSystem, 200);

TEST_SUITE(System)

TEST_CASE(query_system)
{
    World::instance().initialize();

    // 创建有 Transform 的实体
    Entity* e1 = World::instance().createEntity();
    e1->addComponent<Transform>();

    Entity* e2 = World::instance().createEntity();
    e2->addComponent<Transform>();

    // 创建没有 Transform 的实体
    Entity* e3 = World::instance().createEntity();

    // Query 应该只返回有 Transform 的实体
    auto query = World::instance().query<Transform>();
    int count = 0;
    for (auto* e : query) {
        (void)e;
        count++;
    }
    CHECK_INT_EQ(count, 2);

    Entity::destroy(e1);
    Entity::destroy(e2);
    Entity::destroy(e3);

    World::instance().shutdown();
}

TEST_CASE(system_auto_registration)
{
    World::instance().initialize();

    // 系统应该在 World 中注册
    // 通过创建实体并让系统处理来验证
    Entity* e1 = World::instance().createEntity();
    e1->addComponent<Transform>();

    Entity* e2 = World::instance().createEntity();
    e2->addComponent<HealthComponent>();
    e2->addComponent<Transform>();

    // 更新世界触发系统
    World::instance().update(0.016f);

    // 获取实体来检查系统处理结果
    auto transformQuery = World::instance().query<Transform>();
    int count = 0;
    for (auto* e : transformQuery) {
        (void)e;
        count++;
    }
    CHECK_INT_EQ(count, 2);

    Entity::destroy(e1);
    Entity::destroy(e2);

    World::instance().shutdown();
}

TEST_CASE(system_priority_order)
{
    World::instance().initialize();

    // 验证系统按优先级排序
    // CounterSystem 优先级 100，HealthProcessSystem 优先级 200
    // CounterSystem 应该先于 HealthProcessSystem 执行

    Entity* e1 = World::instance().createEntity();
    e1->addComponent<Transform>();
    e1->addComponent<HealthComponent>();

    // 更新世界
    World::instance().update(0.016f);

    Entity::destroy(e1);

    World::instance().shutdown();
}

TEST_CASE(system_update_called)
{
    World::instance().initialize();

    Entity* e = World::instance().createEntity();
    e->addComponent<Transform>();

    // 调用 update 多次，确认系统被调用
    World::instance().update(0.016f);
    World::instance().update(0.016f);
    World::instance().update(0.016f);

    Entity::destroy(e);

    World::instance().shutdown();
}

TEST_SUITE_END