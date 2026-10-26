#pragma once

#include <AYCore.h>
#include <AYEntity/IEntity.h>

#include <cmath>
#include <string>

namespace ayt::entity {

#define AY_CURRENT_CLASS SceneDecalComponent
/// Spatial projector authored in a Scene. The Entity world transform maps
/// local cube [-1,1] to its volume; local +Z faces the receiver. Keep its
/// parent chain orthogonal with positive scale. Texture paths are optional
/// asset-root-relative files; rendering resolves them on the owner thread.
struct SceneDecalComponent : IComponent {
    const char* getName() const override { return "SceneDecalComponent"; }
    AY_PROPERTY(bool, enabled, kAttrSerialize)
    AY_PROPERTY(int32_t, priority, kAttrSerialize)
    AY_PROPERTY(math::FVector3, color, kAttrSerialize)
    AY_PROPERTY(float, opacity, kAttrSerialize)
    AY_PROPERTY(float, normalStrength, kAttrSerialize)
    AY_PROPERTY(float, roughness, kAttrSerialize)
    AY_PROPERTY(float, metallic, kAttrSerialize)
    AY_PROPERTY(float, materialStrength, kAttrSerialize)
    AY_PROPERTY(float, minFacing, kAttrSerialize)
    AY_PROPERTY(std::string, albedoPath, kAttrSerialize)
    AY_PROPERTY(std::string, normalPath, kAttrSerialize)

    SceneDecalComponent() : enabled(true), priority(0), color(1,1,1),
        opacity(1), normalStrength(0), roughness(.5f), metallic(0),
        materialStrength(0), minFacing(.1f) {}

    /// Scene serialization refuses invalid authoring values. Direct runtime
    /// writes are skipped by extraction until corrected.
    bool valid() const noexcept {
        const auto in = [](float value, float low, float high) {
            return std::isfinite(value) && value >= low && value <= high;
        };
        return in(color.x,0,1) && in(color.y,0,1) && in(color.z,0,1)
            && in(opacity,0,1) && in(normalStrength,0,1)
            && in(roughness,.045f,1) && in(metallic,0,1)
            && in(materialStrength,0,1) && in(minFacing,0,.99f)
            && albedoPath.size() <= 512 && normalPath.size() <= 512;
    }
};
#undef AY_CURRENT_CLASS

} // namespace ayt::entity
