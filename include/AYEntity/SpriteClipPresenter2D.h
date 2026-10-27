#pragma once

#include <AY2D/SpriteClip2D.h>
#include <AYEntity/components/SpriteComponent.h>
#include <AYEntity/components/TransformComponent.h>

namespace ayt::entity {

namespace detail {
inline bool presentSpriteClipFrame2D(const ay2d::SpriteClipSet2D& set,
                                     const ay2d::SpriteClipPlayer2D& player,
                                     float unitsPerPixel,
                                     Transform& visualTransform,
                                     SpriteComponent& sprite,
                                     bool mirrorX, bool ownFlip) {
    const auto* frame = player.frame();
    if (!frame) return false;
    ay2d::SpriteClipPose2D pose;
    if (!ay2d::spriteClipPose2D(set, *frame, unitsPerPixel,
                                mirrorX, pose)) return false;
    if (sprite.texturePath != pose.texturePath) {
        sprite.texturePath = pose.texturePath;
        sprite.residentAlbedoTextureId = 0;
    }
    sprite.sourceRectMin = {pose.u0, pose.v0};
    sprite.sourceRectMax = {pose.u1, pose.v1};
    if (ownFlip) sprite.flip = (sprite.flip & ~1) | (mirrorX ? 1 : 0);
    visualTransform.setPresentationPose(
        {pose.centerX, pose.centerY, visualTransform.position.z},
        visualTransform.rotation);
    visualTransform.setScale(pose.width, pose.height,
                             visualTransform.scale.z);
    return true;
}
} // namespace detail

/// Present a named clip on a visual child Entity whose local origin is the
/// gameplay actor's anchor. The actor's physics Transform is never written.
/// Call before SpriteRenderSystem and do not attach SpriteAnimationComponent
/// to the same Sprite. A caller owns the clip set and its texture residency.
/// This legacy form leaves Sprite.flip untouched; use the explicit mirrorX
/// overload when reflecting a character around its logical pivot.
inline bool presentSpriteClip2D(const ay2d::SpriteClipSet2D& set,
                                const ay2d::SpriteClipPlayer2D& player,
                                float unitsPerPixel,
                                Transform& visualTransform,
                                SpriteComponent& sprite) {
    return detail::presentSpriteClipFrame2D(set, player, unitsPerPixel,
                                             visualTransform, sprite,
                                             false, false);
}

/// Set the horizontal UV flip and reflect the trimmed quad about its authored
/// logical pivot. Other Sprite.flip bits are preserved. Socket attachments
/// should use spriteClipSocket2D(..., mirrorX, ...).
inline bool presentSpriteClip2D(const ay2d::SpriteClipSet2D& set,
                                const ay2d::SpriteClipPlayer2D& player,
                                float unitsPerPixel,
                                Transform& visualTransform,
                                SpriteComponent& sprite,
                                bool mirrorX) {
    return detail::presentSpriteClipFrame2D(set, player, unitsPerPixel,
                                             visualTransform, sprite,
                                             mirrorX, true);
}

} // namespace ayt::entity
