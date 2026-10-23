#include <AYEntity/ConnectedSpaceRender.h>
#include <AYEntity/World.h>
#include <AYMath/MathUtils.h>
#include <algorithm>
#include <cmath>

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
} // namespace ayt::entity
