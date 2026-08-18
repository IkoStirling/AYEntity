// ComponentTest.cpp - 组件测试

#include <AYEntity.h>
#include <AYEntity/components/TransformComponent.h>
#include <AYEntity/components/MeshComponent.h>
#include <AYEntity/components/HealthComponent.h>
#include <AYEntity/components/RigidBodyComponent.h>
#include <AYEntity/components/ScriptComponent.h>
#include <AYEntity/components/NetworkComponent.h>
#include <AYTest.h>
#include <algorithm>
#include <cstring>

using namespace ayt::entity;

TEST_SUITE(Component)

TEST_CASE(add_and_get_component)
{
    World::instance().initialize();

    Entity* e = Entity::create();

    Transform t;
    t.position = {1.0f, 2.0f, 3.0f};

    e->addComponent<Transform>(t);

    CHECK_TRUE(e->hasComponent<Transform>());

    Transform* got = e->getComponent<Transform>();
    CHECK_NOT_NULL(got);
    CHECK_FLOAT_EQ(got->position.x, 1.0f, 0.001f);
    CHECK_FLOAT_EQ(got->position.y, 2.0f, 0.001f);
    CHECK_FLOAT_EQ(got->position.z, 3.0f, 0.001f);

    Entity::destroy(e);
    World::instance().shutdown();
}

TEST_CASE(remove_component)
{
    World::instance().initialize();

    Entity* e = Entity::create();

    e->addComponent<Transform>();
    CHECK_TRUE(e->hasComponent<Transform>());

    e->removeComponent<Transform>();
    CHECK_FALSE(e->hasComponent<Transform>());

    Entity::destroy(e);
    World::instance().shutdown();
}

TEST_CASE(multiple_components)
{
    World::instance().initialize();

    Entity* e = Entity::create();

    e->addComponent<Transform>();
    e->addComponent<MeshComponent>("cube.aymesh", "default.aymat");

    CHECK_TRUE(e->hasComponent<Transform>());
    CHECK_TRUE(e->hasComponent<MeshComponent>());

    MeshComponent* mesh = e->getComponent<MeshComponent>();
    CHECK_NOT_NULL(mesh);
    CHECK_TRUE(mesh->isValid());

    Entity::destroy(e);
    World::instance().shutdown();
}

TEST_CASE(health_component)
{
    World::instance().initialize();

    Entity* e = Entity::create();

    auto* health = e->addComponent<HealthComponent>();
    CHECK_NOT_NULL(health);
    CHECK_TRUE(strcmp(health->getName(), "Health") == 0);

    // onStart should initialize hp
    e->onStart();

    CHECK_INT_EQ(health->getHp(), 100);
    CHECK_INT_EQ(health->getMaxHp(), 100);

    health->damage(30);
    CHECK_INT_EQ(health->getHp(), 70);

    health->heal(20);
    CHECK_INT_EQ(health->getHp(), 90);

    health->kill();
    CHECK_TRUE(health->isDead());

    Entity::destroy(e);
    World::instance().shutdown();
}

TEST_CASE(rigid_body_component)
{
    World::instance().initialize();

    Entity* e = Entity::create();

    auto* rb = e->addComponent<RigidBodyComponent>();
    CHECK_NOT_NULL(rb);
    CHECK_TRUE(strcmp(rb->getName(), "RigidBody") == 0);

    // 验证默认值
    CHECK_FLOAT_EQ(rb->getMass(), 1.0f, 0.001f);
    CHECK_FALSE(rb->isStatic());
    CHECK_FALSE(rb->isKinematic());

    // 验证速度设置
    rb->setVelocity(1.0f, 2.0f, 3.0f);
    CHECK_FLOAT_EQ(rb->getVelocityX(), 1.0f, 0.001f);
    CHECK_FLOAT_EQ(rb->getVelocityY(), 2.0f, 0.001f);
    CHECK_FLOAT_EQ(rb->getVelocityZ(), 3.0f, 0.001f);

    // 验证力的施加
    rb->applyForce(10.0f, 20.0f, 30.0f);
    rb->clearForces();

    // 验证物理状态切换
    rb->setStatic(true);
    CHECK_TRUE(rb->isStatic());

    rb->setKinematic(true);
    CHECK_TRUE(rb->isKinematic());

    // 验证碰撞属性
    rb->setRestitution(0.5f);
    CHECK_FLOAT_EQ(rb->getRestitution(), 0.5f, 0.001f);

    rb->setFriction(0.8f);
    CHECK_FLOAT_EQ(rb->getFriction(), 0.8f, 0.001f);

    Entity::destroy(e);
    World::instance().shutdown();
}

TEST_CASE(transform_interpolates_between_simulation_poses)
{
    Transform transform;
    transform.applySimulationPose({0.0f, 0.0f, 0.0f},
                                  {0.0f, 0.0f, 0.0f, 1.0f});
    transform.applySimulationPose({10.0f, 4.0f, -2.0f},
                                  {0.0f, 0.0f, 0.0f, 1.0f});

    const auto halfway = transform.interpolatedPosition(0.5f);
    CHECK_FLOAT_EQ(halfway.x, 5.0f, 0.001f);
    CHECK_FLOAT_EQ(halfway.y, 2.0f, 0.001f);
    CHECK_FLOAT_EQ(halfway.z, -1.0f, 0.001f);

    CHECK_FLOAT_EQ(transform.interpolatedPosition(-1.0f).x, 0.0f, 0.001f);
    CHECK_FLOAT_EQ(transform.interpolatedPosition(2.0f).x, 10.0f, 0.001f);
}

TEST_CASE(script_component)
{
    World::instance().initialize();

    Entity* e = Entity::create();

    auto* script = e->addComponent<ScriptComponent>();
    CHECK_NOT_NULL(script);
    CHECK_TRUE(strcmp(script->getName(), "ScriptComponent") == 0);

    // 验证脚本名称设置
    script->setScriptName("PlayerAI");
    CHECK_TRUE(strcmp(script->getScriptName(), "PlayerAI") == 0);

    // 验证桥接（当前为空）
    CHECK_NULL(script->getBridge());

    // 验证实体引用
    CHECK_TRUE(script->getEntity() == e);

    // 验证方法调用（当前无实际实现）
    CHECK_FALSE(script->callScriptMethod("onStart"));

    Entity::destroy(e);
    World::instance().shutdown();
}

TEST_CASE(network_component)
{
    World::instance().initialize();

    Entity* e = Entity::create();

    auto* net = e->addComponent<NetworkComponent>();
    CHECK_NOT_NULL(net);
    CHECK_TRUE(strcmp(net->getName(), "NetworkComponent") == 0);

    // 验证默认值
    CHECK_FALSE(net->isOwner());
    CHECK_TRUE(net->isStreamed());
    CHECK_FLOAT_EQ(net->getReplicationPriority(), 1.0f, 0.001f);
    CHECK_INT_EQ(net->getReplicationChannel(), 0);

    // 验证网络ID设置
    net->setNetId(12345);
    CHECK_INT_EQ(net->getNetId(), 12345);
    CHECK_TRUE(net->isValid());

    // 验证所有者设置
    net->setOwner(true);
    CHECK_TRUE(net->isOwner());

    // 验证同步控制
    net->setStreamed(false);
    CHECK_FALSE(net->isStreamed());

    // 验证复制优先级
    net->setReplicationPriority(0.5f);
    CHECK_FLOAT_EQ(net->getReplicationPriority(), 0.5f, 0.001f);

    // 验证复制通道
    net->setReplicationChannel(1);
    CHECK_INT_EQ(net->getReplicationChannel(), 1);

    Entity::destroy(e);
    World::instance().shutdown();
}

TEST_CASE(transform_revision_tracks_setter_writes)
{
    Transform t;
    CHECK_INT_EQ(t.revision, 0u);

    t.setPosition(1.0f, 2.0f, 3.0f);
    CHECK_INT_EQ(t.revision, 1u);
    t.setRotation(0.0f, 0.0f, 0.0f, 1.0f);
    CHECK_INT_EQ(t.revision, 2u);
    t.setScale(2.0f, 2.0f, 2.0f);
    CHECK_INT_EQ(t.revision, 3u);
    t.setScale(3.0f);
    CHECK_INT_EQ(t.revision, 4u);
    t.translate(1.0f, 0.0f, 0.0f);
    CHECK_INT_EQ(t.revision, 5u);
    t.scaleBy(2.0f);
    CHECK_INT_EQ(t.revision, 6u);

    // Direct field writes bypass the revision counter; sync layers use
    // poseEquals as the fallback for those.
    t.position = {9.0f, 9.0f, 9.0f};
    CHECK_INT_EQ(t.revision, 6u);

    // Physics write-back is engine-internal, not a user mutation.
    t.applySimulationPose({0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f, 1.0f});
    CHECK_INT_EQ(t.revision, 6u);
}

TEST_SUITE_END
