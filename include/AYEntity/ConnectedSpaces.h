#pragma once

#include <AYMath/MathGeometry.h>
#include <AYMath/MathTypes.h>

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace ayt::entity {

using SpaceRegionId = uint32_t;
using SpacePortalId = uint64_t;

/// Rigid world pose. Portal traversal preserves lengths; scaled/sheared portal
/// frames are deliberately excluded so rendering, motion and queries agree.
struct SpacePose {
    math::FVector3 position{};
    math::FQuaternion rotation{0, 0, 0, 1};
};

/// A directed, rectangular opening. The source region lies on the local +Z
/// side of entrance; crossing local +Z to -Z emerges on the exit's local +Z
/// side. Add the reverse direction explicitly when both sides are traversable.
struct SpacePortal {
    SpacePortalId id = 0;
    SpaceRegionId source = 0;
    SpaceRegionId destination = 0;
    SpacePose entrance{};
    SpacePose exit{};
    float halfWidth = 1.0f;
    float halfHeight = 1.0f;
    bool enabled = true;
};

struct SpaceMotion {
    SpaceRegionId region = 0;
    SpacePose pose{};
    math::FVector3 velocity{};
};

struct SpaceTraversal {
    SpaceMotion state{};
    uint32_t crossings = 0;
    bool hopLimitReached = false;
};

/// Per-region local query result. A successful miss is hit=false. Distances
/// are measured from the supplied ray origin, along its normalized direction.
struct SpaceLocalHit {
    bool hit = false;
    float distance = 0;
};

enum class SpaceTraceStatus : uint8_t { Hit, Miss, QueryFailed, HopLimit };
struct SpaceTrace {
    SpaceTraceStatus status = SpaceTraceStatus::Miss;
    SpaceRegionId region = 0;
    math::FVector3 position{};
    float distance = 0;
    uint32_t crossings = 0;
};

/// One region-local physics query. A null result means the query failed;
/// implementations must filter their own bodies by region.
using SpaceRayQuery = std::function<std::optional<SpaceLocalHit>(
    SpaceRegionId, const math::Ray&, float maxDistance)>;

/// World-local topology shared by movement, view planning and spatial queries.
/// This value does not own entities, physics bodies or GPU resources. Mutate it
/// only at a quiescent world boundary; snapshots/callers must not retain portal
/// pointers across mutation.
class ConnectedSpaces {
public:
    bool addRegion(SpaceRegionId id);
    bool removeRegion(SpaceRegionId id);
    bool setPortal(const SpacePortal& portal);
    bool removePortal(SpacePortalId id);
    bool containsRegion(SpaceRegionId id) const noexcept;
    const SpacePortal* portal(SpacePortalId id) const noexcept;
    const std::vector<SpaceRegionId>& regions() const noexcept { return _regions; }
    const std::vector<SpacePortal>& portals() const noexcept { return _portals; }

    /// Applies the same rigid crossing transform used by travel and trace.
    bool mapPose(SpacePortalId id, const SpacePose& source, SpacePose& destination) const noexcept;
    bool mapVector(SpacePortalId id, const math::FVector3& source,
                   math::FVector3& destination) const noexcept;

    /// Move through every crossed opening, bounded by maxHops. A blocked hop
    /// leaves the remaining displacement unconsumed and reports hopLimitReached.
    /// Invalid input fails without changing output.
    bool travel(const SpaceMotion& start, const math::FVector3& displacement,
                uint32_t maxHops, SpaceTraversal& output) const noexcept;

    /// Cast a finite ray through connected regions. The query is called once
    /// per visited region and never beyond the nearest opening. Local hits at
    /// or before the opening take precedence; exhausted hop budget is explicit.
    /// A tiny counted exit separation avoids immediately re-hitting a paired
    /// portal plane and remains within maxDistance.
    SpaceTrace traceRay(SpaceRegionId region, const math::Ray& ray, float maxDistance,
                        uint32_t maxHops, const SpaceRayQuery& query) const;

private:
    std::vector<SpaceRegionId> _regions;
    std::vector<SpacePortal> _portals;
};

} // namespace ayt::entity
