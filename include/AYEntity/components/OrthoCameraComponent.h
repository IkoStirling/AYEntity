#pragma once
// AYEntity/components/AYEntity/components/AYEntity/components/AYEntity/components/OrthoCameraComponent.h — CM-3 (2026-08-11): 2D orthographic camera
// metadata + header-only view/projection math.
//
// Coordinate convention: LH, Y-up, +Z forward, CCW winding, atlas V-top
// (see AYMath/CoordinateConvention.h). Routed through math::lh::* and
// the AYMath composition API (translate/rotate/scale) — do NOT
// reintroduce bare math::* / math::rh::* or hand-rolled Float4x4 row
// writes here. (M3, lh-rh-split-entity audit 2026-08-24.)
//
// Runtime placement comes from the entity Transform. Legacy position and
// rotation fields remain hidden/serialized for pre-v3 Scene migration. The
// host supplies the current viewport aspect to projectionMatrix(aspect), while
// viewportAspect remains a headless/legacy fallback.
//
// Dependency-direction lock: AYEntity must not depend on AY2D, so
// this math is duplicated with a mirror-of comment; the unittest
// cross-asserts every element against the real AY2D camera.

#include <AYCore.h>
#include <AYEntity/IEntity.h>
#include <AYEntity/components/TransformComponent.h>

#include <AYMath/MathTypes.h>
#include <AYMath/MathUtils.h>

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace ayt::entity
{

#define AY_CURRENT_CLASS OrthoCameraComponent
struct OrthoCameraComponent : public IComponent {
    const char* getName() const override { return "OrthoCameraComponent"; }

    AY_PROPERTY(float, positionX, kAttrSerializeHidden)
    AY_PROPERTY(float, positionY, kAttrSerializeHidden)
    // Zoom factor: 1.0 = 1 world unit == 1 viewport pixel. >1 zooms in.
    AY_PROPERTY(float, zoom, kAttrSerialize)
    // Rotation in radians about the screen-space +z axis.
    AY_PROPERTY(float, rotationRadians, kAttrSerializeHidden)
    // Vertical extent in world units; horizontal = viewSize * aspect.
    AY_PROPERTY(float, viewSize, kAttrSerialize)
    // Viewport aspect (width/height). Host sets it from window size.
    AY_PROPERTY(float, viewportAspect, kAttrSerializeHidden)
    // Near/far clip planes along the camera forward; default -1/1.
    AY_PROPERTY(float, nearZ, kAttrSerialize)
    AY_PROPERTY(float, farZ, kAttrSerialize)
    // 32 layers max: layerMask & (1u << layer) gates camera visibility.
    AY_PROPERTY(uint32_t, layerMask, kAttrSerialize)
    // Optional design frame used by resize adaptation. Zero disables design
    // framing and keeps viewSize as a fixed vertical extent.
    AY_PROPERTY(float, designWidth, kAttrSerialize)
    AY_PROPERTY(float, designHeight, kAttrSerialize)
    // 0=Expand (fixed height), 1=Fit (show the complete design frame),
    // 2=Fill (cover the viewport and crop the design frame).
    AY_PROPERTY(int32_t, aspectPolicy, kAttrSerialize)
    AY_PROPERTY(bool, active, kAttrSerialize)
    AY_PROPERTY(int32_t, priority, kAttrSerialize)
    // Local pose allows several cameras to share one Entity Transform.
    AY_PROPERTY(float, offsetX, kAttrSerialize)
    AY_PROPERTY(float, offsetY, kAttrSerialize)
    AY_PROPERTY(float, localRotationRadians, kAttrSerialize)

    // Runtime-only compatibility gate. Serialized active/priority determine
    // normal selection; legacy hosts can still suppress a camera here.
    bool isPrimary = true;

    OrthoCameraComponent() {
        positionX       = 0.0f;
        positionY       = 0.0f;
        zoom            = 1.0f;
        rotationRadians = 0.0f;
        viewSize        = 1.0f;
        viewportAspect  = 16.0f / 9.0f;
        nearZ           = -1.0f;
        farZ            = 1.0f;
        layerMask       = 0xFFFFFFFFu;
        designWidth     = 0.0f;
        designHeight    = 0.0f;
        aspectPolicy    = 0;
        active          = true;
        priority        = 0;
        offsetX         = 0.0f;
        offsetY         = 0.0f;
        localRotationRadians = 0.0f;
    }

    [[nodiscard]] float viewportAspectOr() const noexcept {
        return std::isfinite(viewportAspect) && viewportAspect > 0.0f
            ? viewportAspect : 1.0f;
    }

    [[nodiscard]] float viewportAspectOr(float actualAspect) const noexcept {
        return std::isfinite(actualAspect) && actualAspect > 0.0f
            ? actualAspect : viewportAspectOr();
    }

    [[nodiscard]] float safeZoom() const noexcept {
        return std::isfinite(zoom) && std::fabs(zoom) > 1.0e-6f
            ? zoom : 1.0f;
    }

    [[nodiscard]] float effectiveViewSize(float actualAspect = 0.0f) const noexcept {
        const float base = std::isfinite(viewSize) && viewSize > 1.0e-6f
            ? viewSize : 1.0f;
        if (!(designWidth > 0.0f) || !(designHeight > 0.0f)) return base;
        const float viewport = viewportAspectOr(actualAspect);
        const float designAspect = designWidth / designHeight;
        if (!std::isfinite(designAspect) || designAspect <= 0.0f) return base;
        const float ratio = designAspect / viewport;
        if (aspectPolicy == 1) return base * std::max(1.0f, ratio);
        if (aspectPolicy == 2) return base * std::min(1.0f, ratio);
        return base;
    }

    [[nodiscard]] math::FVector2 visibleHalfExtents(
        float actualAspect = 0.0f) const noexcept {
        const float aspect = viewportAspectOr(actualAspect);
        const float halfH = effectiveViewSize(aspect) * 0.5f
                          / std::fabs(safeZoom());
        return {halfH * aspect, halfH};
    }

    [[nodiscard]] bool isEnabled() const noexcept {
        return active && isPrimary;
    }

    [[nodiscard]] math::FVector2 worldCenter(
        const math::FVector3& entityPosition, float entityRotation) const noexcept {
        return {
            entityPosition.x + std::cos(entityRotation) * offsetX
                - std::sin(entityRotation) * offsetY,
            entityPosition.y + std::sin(entityRotation) * offsetX
                + std::cos(entityRotation) * offsetY
        };
    }

    [[nodiscard]] float worldRotation(float entityRotation) const noexcept {
        return entityRotation + localRotationRadians;
    }

    // Mirror of ayt::ay2d::OrthographicCamera::viewMatrix()
    // (AY2D/OrthographicCamera.h:116-152). Translate by -position,
    // rotate by -rotationRadians about +z, scale by zoom. Y axis
    // is bottom-up — no Y flip here (the projection matches).
    //
    // Routed through the AYMath composition API
    // (translate/rotate/scale) rather than hand-rolled Float4x4 row
    // writes — keeps future 2.5D additions (e.g. rotateY) routed
    // through the lh:: helpers instead of reintroducing a hand-built
    // handedness bug. (M1, lh-rh-split-entity audit 2026-08-24.)
    [[nodiscard]] math::Float4x4 viewMatrix(
        float cameraX, float cameraY, float cameraRotation) const noexcept {
        // Zoom scales view-space XY: values above one magnify content and
        // reduce the visible world extent. Z remains unscaled.
        const float s = safeZoom();
        // Inverse camera transform for column vectors. Translation is
        // composed on the right so the camera position is transformed by
        // the inverse rotation/scale and always maps to the view origin.
        return math::rotate(math::FVector3(0.0f, 0.0f, 1.0f),
                            -cameraRotation)
             * math::scale(s, s, 1.0f)
             * math::translate(-cameraX, -cameraY, 0.0f);
    }

    [[nodiscard]] math::Float4x4 viewMatrix(
        const Transform& transform) const noexcept {
        const float angle = transform.rotation.toEulerAngles().z;
        const auto center = worldCenter(transform.position, angle);
        return viewMatrix(center.x, center.y, worldRotation(angle));
    }

    // Legacy/headless compatibility overload. Runtime systems use Transform.
    [[nodiscard]] math::Float4x4 viewMatrix() const noexcept {
        return viewMatrix(positionX, positionY, rotationRadians);
    }

    // Mirror of ayt::ay2d::OrthographicCamera::projectionMatrix()
    // (AY2D/OrthographicCamera.h:155-163). Visible region centered on
    // the camera with vertical extent viewSize (top/bottom = ±half);
    // horizontal extent = vertical * aspect.
    [[nodiscard]] math::Float4x4 projectionMatrix(
        float actualAspect) const noexcept {
        const float aspect = viewportAspectOr(actualAspect);
        const float half   = effectiveViewSize(aspect) * 0.5f;
        const float left   = -half * aspect;
        const float right  =  half * aspect;
        const float bottom = -half;
        const float top    =  half;
        return math::lh::ortho(left, right, bottom, top, nearZ, farZ);
    }

    [[nodiscard]] math::Float4x4 projectionMatrix() const noexcept {
        return projectionMatrix(viewportAspect);
    }
};
#undef AY_CURRENT_CLASS

} // namespace ayt::entity
