#pragma once
#include <AYCore.h>
#include <AYEntity/IEntity.h>
#include <cmath>

namespace ayt::entity {
enum class SceneLightType : int32_t { Directional=0, Point=1, Spot=2 };
#define AY_CURRENT_CLASS LightComponent
/// Spatial Scene light, registered by EntityRenderIntegrationModule. Local +Z
/// is the emission direction; parent rotation applies, scale never changes range.
/// Values are linear RGB and unitless strength, range is meters, cone values are
/// half angles in degrees. Point shadows remain unsupported by the renderer.
struct LightComponent : IComponent {
    const char* getName() const override { return "LightComponent"; }
    AY_PROPERTY(int32_t, type, kAttrSerialize)
    AY_PROPERTY(bool, enabled, kAttrSerialize)
    AY_PROPERTY(math::FVector3, color, kAttrSerialize)
    AY_PROPERTY(float, intensity, kAttrSerialize)
    AY_PROPERTY(float, range, kAttrSerialize)
    AY_PROPERTY(float, innerAngleDegrees, kAttrSerialize)
    AY_PROPERTY(float, outerAngleDegrees, kAttrSerialize)
    AY_PROPERTY(bool, castShadow, kAttrSerialize)
    AY_PROPERTY(float, shadowBias, kAttrSerialize)
    AY_PROPERTY(int32_t, priority, kAttrSerialize)
    LightComponent() : type(0), enabled(true), color(1,1,1), intensity(1), range(10),
        innerAngleDegrees(20), outerAngleDegrees(30), castShadow(false), shadowBias(0), priority(0) {}
    /// Invalid values are refused by Scene serialization and editor commands;
    /// direct runtime writes are skipped by extraction instead of emitting NaNs.
    bool valid() const noexcept {
        const auto in=[](float v,float a,float b){return std::isfinite(v)&&v>=a&&v<=b;};
        return type>=0&&type<=2&&in(color.x,0,64)&&in(color.y,0,64)&&in(color.z,0,64)
            &&in(intensity,0,100000)&&in(range,.001f,100000)&&in(innerAngleDegrees,.1f,89)
            &&in(outerAngleDegrees,.1f,89)&&innerAngleDegrees<=outerAngleDegrees&&in(shadowBias,0,.05f)
            &&!(type==int32_t(SceneLightType::Point)&&castShadow);
    }
};
#undef AY_CURRENT_CLASS
} // namespace ayt::entity
