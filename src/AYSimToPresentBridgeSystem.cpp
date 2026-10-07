#include <AYEntity/SimToPresentBridgeSystem.h>

#include <AYEntity.h>
#include <AYEntity/components/SimTransformComponent.h>
#include <AYEntity/components/DetSimTransformComponent.h>
#include <AYEntity/components/TransformComponent.h>

namespace ayt::entity
{
namespace
{

float clampAlpha(float alpha) noexcept
{
    if ((std::bit_cast<std::uint32_t>(alpha) & 0x7fffffffu) > 0x7f800000u) return 0.0f;
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
        auto* det = entity->getComponent<DetSimTransformComponent>();
        auto* present = entity->getComponent<Transform>();
        if (present == nullptr || (sim == nullptr && det == nullptr) || (sim && det)) continue;

        if (det) {
            const auto previous = det->hasPreviousPosition ? det->previousPosition : det->position;
            if (!previous.isFinite() || !det->position.isFinite()) continue;
            std::optional<math::DetQuaternion> rotationSample;
            if (det->rotationEnabled) {
                const auto from = det->hasPreviousRotation ? det->previousRotation : det->rotation;
                rotationSample = math::DetQuaternion::nlerp(from,det->rotation,math::DetFloat32::fromFloat(alpha));
                if (!rotationSample) continue; // Reject invalid pose before any Present writes.
            }
            const auto blend = [alpha](math::DetFloat32 a, math::DetFloat32 b) {
                if (alpha == 0.0f) return a.toFloat();
                if (alpha == 1.0f) return b.toFloat();
                return static_cast<float>((1.0 - static_cast<double>(alpha)) * a.toFloat()
                    + static_cast<double>(alpha) * b.toFloat());
            };
            present->position = {
                blend(previous.x, det->position.x), blend(previous.y, det->position.y), blend(previous.z, det->position.z)};
            if (rotationSample) {
                const auto fields = rotationSample->toFloats();
                present->rotation = {fields[0],fields[1],fields[2],fields[3]};
                present->previousRotation = present->rotation;
            }
        } else {
            const math::FixedVec3& previous = sim->hasPreviousPosition
                ? sim->previousPosition : sim->position;
            present->position = {
                interpolate(previous.x, sim->position.x, alpha),
                interpolate(previous.y, sim->position.y, alpha),
                interpolate(previous.z, sim->position.z, alpha),
            };
        }

        // The bridge already produced the interpolated presentation sample.
        // Disable Transform's physics interpolation to avoid applying alpha a
        // second time in render systems. Optional Det rotation is already
        // sampled above; legacy rotation and all scale values remain untouched.
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
