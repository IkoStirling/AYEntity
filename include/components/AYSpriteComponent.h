#pragma once
// AYSpriteComponent.h — CM-3 (2026-08-11): 2D sprite placement metadata.
//
// Self-contained transform: position (world, z carried for the world
// matrix translation), rotationZ (radians about +z), scaleX/scaleY.
// The sprite carries its own transform instead of riding a
// Transform component so a 2D-only entity can be authored without
// the 3D Transform semantics (AY2D convention, design.md §7.x).
// sourceRectMin/Max are atlas UVs in 0..1; default (0,0)-(1,1) =
// whole texture. colorRGBA tints the sampled color (default white).

#include <AYCore.h>
#include <IAYEntity.h>

#include <cstdint>
#include <string>

namespace ayt::entity
{

#define AY_CURRENT_CLASS SpriteComponent
struct SpriteComponent : public IComponent {
    const char* getName() const override { return "SpriteComponent"; }

    AY_PROPERTY(std::string, texturePath, kAttrSerialize)
    AY_PROPERTY(math::FVector3, position, kAttrSerialize)
    AY_PROPERTY(float, rotationZ, kAttrSerialize)
    AY_PROPERTY(float, scaleX, kAttrSerialize)
    AY_PROPERTY(float, scaleY, kAttrSerialize)
    AY_PROPERTY(math::FVector2, sourceRectMin, kAttrSerialize)
    AY_PROPERTY(math::FVector2, sourceRectMax, kAttrSerialize)
    AY_PROPERTY(math::FVector4, colorRGBA, kAttrSerialize)
    // 1 = flip horizontally (U), 2 = flip vertically (V).
    AY_PROPERTY(int32_t, flip, kAttrSerialize)
    AY_PROPERTY(int32_t, layer, kAttrSerialize)
    AY_PROPERTY(int32_t, sortingKey, kAttrSerialize)

    // Runtime-only (not serialized): render skip flag.
    bool visible = true;

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
    }

    explicit SpriteComponent(const char* path)
        : texturePath(path ? path : "") {}

    void setTexture(const char* path) { texturePath = path ? path : ""; }

    bool isValid() const { return !texturePath.empty(); }
};
#undef AY_CURRENT_CLASS

} // namespace ayt::entity
