#pragma once

#include <AY2D/ActionClip2D.h>
#include <AYEntity/components/SpriteComponent.h>

namespace ayt::entity {

/// Optional read-only presentation adapter for the ordinary fixed-step 2D
/// action runtime. Call after ActionRuntime2D::step and do not enable the
/// independent SpriteAnimationSystem on this sprite at the same time.
inline bool presentActionFrame2D(const ayt::ay2d::ActionStep2D& step,
                                 SpriteComponent& sprite) noexcept {
    if (!step.active) return false;
    sprite.sourceRectMin = math::FVector2(step.uv.minX, step.uv.minY);
    sprite.sourceRectMax = math::FVector2(step.uv.maxX, step.uv.maxY);
    sprite.flip = (sprite.flip & ~1) | (step.facingRight ? 0 : 1);
    return true;
}

} // namespace ayt::entity
