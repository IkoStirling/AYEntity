#include <AYEntity/ConnectedSpaces.h>

#include <algorithm>
#include <cmath>
#include <limits>

namespace ayt::entity {
namespace {
constexpr float kPlaneEpsilon = 1.0e-5f;
constexpr float kExitOffset = 1.0e-4f;
const math::FQuaternion kThroughDoor{0, 1, 0, 0};

bool finite(const math::FVector3& v) noexcept {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}
bool valid(const SpacePose& pose) noexcept {
    if (!finite(pose.position) || !std::isfinite(pose.rotation.x)
        || !std::isfinite(pose.rotation.y) || !std::isfinite(pose.rotation.z)
        || !std::isfinite(pose.rotation.w)) return false;
    const float length = pose.rotation.length();
    return std::isfinite(length) && std::fabs(length - 1.0f) < 1.0e-3f;
}
math::FVector3 mappedVector(const SpacePortal& portal, const math::FVector3& vector) {
    return portal.exit.rotation * (kThroughDoor * (portal.entrance.rotation.inverse() * vector));
}
SpacePose mappedPose(const SpacePortal& portal, const SpacePose& pose) {
    const auto local = portal.entrance.rotation.inverse() * (pose.position - portal.entrance.position);
    return {portal.exit.position + portal.exit.rotation * (kThroughDoor * local),
            (portal.exit.rotation * kThroughDoor * portal.entrance.rotation.inverse()
             * pose.rotation).normalize()};
}
struct Crossing {
    const SpacePortal* portal = nullptr;
    float fraction = 1;
};
Crossing nearestCrossing(const std::vector<SpacePortal>& portals, SpaceRegionId region,
                         const math::FVector3& start, const math::FVector3& displacement) {
    Crossing nearest;
    for (const auto& portal : portals) {
        if (!portal.enabled || portal.source != region) continue;
        const auto inverse = portal.entrance.rotation.inverse();
        const auto localStart = inverse * (start - portal.entrance.position);
        const auto localDelta = inverse * displacement;
        const float endZ = localStart.z + localDelta.z;
        // A fixed step may finish exactly on the plane. Admit the next step
        // from a narrow plane band when it moves through the opening; otherwise
        // the actor can pass through geometrically and remain in the old region.
        if (localStart.z < -kPlaneEpsilon || endZ >= -kPlaneEpsilon
            || localDelta.z >= 0) continue;
        const float t = std::max(0.0f, -localStart.z / localDelta.z);
        if (!(t >= 0 && t <= nearest.fraction)) continue;
        const auto intersection = localStart + localDelta * t;
        if (std::fabs(intersection.x) > portal.halfWidth
            || std::fabs(intersection.y) > portal.halfHeight) continue;
        if (nearest.portal && t == nearest.fraction && portal.id > nearest.portal->id) continue;
        nearest = {&portal, t};
    }
    return nearest;
}
} // namespace

bool ConnectedSpaces::addRegion(SpaceRegionId id) {
    if (!id || containsRegion(id)) return false;
    _regions.push_back(id);
    return true;
}
bool ConnectedSpaces::removeRegion(SpaceRegionId id) {
    const auto found = std::find(_regions.begin(), _regions.end(), id);
    if (found == _regions.end()) return false;
    _regions.erase(found);
    std::erase_if(_portals, [id](const SpacePortal& p) {
        return p.source == id || p.destination == id;
    });
    return true;
}
bool ConnectedSpaces::containsRegion(SpaceRegionId id) const noexcept {
    return id && std::find(_regions.begin(), _regions.end(), id) != _regions.end();
}
const SpacePortal* ConnectedSpaces::portal(SpacePortalId id) const noexcept {
    const auto found = std::find_if(_portals.begin(), _portals.end(),
                                    [id](const SpacePortal& p) { return p.id == id; });
    return found == _portals.end() ? nullptr : &*found;
}
bool ConnectedSpaces::setPortal(const SpacePortal& value) {
    if (!value.id || !containsRegion(value.source) || !containsRegion(value.destination)
        || !valid(value.entrance) || !valid(value.exit)
        || !std::isfinite(value.halfWidth) || !std::isfinite(value.halfHeight)
        || value.halfWidth <= 0 || value.halfHeight <= 0) return false;
    auto found = std::find_if(_portals.begin(), _portals.end(),
                              [&](const SpacePortal& p) { return p.id == value.id; });
    if (found == _portals.end()) _portals.push_back(value);
    else *found = value;
    return true;
}
bool ConnectedSpaces::removePortal(SpacePortalId id) {
    const auto size = _portals.size();
    std::erase_if(_portals, [id](const SpacePortal& p) { return p.id == id; });
    return _portals.size() != size;
}
bool ConnectedSpaces::mapPose(SpacePortalId id, const SpacePose& source,
                              SpacePose& destination) const noexcept {
    const auto* link = portal(id);
    if (!link || !link->enabled || !valid(source)) return false;
    destination = mappedPose(*link, source);
    return valid(destination);
}
bool ConnectedSpaces::mapVector(SpacePortalId id, const math::FVector3& source,
                                math::FVector3& destination) const noexcept {
    const auto* link = portal(id);
    if (!link || !link->enabled || !finite(source)) return false;
    const auto result = mappedVector(*link, source);
    if (!finite(result)) return false;
    destination = result;
    return true;
}
bool ConnectedSpaces::travel(const SpaceMotion& start, const math::FVector3& displacement,
                             uint32_t maxHops, SpaceTraversal& output) const noexcept {
    if (!containsRegion(start.region) || !valid(start.pose) || !finite(start.velocity)
        || !finite(displacement)) return false;
    SpaceTraversal result;
    result.state = start;
    auto remaining = displacement;
    while (true) {
        const auto crossing = nearestCrossing(_portals, result.state.region,
                                               result.state.pose.position, remaining);
        if (!crossing.portal) {
            result.state.pose.position += remaining;
            break;
        }
        const auto intersection = result.state.pose.position + remaining * crossing.fraction;
        if (result.crossings == maxHops) {
            result.state.pose.position = intersection;
            result.hopLimitReached = true;
            break;
        }
        const SpacePose atPortal{intersection, result.state.pose.rotation};
        result.state.pose = mappedPose(*crossing.portal, atPortal);
        result.state.velocity = mappedVector(*crossing.portal, result.state.velocity);
        remaining = mappedVector(*crossing.portal, remaining * (1.0f - crossing.fraction));
        result.state.region = crossing.portal->destination;
        ++result.crossings;
        // Keep the next region's starting point on its visible side when a
        // ray/step lands exactly on the exit plane. This is not a physical gap.
        if (remaining.lengthSq() <= kPlaneEpsilon * kPlaneEpsilon) break;
    }
    if (!valid(result.state.pose) || !finite(result.state.velocity)) return false;
    output = result;
    return true;
}
SpaceTrace ConnectedSpaces::traceRay(SpaceRegionId region, const math::Ray& ray,
                                     float maxDistance, uint32_t maxHops,
                                     const SpaceRayQuery& query) const {
    SpaceTrace result;
    result.region = region;
    result.position = ray.origin;
    if (!containsRegion(region) || !finite(ray.origin) || !finite(ray.dir)
        || !std::isfinite(maxDistance) || maxDistance < 0 || !query
        || ray.dir.lengthSq() <= 0) {
        result.status = SpaceTraceStatus::QueryFailed;
        return result;
    }
    math::FVector3 origin = ray.origin;
    math::FVector3 direction = ray.dir.normalize();
    float remaining = maxDistance;
    while (true) {
        const auto crossing = nearestCrossing(_portals, result.region, origin,
                                               direction * remaining);
        const float untilPortal = crossing.portal ? remaining * crossing.fraction : remaining;
        const auto local = query(result.region, math::Ray{origin, direction}, untilPortal);
        if (!local || (local->hit && (!std::isfinite(local->distance)
            || local->distance < 0 || local->distance > untilPortal + kPlaneEpsilon))) {
            result.status = SpaceTraceStatus::QueryFailed;
            return result;
        }
        if (local->hit) {
            result.status = SpaceTraceStatus::Hit;
            result.distance += local->distance;
            result.position = origin + direction * local->distance;
            return result;
        }
        result.distance += untilPortal;
        result.position = origin + direction * untilPortal;
        if (!crossing.portal) return result;
        if (result.crossings == maxHops) {
            result.status = SpaceTraceStatus::HopLimit;
            return result;
        }
        const SpacePose mapped = mappedPose(*crossing.portal, {result.position, {0, 0, 0, 1}});
        direction = mappedVector(*crossing.portal, direction).normalize();
        remaining -= untilPortal;
        result.region = crossing.portal->destination;
        ++result.crossings;
        if (remaining <= 0) { result.position = mapped.position; return result; }
        // Count the separation as traveled distance and remove it from the
        // remaining budget; a chain of portals must not extend the ray.
        const float offset = std::min(kExitOffset, remaining * 0.5f);
        origin = mapped.position + direction * offset;
        remaining -= offset;
        result.distance += offset;
    }
}

} // namespace ayt::entity
