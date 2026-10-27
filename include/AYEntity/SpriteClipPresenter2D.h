#pragma once

#include <AY2D/SpriteClip2D.h>
#include <AYEntity/components/SpriteComponent.h>
#include <AYEntity/components/TransformComponent.h>

namespace ayt::entity {

/// Present a named clip on a visual child Entity whose local origin is the
/// gameplay actor's anchor. The actor's physics Transform is never written.
/// Call before SpriteRenderSystem and do not attach SpriteAnimationComponent
/// to the same Sprite. A caller owns the clip set and its texture residency.
inline bool presentSpriteClip2D(const ay2d::SpriteClipSet2D& set,
                                const ay2d::SpriteClipPlayer2D& player,
                                float unitsPerPixel,
                                Transform& visualTransform,
                                SpriteComponent& sprite) {
    const auto* frame = player.frame();
    if (!frame) return false;
    ay2d::SpriteClipPose2D pose;
    if (!ay2d::spriteClipPose2D(set, *frame, unitsPerPixel, pose)) return false;
    if (sprite.texturePath != pose.texturePath) {
        sprite.texturePath = pose.texturePath;
        sprite.residentAlbedoTextureId = 0;
    }
    sprite.sourceRectMin = {pose.u0, pose.v0};
    sprite.sourceRectMax = {pose.u1, pose.v1};
    visualTransform.setPresentationPose(
        {pose.centerX, pose.centerY, visualTransform.position.z},
        visualTransform.rotation);
    visualTransform.setScale(pose.width, pose.height,
                             visualTransform.scale.z);
    return true;
}

} // namespace ayt::entity
