#pragma once
// AYTransform.h - transform component

#include <AYCore.h>
#include <IAYEntity.h>

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
    }

    AY_PROPERTY(math::FVector3, position, kAttrSerialize)
    AY_PROPERTY(math::FQuaternion, rotation, kAttrSerialize)
    AY_PROPERTY(math::FVector3, scale, kAttrSerialize)

    void setPosition(float x, float y, float z)
    {
        position = {x, y, z};
    }

    void setRotation(float x, float y, float z, float w)
    {
        rotation = {x, y, z, w};
    }

    void setScale(float x, float y, float z)
    {
        scale = {x, y, z};
    }

    void setScale(float uniform)
    {
        scale = {uniform, uniform, uniform};
    }

    void translate(float dx, float dy, float dz)
    {
        position.x += dx;
        position.y += dy;
        position.z += dz;
    }

    void scaleBy(float factor)
    {
        scale.x *= factor;
        scale.y *= factor;
        scale.z *= factor;
    }
};
#undef AY_CURRENT_CLASS

} // namespace ayt::entity
