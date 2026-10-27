#pragma once
// AYEntity/components/AYEntity/components/AYEntity/components/AYEntity/components/SpriteComponent.h — CM-3 (2026-08-11): 2D sprite placement metadata.
//
// Transform is authoritative for all runtime/editor placement. The legacy
// position/rotationZ/scaleX/scaleY fields remain serialized and hidden for a
// compatibility window so pre-v3 Scenes can migrate without losing data.
// sourceRectMin/Max are atlas UVs in 0..1; default (0,0)-(1,1) =
// whole texture. colorRGBA tints the sampled color (default white).

#include <AYCore.h>
#include <AYEntity/IEntity.h>

#include <cstdint>
#include <string>

namespace ayt::entity
{

#define AY_CURRENT_CLASS SpriteComponent
struct SpriteComponent : public IComponent {
    const char* getName() const override { return "SpriteComponent"; }

    AY_PROPERTY(std::string, texturePath, kAttrSerialize)
    /// Optional SceneOverlay alpha coverage map. It samples the same UVs as
    /// texturePath, including source rect and flip. WorldLit2D is unsupported.
    AY_PROPERTY(std::string, opacityMaskTexturePath, kAttrSerialize)
    // Optional WorldLit2D maps. Color textures are loaded as sRGB; normal and
    // roughness data stay linear. Empty paths use renderer fallbacks.
    AY_PROPERTY(std::string, normalTexturePath, kAttrSerialize)
    AY_PROPERTY(std::string, roughnessTexturePath, kAttrSerialize)
    AY_PROPERTY(std::string, emissiveTexturePath, kAttrSerialize)
    AY_PROPERTY(math::FVector3, position, kAttrSerializeHidden)
    AY_PROPERTY(float, rotationZ, kAttrSerializeHidden)
    AY_PROPERTY(float, scaleX, kAttrSerializeHidden)
    AY_PROPERTY(float, scaleY, kAttrSerializeHidden)
    AY_PROPERTY(math::FVector2, sourceRectMin, kAttrSerialize)
    AY_PROPERTY(math::FVector2, sourceRectMax, kAttrSerialize)
    AY_PROPERTY(math::FVector4, colorRGBA, kAttrSerialize)
    // 1 = flip horizontally (U), 2 = flip vertically (V).
    AY_PROPERTY(int32_t, flip, kAttrSerialize)
    AY_PROPERTY(int32_t, layer, kAttrSerialize)
    AY_PROPERTY(int32_t, sortingKey, kAttrSerialize)
    // 0 = legacy SceneOverlay (default), 1 = deferred WorldLit2D.
    AY_PROPERTY(int32_t, renderDomain, kAttrSerialize)
    // SceneOverlay sampling: 0 = ordinary linear, 1 = Retro AA for stable
    // pixel-art motion at non-integer viewport scales, 2 = hard nearest texel.
    // WorldLit2D and masked SceneOverlay sprites ignore this option.
    AY_PROPERTY(int32_t, samplingQuality, kAttrSerialize)
    AY_PROPERTY(float, metallic, kAttrSerialize)
    AY_PROPERTY(float, roughness, kAttrSerialize)
    AY_PROPERTY(float, ambientOcclusion, kAttrSerialize)
    AY_PROPERTY(float, emissiveStrength, kAttrSerialize)
    AY_PROPERTY(float, alphaCutoff, kAttrSerialize)
    AY_PROPERTY(bool, invertNormalY, kAttrSerialize)
    // WorldLit-only caster control. SceneOverlay is never part of the world
    // shadow domain regardless of this value.
    AY_PROPERTY(bool, castShadow, kAttrSerialize)

    // Runtime-only (not serialized): render skip flag.
    bool visible = true;
    /// Optional Renderer-owned resident texture IDs for SceneOverlay. Set
    /// these from a live GPU texture lease, and retain that lease until the
    /// Sprite render scene has rebound and prior frames have retired. Zero
    /// keeps path-based loading. A mask ID is required when the authored
    /// opacityMaskTexturePath is nonempty and an albedo ID is supplied.
    std::uint64_t residentAlbedoTextureId = 0;
    /// Optional resident mask paired with residentAlbedoTextureId.
    std::uint64_t residentMaskTextureId = 0;

    SpriteComponent() {
        rotationZ   = 0.0f;
        scaleX      = 1.0f;
        scaleY      = 1.0f;
        sourceRectMin = math::FVector2(0.0f, 0.0f);
        sourceRectMax = math::FVector2(1.0f, 1.0f);
        colorRGBA   = math::FVector4(1.0f, 1.0f, 1.0f, 1.0f);
        flip        = 0;
        layer       = 0;
        sortingKey  = 0;
        renderDomain = 0;
        samplingQuality = 0;
        metallic = 0.0f;
        roughness = 0.75f;
        ambientOcclusion = 1.0f;
        emissiveStrength = 0.0f;
        alphaCutoff = 0.5f;
        invertNormalY = false;
        castShadow = true;
    }

    explicit SpriteComponent(const char* path)
        : SpriteComponent() {
        texturePath = path ? path : "";
    }

    void setTexture(const char* path) { texturePath = path ? path : ""; }

    bool isValid() const { return !texturePath.empty() || residentAlbedoTextureId != 0; }
    bool isWorldLit() const noexcept { return renderDomain == 1; }
};
#undef AY_CURRENT_CLASS

} // namespace ayt::entity
