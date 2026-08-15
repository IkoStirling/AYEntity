#pragma once
// AYEntity/components/AYEntity/components/AYEntity/components/AYEntity/components/OrthoCameraComponent.h — CM-3 (2026-08-11): 2D orthographic camera
// metadata + header-only view/projection math.
//
// viewMatrix()/projectionMatrix() mirror ayt::ay2d::OrthographicCamera
// (AY2D/OrthographicCamera.h:116-163) exactly — same origin-centered
// convention, zoom as a view-side scale, viewSize = VERTICAL world
// extent (horizontal = viewSize * viewportAspect), nearZ/farZ default
// -1/1 (depth 0..1 for a 2D scene). The component has no viewport
// struct; the aspect is a plain float field (viewportAspect, default
// 16:9) that the host sets from its window size.
//
// Dependency-direction lock: AYEntity must not depend on AY2D, so
// this math is duplicated with a mirror-of comment; the unittest
// cross-asserts every element against the real AY2D camera.

#include <AYCore.h>
#include <AYEntity/IEntity.h>

#include <AYMath/MathTypes.h>
#include <AYMath/MathUtils.h>

#include <cmath>
#include <cstdint>

namespace ayt::entity
{

#define AY_CURRENT_CLASS OrthoCameraComponent
struct OrthoCameraComponent : public IComponent {
    const char* getName() const override { return "OrthoCameraComponent"; }

    AY_PROPERTY(float, positionX, kAttrSerialize)
    AY_PROPERTY(float, positionY, kAttrSerialize)
    // Zoom factor: 1.0 = 1 world unit == 1 viewport pixel. >1 zooms in.
    AY_PROPERTY(float, zoom, kAttrSerialize)
    // Rotation in radians about the screen-space +z axis.
    AY_PROPERTY(float, rotationRadians, kAttrSerialize)
    // Vertical extent in world units; horizontal = viewSize * aspect.
    AY_PROPERTY(float, viewSize, kAttrSerialize)
    // Viewport aspect (width/height). Host sets it from window size.
    AY_PROPERTY(float, viewportAspect, kAttrSerialize)
    // Near/far clip planes along the camera forward; default -1/1.
    AY_PROPERTY(float, nearZ, kAttrSerialize)
    AY_PROPERTY(float, farZ, kAttrSerialize)
    // 32 layers max: layerMask & (1u << layer) gates camera visibility.
    AY_PROPERTY(uint32_t, layerMask, kAttrSerialize)

    // Runtime-only: the first primary camera found wins the
    // RendererSubSystem main camera during the scene build.
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
    }

    [[nodiscard]] float viewportAspectOr() const noexcept {
        return viewportAspect > 0.0f ? viewportAspect : 1.0f;
    }

    // Mirror of ayt::ay2d::OrthographicCamera::viewMatrix()
    // (AY2D/OrthographicCamera.h:116-149). Translate by -position,
    // rotate by -rotationRadians about +z, scale by 1/zoom. Y axis
    // is bottom-up — no Y flip here (the projection matches).
    [[nodiscard]] math::Float4x4 viewMatrix() const noexcept {
        const float s = 1.0f / zoom;

        // Scale (1/zoom, 1/zoom, 1, 1).
        math::Float4x4 m = math::Float4x4::identity();
        m.row[0].x = s;
        m.row[1].y = s;
        m.row[2].z = 1.0f;

        // Rotation about z by -rotationRadians (world rotates under
        // the camera, so the camera's own rotation is negated).
        const float c  = std::cos(rotationRadians);
        const float sn = std::sin(rotationRadians);
        math::Float4x4 r = math::Float4x4::identity();
        r.row[0].x =  c;
        r.row[0].y =  sn;
        r.row[1].x = -sn;
        r.row[1].y =  c;
        m = r * m;

        // Translate by -position.
        m.row[0].w = -positionX;
        m.row[1].w = -positionY;
        return m;
    }

    // Mirror of ayt::ay2d::OrthographicCamera::projectionMatrix()
    // (AY2D/OrthographicCamera.h:155-163). Visible region centered on
    // the camera with vertical extent viewSize (top/bottom = ±half);
    // horizontal extent = vertical * aspect.
    [[nodiscard]] math::Float4x4 projectionMatrix() const noexcept {
        const float aspect = viewportAspectOr();
        const float half   = viewSize * 0.5f;
        const float left   = -half * aspect;
        const float right  =  half * aspect;
        const float bottom = -half;
        const float top    =  half;
        return math::ortho(left, right, bottom, top, nearZ, farZ);
    }
};
#undef AY_CURRENT_CLASS

} // namespace ayt::entity
