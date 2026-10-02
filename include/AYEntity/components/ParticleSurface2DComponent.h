#pragma once
#include <AYEntity/IEntity.h>
namespace ayt::entity {
#define AY_CURRENT_CLASS ParticleSurface2DComponent
/// Axis-aligned world-XY area used by CPU particle collision. Ground queries
/// choose the highest marked surface beneath a particle's visual height.
/// Solid areas also provide swept XY collision for side-view effects.
struct ParticleSurface2DComponent : IComponent {
    const char* getName() const override { return "ParticleSurface2DComponent"; }
    AY_PROPERTY(math::FVector2, halfExtent, kAttrSerialize)
    AY_PROPERTY(float, height, kAttrSerialize)
    AY_PROPERTY(int32_t, surfaceTag, kAttrSerialize)
    AY_PROPERTY(int32_t, collisionMask, kAttrSerialize)
    AY_PROPERTY(bool, ground, kAttrSerialize)
    AY_PROPERTY(bool, solid, kAttrSerialize)
    ParticleSurface2DComponent() {
        halfExtent={1,1};height=0;surfaceTag=0;collisionMask=-1;
        ground=true;solid=false;
    }
};
#undef AY_CURRENT_CLASS
}
