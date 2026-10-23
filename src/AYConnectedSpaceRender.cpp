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
    // A perspective matrix has w=z in camera space. Reject orthographic or
    // singular/unsupported custom matrices rather than emitting invalid clips.
    if (std::fabs(projection(3,2)) < 1.0e-6f || std::fabs(projection(3,3)) > 1.0e-6f
        || std::fabs(projection(0,0)) < 1.0e-6f
        || std::fabs(projection(1,1)) < 1.0e-6f
        || std::fabs(projection(2,3)) < 1.0e-6f)
        return false;
    const float qz = 1.0f/projection(3,2);
    const float qw = (1.0f-projection(2,2)*qz)/projection(2,3);
    const math::FVector4 corner{
        (sign(plane.x)-projection(0,2)*qz-projection(0,3)*qw)/projection(0,0),
        (sign(plane.y)-projection(1,2)*qz-projection(1,3)*qw)/projection(1,1),
        qz,qw};
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
