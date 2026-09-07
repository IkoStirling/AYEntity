#include <AYEntity.h>
#include <AYEntity/SimToPresentBridgeSystem.h>
#include <AYEntity/components/SimTransformComponent.h>
#include <AYEntity/components/TransformComponent.h>
#include <AYMath/Fixed.h>
#include <AYTest.h>

#include <cstddef>

namespace ayt::entity::test
{
namespace
{

struct LaneProbeState {
    int presentStarts = 0;
    int presentUpdates = 0;
    float presentValue = 0.0f;
    int simStarts = 0;
    int simUpdates = 0;
    float simValue = 0.0f;
    int bridgeStarts = 0;
    int bridgeUpdates = 0;
    float bridgeValue = 0.0f;
};

LaneProbeState g_laneProbe;

class PresentLaneProbe final : public ISystem {
public:
    const char* getName() const override { return "PresentLaneProbe"; }
    void onStart() override { ++g_laneProbe.presentStarts; }
    void onUpdate(float value) override
    {
        ++g_laneProbe.presentUpdates;
        g_laneProbe.presentValue = value;
    }
};

class SimLaneProbe final : public ISystem {
public:
    const char* getName() const override { return "SimLaneProbe"; }
    void onStart() override { ++g_laneProbe.simStarts; }
    void onUpdate(float value) override
    {
        ++g_laneProbe.simUpdates;
        g_laneProbe.simValue = value;
    }
};

class BridgeLaneProbe final : public ISystem {
public:
    const char* getName() const override { return "BridgeLaneProbe"; }
    void onStart() override { ++g_laneProbe.bridgeStarts; }
    void onUpdate(float value) override
    {
        ++g_laneProbe.bridgeUpdates;
        g_laneProbe.bridgeValue = value;
    }
};

class FixedTranslationProbe final : public ISystem {
public:
    const char* getName() const override { return "FixedTranslationProbe"; }

    void onUpdate(float) override
    {
        const math::FixedVec3 delta{
            math::Fixed32::fromInt(8),
            math::Fixed32::fromInt(-4),
            math::Fixed32::fromInt(2),
        };
        for (Entity* entity : World::instance().getAllEntities()) {
            if (auto* transform = entity->getComponent<SimTransformComponent>()) {
                transform->translate(delta);
            }
        }
    }
};

SystemLane laneOf(const World& world, const char* name)
{
    for (std::size_t i = 0; i < world.systemCount(); ++i) {
        if (std::string(world.getSystemNameAt(i)) == name) {
            return world.getSystemLaneAt(i);
        }
    }
    return SystemLane::Present;
}

} // namespace

TEST_SUITE(DeterministicLane)

TEST_CASE(system_lanes_have_separate_time_contracts)
{
    World& world = World::instance();
    world.initialize();
    g_laneProbe = {};

    world.registerSystem<PresentLaneProbe>(-300, SystemLane::Present);
    world.registerSystem<SimLaneProbe>(-300, SystemLane::Sim);
    world.registerSystem<BridgeLaneProbe>(-300, SystemLane::Bridge);

    CHECK(laneOf(world, "PresentLaneProbe") == SystemLane::Present);
    CHECK(laneOf(world, "SimLaneProbe") == SystemLane::Sim);
    CHECK(laneOf(world, "BridgeLaneProbe") == SystemLane::Bridge);

    world.fixedUpdate(0.125f);
    CHECK_INT_EQ(g_laneProbe.simStarts, 1);
    CHECK_INT_EQ(g_laneProbe.simUpdates, 1);
    CHECK_FLOAT_EQ(g_laneProbe.simValue, 0.125f, 0.0001f);
    CHECK_INT_EQ(g_laneProbe.presentUpdates, 0);
    CHECK_INT_EQ(g_laneProbe.bridgeUpdates, 0);

    world.updatePresentation(0.02f, 0.25f);
    CHECK_INT_EQ(g_laneProbe.simUpdates, 1);
    CHECK_INT_EQ(g_laneProbe.presentStarts, 1);
    CHECK_INT_EQ(g_laneProbe.presentUpdates, 1);
    CHECK_FLOAT_EQ(g_laneProbe.presentValue, 0.02f, 0.0001f);
    CHECK_INT_EQ(g_laneProbe.bridgeStarts, 1);
    CHECK_INT_EQ(g_laneProbe.bridgeUpdates, 1);
    CHECK_FLOAT_EQ(g_laneProbe.bridgeValue, 0.25f, 0.0001f);

    // onStart is once per system, regardless of lane tick count.
    world.fixedUpdate(0.125f);
    world.updatePresentation(0.02f, 0.75f);
    CHECK_INT_EQ(g_laneProbe.simStarts, 1);
    CHECK_INT_EQ(g_laneProbe.presentStarts, 1);
    CHECK_INT_EQ(g_laneProbe.bridgeStarts, 1);

    world.shutdown();
}

TEST_CASE(sim_to_present_bridge_interpolates_fixed_translation)
{
    World& world = World::instance();
    world.initialize();
    world.registerSystem<FixedTranslationProbe>(0, SystemLane::Sim);

    Entity* entity = world.createEntity();
    CHECK_NOT_NULL(entity);
    Transform* present = entity->addComponent<Transform>();
    SimTransformComponent* sim = entity->addComponent<SimTransformComponent>();
    CHECK_NOT_NULL(present);
    CHECK_NOT_NULL(sim);

    present->rotation = {0.0f, 0.0f, 0.5f, 0.5f};
    present->scale = {2.0f, 3.0f, 4.0f};
    sim->setPosition({
        math::Fixed32::fromInt(2),
        math::Fixed32::fromInt(6),
        math::Fixed32::fromInt(-2),
    });

    // World snapshots (2, 6, -2), then the Sim system advances to
    // (10, 2, 0). The bridge must publish the 25% presentation sample.
    world.fixedUpdate(1.0f / 60.0f);
    SimToPresentBridgeSystem bridge;
    bridge.onUpdate(0.25f);

    CHECK_FLOAT_EQ(present->position.x, 4.0f, 0.0001f);
    CHECK_FLOAT_EQ(present->position.y, 5.0f, 0.0001f);
    CHECK_FLOAT_EQ(present->position.z, -1.5f, 0.0001f);
    CHECK_FLOAT_EQ(present->scale.x, 2.0f, 0.0001f);
    CHECK_FLOAT_EQ(present->scale.y, 3.0f, 0.0001f);
    CHECK_FLOAT_EQ(present->rotation.z, 0.5f, 0.0001f);
    CHECK_FALSE(present->hasPreviousSimulationPose);
    CHECK_FLOAT_EQ(present->interpolatedPosition(0.25f).x, 4.0f, 0.0001f);

    bridge.onUpdate(-1.0f);
    CHECK_FLOAT_EQ(present->position.x, 2.0f, 0.0001f);
    bridge.onUpdate(2.0f);
    CHECK_FLOAT_EQ(present->position.x, 10.0f, 0.0001f);

    world.destroyEntity(entity);
    world.shutdown();
}

TEST_SUITE_END

} // namespace ayt::entity::test
