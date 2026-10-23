#include <AYEntity/ConnectedSpaceRender.h>
#include <AYEntity/World.h>
#include <AYRenderer.h>
#include <AYMath/MathUtils.h>
#include <algorithm>
#include <cmath>
#include <limits>

namespace ayt::entity {
namespace {
bool finiteMatrix(const math::Float4x4& value) noexcept {
    for (int i=0; i<16; ++i) if (!std::isfinite(value.ptr()[i])) return false;
    return true;
}
float sign(float value) noexcept { return value < 0 ? -1.0f : 1.0f; }
bool clipCorner(const math::Float4x4& projection, const math::FVector4& clip,
                math::FVector4& corner) noexcept {
    double rows[4][5]{};
    for (int row=0; row<4; ++row) {
        for (int col=0; col<4; ++col) rows[row][col]=projection(row,col);
        rows[row][4]=clip.f[row];
    }
    for (int col=0; col<4; ++col) {
        int pivot=col;
        for (int row=col+1; row<4; ++row)
            if (std::fabs(rows[row][col])>std::fabs(rows[pivot][col])) pivot=row;
        if (std::fabs(rows[pivot][col])<1.0e-12) return false;
        if (pivot!=col) for (int c=col;c<5;++c) std::swap(rows[pivot][c],rows[col][c]);
        const double diagonal=rows[col][col];
        for (int c=col;c<5;++c) rows[col][c]/=diagonal;
        for (int row=0; row<4; ++row) if (row!=col) {
            const double factor=rows[row][col];
            for (int c=col;c<5;++c) rows[row][c]-=factor*rows[col][c];
        }
    }
    for (int row=0; row<4; ++row) {
        if (!std::isfinite(rows[row][4])) return false;
        corner.f[row]=static_cast<float>(rows[row][4]);
    }
    return true;
}

float clipDistance(const math::FVector4& v, int plane) noexcept {
    switch (plane) {
    case 0: return v.w+v.x;
    case 1: return v.w-v.x;
    case 2: return v.w+v.y;
    case 3: return v.w-v.y;
    case 4: return v.z;
    default: return v.w-v.z;
    }
}

float projectedPortalPixels(const SpacePortal& portal,
    const render::CameraRenderView& view) {
    if (view.spaceRegionId!=portal.source || !portal.enabled) return 0;
    const auto normal=portal.entrance.rotation*math::FVector3{0,0,1};
    if ((view.position-portal.entrance.position).dot(normal)<=1.0e-4f) return 0;
    const auto forward=math::FVector3{view.view(2,0),view.view(2,1),view.view(2,2)};
    if (forward.dot(portal.entrance.position-view.position)<=0) return 0;
    const auto right=portal.entrance.rotation*math::FVector3{portal.halfWidth,0,0};
    const auto up=portal.entrance.rotation*math::FVector3{0,portal.halfHeight,0};
    const auto center=portal.entrance.position;
    const auto vp=view.projection*view.view;
    std::vector<math::FVector4> polygon;
    polygon.reserve(12);
    for (const auto& point : {center-right-up,center+right-up,
                              center+right+up,center-right+up})
        polygon.push_back(vp*math::FVector4{point.x,point.y,point.z,1});
    for (int plane=0;plane<6 && !polygon.empty();++plane) {
        std::vector<math::FVector4> clipped;
        clipped.reserve(polygon.size()+2);
        auto previous=polygon.back();
        float before=clipDistance(previous,plane);
        for (const auto& current:polygon) {
            const float after=clipDistance(current,plane);
            if ((before>=0)!=(after>=0)) {
                const float t=before/(before-after);
                clipped.push_back(previous+(current-previous)*t);
            }
            if (after>=0) clipped.push_back(current);
            previous=current; before=after;
        }
        polygon=std::move(clipped);
    }
    if (polygon.size()<3) return 0;
    double twiceArea=0;
    for (size_t i=0;i<polygon.size();++i) {
        const auto& a=polygon[i], &b=polygon[(i+1)%polygon.size()];
        if (a.w<=0 || b.w<=0) return 0;
        twiceArea+=static_cast<double>(a.x/a.w)*(b.y/b.w)
                  -static_cast<double>(b.x/b.w)*(a.y/a.w);
    }
    const double pixels=std::fabs(twiceArea)*view.width*view.height/8.0;
    return std::isfinite(pixels) ? static_cast<float>(pixels) : 0;
}

bool withinEntranceDistance(const SpacePortal& portal,
    const render::CameraRenderView& view, float maxDistance) noexcept {
    if (std::isinf(maxDistance)) return true;
    const auto local = portal.entrance.rotation.inverse()
        * (view.position - portal.entrance.position);
    const double dx = std::max(0.0, static_cast<double>(std::fabs(local.x))
        - portal.halfWidth);
    const double dy = std::max(0.0, static_cast<double>(std::fabs(local.y))
        - portal.halfHeight);
    const double dz = static_cast<double>(local.z);
    const double limit = static_cast<double>(maxDistance);
    return dx * dx + dy * dy + dz * dz <= limit * limit;
}
}

bool planSpacePortalView(const World& world, SpacePortalId portalId,
    const render::CameraRenderView& sourceView,
    uint32_t childViewId, render::CameraOutputHandle output,
    SpacePortalView& result) noexcept {
    const auto* portal = world.connectedSpaces().portal(portalId);
    if (!portal || !portal->enabled || !sourceView.scene || !output.id
        || sourceView.spaceRegionId != portal->source
        || !sourceView.width || !sourceView.height
        || !std::isfinite(sourceView.position.x)
        || !std::isfinite(sourceView.position.y)
        || !std::isfinite(sourceView.position.z)
        || !finiteMatrix(sourceView.projection) || !finiteMatrix(sourceView.view)) return false;
    const auto sourceAtOrigin = sourceView.view.transformPoint(sourceView.position);
    if (sourceAtOrigin.lengthSq() > 1.0e-4f) return false;
    const math::FVector3 sourceForward{sourceView.view(2,0),sourceView.view(2,1),sourceView.view(2,2)};
    const math::FVector3 sourceUp{sourceView.view(1,0),sourceView.view(1,1),sourceView.view(1,2)};
    if (std::fabs(sourceForward.lengthSq()-1.0f) > 1.0e-3f
        || std::fabs(sourceUp.lengthSq()-1.0f) > 1.0e-3f
        || std::fabs(sourceForward.dot(sourceUp)) > 1.0e-3f) return false;
    const auto front = portal->entrance.rotation * math::FVector3{0,0,1};
    if ((sourceView.position-portal->entrance.position).dot(front) <= 1.0e-4f)
        return false;
    if (sourceForward.dot(portal->entrance.position-sourceView.position) <= 0)
        return false;
    SpacePose mapped;
    if (!world.connectedSpaces().mapPose(portalId,{sourceView.position,{0,0,0,1}},mapped))
        return false;
    math::FVector3 mappedForward, mappedUp;
    if (!world.connectedSpaces().mapVector(portalId,sourceForward,mappedForward)
        || !world.connectedSpaces().mapVector(portalId,sourceUp,mappedUp)) return false;
    const auto view = math::lh::lookAt(mapped.position,mapped.position+mappedForward,mappedUp);
    const auto exitNormal = portal->exit.rotation * math::FVector3{0,0,1};
    if ((mapped.position-portal->exit.position).dot(exitNormal) >= -1.0e-4f)
        return false;
    // Clip to a plane just inside the destination so content behind the exit
    // cannot leak into the opening. Left-handed depth uses the 0..1 near plane.
    const auto planePoint = portal->exit.position + exitNormal * 1.0e-3f;
    const auto normalCamera = view.transformDirection(exitNormal);
    const auto pointCamera = view.transformPoint(planePoint);
    const math::FVector4 plane{normalCamera.x,normalCamera.y,normalCamera.z,
                               -normalCamera.dot(pointCamera)};
    auto projection = sourceView.projection;
    // A perspective matrix has homogeneous w from camera z. Solve the far
    // clip corner directly: recursively planned views already have an oblique
    // row 2, and AYMath's generic inverse need not be used for projections.
    if (std::fabs(projection(3,0)) > 1.0e-6f
        || std::fabs(projection(3,1)) > 1.0e-6f
        || std::fabs(projection(3,2)) < 1.0e-6f
        || std::fabs(projection(3,3)) > 1.0e-6f)
        return false;
    math::FVector4 corner{};
    if (!clipCorner(projection,{sign(plane.x),sign(plane.y),1,1},corner)) return false;
    const float denominator = plane.x*corner.x + plane.y*corner.y
        + plane.z*corner.z + plane.w*corner.w;
    if (!std::isfinite(denominator) || denominator <= 1.0e-6f) return false;
    projection.row[2] = plane * (1.0f/denominator);
    if (!finiteMatrix(projection)) return false;

    SpacePortalView candidate;
    candidate.view = sourceView;
    candidate.view.id = childViewId;
    candidate.view.view = view;
    candidate.view.projection = projection;
    candidate.view.position = mapped.position;
    candidate.view.spaceRegionId = portal->destination;
    candidate.view.output = output;
    candidate.view.x = candidate.view.y = 0;
    candidate.sourceRegion = portal->source;
    candidate.portalId = portalId;
    candidate.entranceWorld = math::Float4x4::fromTRS(portal->entrance.position,
        portal->entrance.rotation,{2*portal->halfWidth,2*portal->halfHeight,1});
    result = candidate;
    return true;
}

bool makeSpacePortalSurfaceDraw(const SpacePortalView& plan,
    render::MeshHandle quad, render::MaterialHandle material,
    render::DrawItem& result) noexcept {
    if (!plan.portalId || !plan.sourceRegion || !plan.view.output.id
        || !quad.isValid() || !material.isValid()) return false;
    render::DrawItem item;
    item.mesh = quad; item.material = material;
    item.world = plan.entranceWorld;
    item.spaceRegionId = plan.sourceRegion;
    item.allowFrustumCulling = false;
    item.shadowFlags = render::ShadowFlags::None;
    result = item;
    return true;
}

SpacePortalViewScheduler::SpacePortalViewScheduler(render::Renderer& renderer) noexcept
    : _renderer(renderer),_context(renderer.resourceContextId()) {}

SpacePortalViewScheduler::~SpacePortalViewScheduler() { clear(); }

void SpacePortalViewScheduler::release(Resource& resource) {
    if (_renderer.resourceContextId()!=_context) return;
    if (resource.material.isValid()) _renderer.destroyMaterial(resource.material);
    if (resource.output.id) _renderer.destroyCameraOutput(resource.output);
    resource={};
}

void SpacePortalViewScheduler::clear() {
    _views.clear(); _scenes.clear();
    for (auto& [path,resource]:_resources) release(resource);
    _resources.clear();
    if (_quad.isValid() && _renderer.resourceContextId()==_context)
        _renderer.destroyMesh(_quad);
    _quad={};
}

SpacePortalViewScheduler::Resource* SpacePortalViewScheduler::resourceFor(
    const std::vector<SpacePortalId>& path,uint16_t width,uint16_t height) {
    auto& resource=_resources[path];
    if (resource.output.id && (resource.width!=width || resource.height!=height))
        release(resource);
    if (!resource.output.id) {
        resource.output=_renderer.createCameraOutput(width,height);
        if (!resource.output.id) return nullptr;
        resource.material=_renderer.createPortalSurfaceMaterial(resource.output);
        if (!resource.material.isValid()) { release(resource); return nullptr; }
        resource.width=width; resource.height=height;
    }
    return &resource;
}

void SpacePortalViewScheduler::buildNode(const World& world,
    const render::CameraRenderView& view,std::vector<SpacePortalId>& path,
    uint32_t depth,const SpacePortalScheduleOptions& options) {
    _scenes.emplace_back();
    auto* scene=&_scenes.back();
    render::copyCameraVisibleScene(*_sourceScene,*scene,view.layerMask,true,
                                   view.spaceRegionId);
    struct Candidate { SpacePortalId id; float pixels; };
    std::vector<Candidate> candidates;
    if (depth<options.maxDepth && _plannedCount<options.maxViews) {
        for (const auto& portal:world.connectedSpaces().portals()) {
            if (!withinEntranceDistance(portal,view,options.maxEntranceDistance))
                continue;
            const float pixels=projectedPortalPixels(portal,view);
            if (pixels>=options.minProjectedPixels && pixels>0)
                candidates.push_back({portal.id,pixels});
        }
        std::sort(candidates.begin(),candidates.end(),[](const auto& a,const auto& b) {
            return a.pixels==b.pixels ? a.id<b.id : a.pixels>b.pixels;
        });
    }
    for (const auto& candidate:candidates) {
        if (_plannedCount>=options.maxViews) break;
        path.push_back(candidate.id);
        auto* resource=resourceFor(path,view.width,view.height);
        if (!resource) { path.pop_back(); continue; }
        while (_nextViewId==_rootViewId) ++_nextViewId;
        const uint32_t id=_nextViewId++;
        SpacePortalView plan;
        if (!planSpacePortalView(world,candidate.id,view,id,resource->output,plan)) {
            path.pop_back(); continue;
        }
        if (!_quad.isValid()) _quad=_renderer.createUnitQuad();
        render::DrawItem surface;
        if (!_quad.isValid() || !makeSpacePortalSurfaceDraw(plan,_quad,
                resource->material,surface)) { path.pop_back(); continue; }
        resource->used=true;
        ++_plannedCount;
        buildNode(world,plan.view,path,depth+1,options);
        surface.cameraLayerMask=view.layerMask;
        scene->add(surface);
        path.pop_back();
    }
    auto finalView=view;
    finalView.scene=scene;
    _views.push_back(finalView);
}

bool SpacePortalViewScheduler::build(const World& world,
    const render::CameraRenderView& root,const SpacePortalScheduleOptions& options) {
    _views.clear(); _scenes.clear();
    if (!_renderer.resourceContextId() || !root.scene || !root.spaceRegionId
        || !world.connectedSpaces().containsRegion(root.spaceRegionId)
        || !root.width || !root.height || !finiteMatrix(root.view)
        || !finiteMatrix(root.projection)
        || !std::isfinite(root.position.x) || !std::isfinite(root.position.y)
        || !std::isfinite(root.position.z) || !options.maxViews
        || options.maxViews>render::kMaximumCameraViews
        || !std::isfinite(options.minProjectedPixels)
        || options.minProjectedPixels<0
        || std::isnan(options.maxEntranceDistance)
        || options.maxEntranceDistance<0) return false;
    if (_context!=_renderer.resourceContextId()) {
        _resources.clear(); _quad={}; _context=_renderer.resourceContextId();
    }
    for (auto& [path,resource]:_resources) resource.used=false;
    _sourceScene=root.scene;
    _plannedCount=1; _nextViewId=1; _rootViewId=root.id;
    std::vector<SpacePortalId> path;
    buildNode(world,root,path,0,options);
    for (auto it=_resources.begin();it!=_resources.end();) {
        if (!it->second.used) { release(it->second); it=_resources.erase(it); }
        else ++it;
    }
    return !_views.empty();
}
} // namespace ayt::entity
