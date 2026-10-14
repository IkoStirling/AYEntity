#pragma once
#include <AYEntity/CameraFrame.h>
#include <AYEntity/TilemapVisibilityRuntime.h>
#include <AYRenderer/RenderScene.h>

namespace ayt::entity::detail {
// Cull against the actual presented camera, including transitions and shake.
inline TilemapCameraVisibility cameraVisibility2D(const render::RenderScene& scene,
    const render::OverlayCamera2D* hostOverride = nullptr) {
    TilemapCameraVisibility result{};
    if (!scene.hasOverlayCamera2D() && !hostOverride) return result;
    const auto& camera = hostOverride ? *hostOverride : scene.overlayCamera2D();
    CameraFrame frame; frame.view = camera.view; frame.projection = camera.projection;
    result.minX = result.minY = INFINITY;
    result.maxX = result.maxY = -INFINITY;
    for (float y : {0.f,1.f}) for (float x : {0.f,1.f}) {
        math::FVector3 point;
        if (!screenToWorld(frame,{}, {x,y,0},point)) return {};
        result.minX = std::min(result.minX,point.x); result.minY = std::min(result.minY,point.y);
        result.maxX = std::max(result.maxX,point.x); result.maxY = std::max(result.maxY,point.y);
    }
    result.layerMask = camera.layerMask; result.valid = true; return result;
}
}
