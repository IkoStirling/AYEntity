#pragma once
#include <AYEntity/ConnectedSpaces.h>
#include <AYPhysics/PhysicsTypes.h>
#include <map>
#include <optional>

namespace ayt::entity {

struct SpaceBodyRoute {
    physics::PhysLayer layer = 0;
    physics::PhysLayerMask collideMask = 0;
};

/// Shared-world routing for connected regions. Layers 0..15 stay reserved for
/// ordinary physics use; each nonzero region owns one unique layer in 16..31.
/// Configure at a quiescent World boundary before bodies are created. Keep
/// this value alive while a bridge or a resolver borrows it.
class SpacePhysicsRoutes {
public:
    /// Returns false without changing routes for zero IDs, reserved/out-of-range
    /// layers, or a layer already assigned to another region.
    bool set(SpaceRegionId region, physics::PhysLayer layer) {
        if (!region || layer < 16 || layer >= 32) return false;
        for (const auto& [other, route] : _routes)
            if (other != region && route.layer == layer) return false;
        _routes[region] = {layer, physics::PhysLayerMask{1u} << layer};
        return true;
    }
    bool erase(SpaceRegionId region) { return _routes.erase(region) != 0; }
    std::optional<SpaceBodyRoute> resolve(SpaceRegionId region) const {
        const auto it = _routes.find(region);
        if (it == _routes.end()) return std::nullopt;
        return it->second;
    }
private:
    std::map<SpaceRegionId, SpaceBodyRoute> _routes;
};

} // namespace ayt::entity
