#pragma once
#include <AYEntity/ConnectedSpaces.h>
#include <AYRenderer/CameraViews.h>

namespace ayt::entity {
class World;

/// A portal child view and the matching entrance surface transform. The caller
/// schedules child views before their parents, with distinct camera outputs.
struct SpacePortalView {
    render::CameraRenderView view{};
    math::Float4x4 entranceWorld = math::Float4x4::identity();
    SpaceRegionId sourceRegion = 0;
    SpacePortalId portalId = 0;
};

/// Map the live source view through one opening and clip the child view to
/// the exit plane. Position and view matrix must describe the same rigid
/// camera; region, scene and perspective projection must be set. Output
/// dimensions must match the source viewport. The
/// camera must stand on the entrance's visible side and face the opening.
/// This plans one hop; recursive hosts may invoke it again with the child view.
bool planSpacePortalView(const World& world, SpacePortalId portalId,
    const render::CameraRenderView& sourceView,
    uint32_t childViewId, render::CameraOutputHandle output,
    SpacePortalView& result) noexcept;

/// Build the visible entrance quad for a previously planned child view.
/// The material must sample that child's camera output. The caller owns the
/// mesh/material and inserts this draw in the parent region's RenderScene.
bool makeSpacePortalSurfaceDraw(const SpacePortalView& plan,
    render::MeshHandle quad, render::MaterialHandle material,
    render::DrawItem& result) noexcept;
} // namespace ayt::entity
