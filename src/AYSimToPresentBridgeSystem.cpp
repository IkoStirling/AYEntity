#include <AYEntity/SimToPresentBridgeSystem.h>

#include <AYEntity.h>
#include <AYEntity/components/SimTransformComponent.h>
#include <AYEntity/components/TransformComponent.h>

namespace ayt::entity
{
namespace
{

float clampAlpha(float alpha) noexcept
{
    if (alpha < 0.0f) return 0.0f;
    if (alpha > 1.0f) return 1.0f;
    return alpha;
}

float interpolate(math::Fixed32 previous,
                  math::Fixed32 current,
                  float alpha) noexcept
{
    const float from = previous.toFloat();
    return from + (current.toFloat() - from) * alpha;
}

} // namespace

void SimToPresentBridgeSystem::onUpdate(float interpolationAlpha)
{
    const float alpha = clampAlpha(interpolationAlpha);

    // getAllEntities() follows monotonically allocated entity IDs, so bridge
    // writes have a stable order even after SparseSet swap-removal.
    for (Entity* entity : World::instance().getAllEntities()) {
        auto* sim = entity->getComponent<SimTransformComponent>();
        auto* present = entity->getComponent<Transform>();
        if (sim == nullptr || present == nullptr) continue;

        const math::FixedVec3& previous = sim->hasPreviousPosition
            ? sim->previousPosition
            : sim->position;

        present->position = {
            interpolate(previous.x, sim->position.x, alpha),
            interpolate(previous.y, sim->position.y, alpha),
            interpolate(previous.z, sim->position.z, alpha),
        };

        // The bridge already produced the interpolated presentation sample.
        // Disable Transform's physics interpolation to avoid applying alpha a
        // second time in render systems. Rotation/scale remain untouched.
        present->previousPosition = present->position;
        present->hasPreviousSimulationPose = false;
    }
}

void registerSimToPresentBridgeSystem()
{
    World::instance().registerSystem<SimToPresentBridgeSystem>(
        SimToPresentBridgeSystem::kPriority,
        SystemLane::Bridge);
}

} // namespace ayt::entity
