#pragma once

#include <AY2D/SpriteClip2D.h>
#include <AYEntity/CameraFrame.h>
#include <AYEntity/components/SpriteComponent.h>
#include <AYEntity/components/TransformComponent.h>

namespace ayt::entity {

/// Quantize only a visual anchor to the active orthographic viewport's pixel
/// grid. Gameplay pose/collision and camera are untouched. This removes
/// subpixel anchor drift, but is not a pixel-perfect guarantee when source
/// texels project to a non-integer size, the sprite rotates, or filtering
/// blends samples. Failure preserves output.
inline bool snapSpriteAnchorToScreenPixel2D(
    const CameraFrame& camera, const CameraViewport& viewport,
    const math::FVector3& anchor, math::FVector3& output) noexcept {
    if (camera.projectionMode != CameraProjection::Orthographic)
        return false;
    math::FVector3 screen;
    if (!worldToScreen(camera, viewport, anchor, screen)) return false;
    const math::FVector3 snappedScreen{
        std::round(screen.x - viewport.x) + viewport.x,
        std::round(screen.y - viewport.y) + viewport.y, screen.z};
    math::FVector3 candidate;
    if (!screenToWorld(camera, viewport, snappedScreen, candidate))
        return false;
    output = candidate;
    return true;
}

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

/// Place an attachment Transform at a named socket of the current clip frame.
/// Parent the attachment beside the visual Sprite under the same unit-scale
/// actor anchor, so the Sprite's trimmed-quad scale does not affect it. Missing
/// sockets or invalid scale leave the Transform unchanged. The caller owns the
/// attachment entity and its visibility when a socket is absent.
inline bool presentSpriteClipSocket2D(const ay2d::SpriteClipPlayer2D& player,
                                      std::string_view socketName,
                                      float unitsPerPixel, bool mirrorX,
                                      Transform& attachmentTransform) {
    const auto* frame = player.frame();
    if (!frame) return false;
    float x = 0.0f, y = 0.0f;
    if (!ay2d::spriteClipSocket2D(*frame, socketName, unitsPerPixel,
                                  mirrorX, x, y)) return false;
    attachmentTransform.setPresentationPose(
        {x, y, attachmentTransform.position.z}, attachmentTransform.rotation);
    return true;
}

/// Present an independently timed Sprite clip at a socket on the current
/// source frame. The socket Entity is a sibling of the source visual under
/// its unit-scale anchor; the attachment visual is a child of that socket.
/// The caller advances both players, owns both clip sets and texture leases,
/// and may rotate the socket Transform for a continuous local effect. Source
/// and attachment mirroring are independent. A missing socket or invalid pose
/// hides the attachment Sprite without changing either Transform; a successful
/// call makes it visible. This affects presentation only.
inline bool presentSpriteClipAttachment2D(
    const ay2d::SpriteClipPlayer2D& sourcePlayer,
    std::string_view socketName, float sourceUnitsPerPixel,
    bool sourceMirrorX, const ay2d::SpriteClipSet2D& attachmentSet,
    const ay2d::SpriteClipPlayer2D& attachmentPlayer,
    float attachmentUnitsPerPixel, bool attachmentMirrorX,
    Transform& socketTransform, Transform& attachmentVisualTransform,
    SpriteComponent& attachmentSprite) {
    const auto* sourceFrame = sourcePlayer.frame();
    const auto* attachmentFrame = attachmentPlayer.frame();
    float socketX = 0.0f, socketY = 0.0f;
    ay2d::SpriteClipPose2D attachmentPose;
    if (!sourceFrame || !attachmentFrame ||
        !ay2d::spriteClipSocket2D(*sourceFrame, socketName,
                                  sourceUnitsPerPixel, sourceMirrorX,
                                  socketX, socketY) ||
        !ay2d::spriteClipPose2D(attachmentSet, *attachmentFrame,
                                attachmentUnitsPerPixel, attachmentMirrorX,
                                attachmentPose)) {
        attachmentSprite.visible = false;
        return false;
    }
    if (!presentSpriteClip2D(attachmentSet, attachmentPlayer,
                             attachmentUnitsPerPixel,
                             attachmentVisualTransform, attachmentSprite,
                             attachmentMirrorX)) {
        attachmentSprite.visible = false;
        return false;
    }
    socketTransform.setPresentationPose(
        {socketX, socketY, socketTransform.position.z},
        socketTransform.rotation);
    attachmentSprite.visible = true;
    return true;
}

} // namespace ayt::entity
