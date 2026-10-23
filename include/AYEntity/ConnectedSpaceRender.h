#pragma once
#include <AYEntity/ConnectedSpaces.h>
#include <AYRenderer/CameraViews.h>
#include <deque>
#include <map>
#include <span>
#include <vector>

namespace ayt::render { class Renderer; }

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

struct SpacePortalScheduleOptions {
    uint32_t maxDepth = 3;
    uint32_t maxViews = render::kMaximumCameraViews; // includes root
    float minProjectedPixels = 16.0f;
};

/// Builds a child-before-parent renderViews batch from the visible entrance
/// quads. Visibility is conservative frustum clipping, without occlusion.
/// Each path owns a cached output/material; each view has its own scene copy.
/// The Renderer must outlive this scheduler. Build before beginFrame and keep
/// the batch alive through the synchronous renderViews call. A renderer reset
/// invalidates the cache automatically on the next build.
class SpacePortalViewScheduler {
public:
    explicit SpacePortalViewScheduler(render::Renderer& renderer) noexcept;
    ~SpacePortalViewScheduler();
    SpacePortalViewScheduler(const SpacePortalViewScheduler&) = delete;
    SpacePortalViewScheduler& operator=(const SpacePortalViewScheduler&) = delete;

    bool build(const World& world, const render::CameraRenderView& root,
        const SpacePortalScheduleOptions& options = {});
    std::span<const render::CameraRenderView> views() const noexcept { return _views; }
    void clear();
private:
    struct Resource {
        render::CameraOutputHandle output{};
        render::MaterialHandle material{};
        uint16_t width = 0, height = 0;
        bool used = false;
    };
    render::Renderer& _renderer;
    uint64_t _context = 0;
    render::MeshHandle _quad{};
    std::map<std::vector<SpacePortalId>,Resource> _resources;
    std::deque<render::RenderScene> _scenes;
    std::vector<render::CameraRenderView> _views;
    uint32_t _plannedCount = 0;
    uint32_t _nextViewId = 1;
    uint32_t _rootViewId = 0;
    const render::RenderScene* _sourceScene = nullptr;
    void release(Resource& resource);
    Resource* resourceFor(const std::vector<SpacePortalId>& path,
        uint16_t width, uint16_t height);
    void buildNode(const World& world, const render::CameraRenderView& view,
        std::vector<SpacePortalId>& path, uint32_t depth,
        const SpacePortalScheduleOptions& options);
};
} // namespace ayt::entity
