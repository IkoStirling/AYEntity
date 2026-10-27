#pragma once

#include <AY2D/ParametricSpriteRig2D.h>
#include <AYEntity/components/SpriteComponent.h>
#include <AYEntity/components/TransformComponent.h>

#include <cmath>
#include <cstddef>
#include <cstdint>

namespace ayt::entity {

/// Apply one evaluated rig layer to its matching Sprite Entity before scene
/// extraction. The host owns layer Entity creation and parent hierarchy.
/// The function preserves the Entity's Z coordinate and RGB tint.
inline bool presentParametricSpriteLayer2D(
    const ay2d::ParametricSpriteRig2D& rig, std::size_t layerIndex,
    const ay2d::PuppetLayerPose2D& pose,
    Transform& transform, SpriteComponent& sprite,
    std::uint32_t sortBase = 0) {
    if (layerIndex >= rig.layerCount() || sortBase > 0x00FFFF00u ||
        !std::isfinite(pose.x) || !std::isfinite(pose.y) ||
        !std::isfinite(pose.rotation) ||
        !std::isfinite(pose.scaleX) || !std::isfinite(pose.scaleY) ||
        !std::isfinite(pose.opacity) || pose.scaleX <= 0 ||
        pose.scaleY <= 0 || pose.opacity < 0 || pose.opacity > 1 ||
        pose.variantIndex < -1 ||
        (pose.variantIndex >= 0 && static_cast<std::size_t>(pose.variantIndex) >=
            rig.layer(layerIndex).variants.size()))
        return false;
    const auto& layer = rig.layer(layerIndex);
    const auto* variant = pose.variantIndex < 0 ? nullptr
        : &layer.variants[static_cast<std::size_t>(pose.variantIndex)];
    const auto rotation = math::FQuaternion::fromAxisAngle(
        math::FVector3{0, 0, 1}, pose.rotation);
    transform.setPresentationPose(
        math::FVector3{pose.x, pose.y, transform.position.z}, rotation);
    transform.setScale(pose.scaleX, pose.scaleY, transform.scale.z);
    const auto& texture = variant ? variant->texturePath : layer.texturePath;
    if (sprite.texturePath != texture) {
        sprite.texturePath = texture;
        sprite.residentAlbedoTextureId = 0;
    }
    if (sprite.opacityMaskTexturePath != layer.maskTexturePath) {
        sprite.opacityMaskTexturePath = layer.maskTexturePath;
        sprite.residentMaskTextureId = 0;
    }
    sprite.sourceRectMin = math::FVector2(
        variant ? variant->u0 : layer.u0, variant ? variant->v0 : layer.v0);
    sprite.sourceRectMax = math::FVector2(
        variant ? variant->u1 : layer.u1, variant ? variant->v1 : layer.v1);
    sprite.colorRGBA.w = pose.opacity;
    sprite.layer = layer.renderLayer;
    sprite.sortingKey = static_cast<std::int32_t>(sortBase + layerIndex);
    sprite.visible = pose.opacity > 0;
    return true;
}

} // namespace ayt::entity
