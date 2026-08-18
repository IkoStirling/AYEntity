#pragma once
// AYTransform.h - transform component

#include <AYCore.h>
#include <AYEntity/IEntity.h>

namespace ayt::entity
{

#define AY_CURRENT_CLASS Transform
struct Transform : public IComponent {
    const char* getName() const override { return "Transform"; }

    Transform()
    {
        position = {0.0f, 0.0f, 0.0f};
        rotation = {0.0f, 0.0f, 0.0f, 1.0f};
        scale    = {1.0f, 1.0f, 1.0f};
        previousPosition = position;
        previousRotation = rotation;
    }

    AY_PROPERTY(math::FVector3, position, kAttrSerialize)
    AY_PROPERTY(math::FQuaternion, rotation, kAttrSerialize)
    AY_PROPERTY(math::FVector3, scale, kAttrSerialize)

    // Previous authoritative simulation pose used only for presentation.
    math::FVector3 previousPosition{};
    math::FQuaternion previousRotation{};
    bool hasPreviousSimulationPose = false;

    // Monotonic write counter. Incremented by every mutator below; direct
    // field writes bypass it, so sync layers keep poseEquals as a fallback.
    // Not serialized (runtime-only).
    uint32_t revision = 0;

    void applySimulationPose(const math::FVector3& newPosition,
                             const math::FQuaternion& newRotation)
    {
        if (hasPreviousSimulationPose) {
            previousPosition = position;
            previousRotation = rotation;
        } else {
            previousPosition = newPosition;
            previousRotation = newRotation;
            hasPreviousSimulationPose = true;
        }
        position = newPosition;
        rotation = newRotation;
    }

    math::FVector3 interpolatedPosition(float alpha) const
    {
        if (!hasPreviousSimulationPose) return position;
        alpha = alpha < 0.0f ? 0.0f : (alpha > 1.0f ? 1.0f : alpha);
        return previousPosition + (position - previousPosition) * alpha;
    }

    math::FQuaternion interpolatedRotation(float alpha) const
    {
        if (!hasPreviousSimulationPose) return rotation;
        alpha = alpha < 0.0f ? 0.0f : (alpha > 1.0f ? 1.0f : alpha);
        return previousRotation.slerp(rotation, alpha);
    }

    void setPosition(float x, float y, float z)
    {
        position = {x, y, z};
        ++revision;
    }

    void setRotation(float x, float y, float z, float w)
    {
        rotation = {x, y, z, w};
        ++revision;
    }

    void setScale(float x, float y, float z)
    {
        scale = {x, y, z};
        ++revision;
    }

    void setScale(float uniform)
    {
        scale = {uniform, uniform, uniform};
        ++revision;
    }

    void translate(float dx, float dy, float dz)
    {
        position.x += dx;
        position.y += dy;
        position.z += dz;
        ++revision;
    }

    void scaleBy(float factor)
    {
        scale.x *= factor;
        scale.y *= factor;
        scale.z *= factor;
        ++revision;
    }
};
#undef AY_CURRENT_CLASS

} // namespace ayt::entity
