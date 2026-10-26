#include <AYEntity/SeatRegistry.h>

#include <AYEntity/EntityImpl.h>
#include <AYEntity/World.h>
#include <AYEntity.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <new>

namespace ayt::entity {
namespace {
bool validStableId(const std::string& id) noexcept {
    if (id.empty() || id.size() > 128) return false;
    for (const auto character : id)
        if (static_cast<unsigned char>(character) < 0x20u ||
            static_cast<unsigned char>(character) == 0x7fu) return false;
    return true;
}
bool validAnchor(SeatAnchor& anchor) noexcept {
    const auto& p = anchor.localPosition;
    const auto norm = anchor.localRotation.dot(anchor.localRotation);
    if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z)
        || !std::isfinite(norm) || norm < 1.0e-12f) return false;
    anchor.localRotation = anchor.localRotation.normalize();
    return true;
}
}

bool SeatRegistry::liveSpatial(EntityHandle handle) const noexcept {
    if (!handle.isValid()) return false;
    const auto* entity = _world->getEntityByHandle(handle);
    return entity && entity->isSpatial();
}
SeatStatus SeatRegistry::setAnchor(SeatKey key, SeatAnchor anchor) noexcept {
    auto* row = find(key);
    if (!row) return SeatStatus::Missing;
    if (!liveSpatial(key.vehicle)) return SeatStatus::Stale;
    if (row->lease.serial) return SeatStatus::Occupied;
    if (!validAnchor(anchor)) return SeatStatus::Invalid;
    row->anchor = anchor;
    return SeatStatus::Ok;
}
SeatRegistry::Row* SeatRegistry::find(SeatKey key) noexcept {
    const auto it = std::find_if(_rows.begin(), _rows.end(),
        [&](const Row& row) { return row.key == key; });
    return it == _rows.end() ? nullptr : &*it;
}
const SeatRegistry::Row* SeatRegistry::find(SeatKey key) const noexcept {
    const auto it = std::find_if(_rows.begin(), _rows.end(),
        [&](const Row& row) { return row.key == key; });
    return it == _rows.end() ? nullptr : &*it;
}
SeatStatus SeatRegistry::define(SeatKey key, SeatRole role) {
    if (!key.slot || !liveSpatial(key.vehicle)
        || (role != SeatRole::Passenger && role != SeatRole::Driver))
        return SeatStatus::Invalid;
    if (find(key)) return SeatStatus::Occupied;
    if (_rows.size() >= kMaxSeats) return SeatStatus::Capacity;
    if (role == SeatRole::Driver)
        for (const auto& row : _rows)
            if (row.key.vehicle == key.vehicle && row.role == role)
                return SeatStatus::Occupied;
    try { _rows.push_back({key, role, {}}); }
    catch (const std::bad_alloc&) { return SeatStatus::Capacity; }
    return SeatStatus::Ok;
}
SeatStatus SeatRegistry::erase(SeatKey key) noexcept {
    const auto it = std::find_if(_rows.begin(), _rows.end(),
        [&](const Row& row) { return row.key == key; });
    if (it == _rows.end()) return SeatStatus::Missing;
    if (it->lease.serial) return SeatStatus::Occupied;
    _rows.erase(it);
    return SeatStatus::Ok;
}
EntityHandle SeatRegistry::occupant(SeatKey key) const noexcept {
    const auto* row = find(key);
    return row && liveSpatial(row->key.vehicle)
        && liveSpatial(row->lease.occupant) ? row->lease.occupant
        : EntityHandle{};
}
SeatLease SeatRegistry::lease(SeatKey key) const noexcept {
    const auto* row = find(key);
    return row && row->lease.serial && liveSpatial(row->key.vehicle) &&
        liveSpatial(row->lease.occupant) ? row->lease : SeatLease{};
}
EntityHandle SeatRegistry::driver(EntityHandle vehicle) const noexcept {
    if (!liveSpatial(vehicle)) return {};
    for (const auto& row : _rows)
        if (row.key.vehicle == vehicle && row.role == SeatRole::Driver
            && liveSpatial(row.lease.occupant))
            return row.lease.occupant;
    return {};
}
EntityHandle SeatRegistry::controlTarget(EntityHandle actor) const noexcept {
    if (!liveSpatial(actor)) return {};
    for (const auto& row : _rows)
        if (row.role == SeatRole::Driver && row.lease.occupant == actor
            && row.lease.serial && liveSpatial(row.key.vehicle))
            return row.key.vehicle;
    return actor;
}
SeatDecision SeatRegistry::board(SeatKey key, EntityHandle actor) noexcept {
    SeatDecision result;
    auto* row = find(key);
    if (!row) { result.status = SeatStatus::Missing; return result; }
    if (!liveSpatial(key.vehicle) || !liveSpatial(actor)) {
        result.status = SeatStatus::Stale; return result;
    }
    if (actor == key.vehicle) return result;
    if (row->lease.serial) { result.status = SeatStatus::Occupied; return result; }
    for (const auto& occupied : _rows)
        if (occupied.lease.serial && occupied.lease.occupant == actor) {
            result.status = SeatStatus::AlreadySeated; return result;
        }
    if (_nextSerial == std::numeric_limits<std::uint64_t>::max()) {
        result.status = SeatStatus::Capacity; return result;
    }
    result.driverBefore = driver(key.vehicle);
    row->lease = {key, actor, _nextSerial++};
    result.lease = row->lease;
    result.driverAfter = driver(key.vehicle);
    result.status = SeatStatus::Ok;
    return result;
}
SeatDecision SeatRegistry::move(SeatLease lease, SeatKey destination) noexcept {
    SeatDecision result;
    auto* source = find(lease.key);
    auto* target = find(destination);
    if (!source || !target) { result.status = SeatStatus::Missing; return result; }
    if (!lease.serial || source->lease != lease) {
        result.status = SeatStatus::Stale; return result;
    }
    if (destination == lease.key || destination.vehicle != lease.key.vehicle)
        return result;
    if (!liveSpatial(lease.key.vehicle) || !liveSpatial(lease.occupant)) {
        result.status = SeatStatus::Stale; return result;
    }
    if (target->lease.serial) { result.status = SeatStatus::Occupied; return result; }
    if (_nextSerial == std::numeric_limits<std::uint64_t>::max()) {
        result.status = SeatStatus::Capacity; return result;
    }
    result.driverBefore = driver(lease.key.vehicle);
    source->lease = {};
    target->lease = {destination, lease.occupant, _nextSerial++};
    result.lease = target->lease;
    result.driverAfter = driver(lease.key.vehicle);
    result.status = SeatStatus::Ok;
    return result;
}
SeatDecision SeatRegistry::leave(SeatLease lease) noexcept {
    SeatDecision result;
    auto* row = find(lease.key);
    if (!row) { result.status = SeatStatus::Missing; return result; }
    if (!lease.serial || row->lease != lease) {
        result.status = SeatStatus::Stale; return result;
    }
    result.driverBefore = driver(lease.key.vehicle);
    row->lease = {};
    result.lease = lease;
    result.driverAfter = driver(lease.key.vehicle);
    result.status = SeatStatus::Ok;
    return result;
}
SeatStatus SeatRegistry::seatWorldPose(SeatLease lease, SeatPose& pose,
                                        float alpha) const {
    const auto* row = find(lease.key);
    if (!row) return SeatStatus::Missing;
    if (!lease.serial || row->lease != lease
        || !liveSpatial(lease.key.vehicle) || !liveSpatial(lease.occupant))
        return SeatStatus::Stale;
    if (!std::isfinite(alpha) || alpha < 0.0f || alpha > 1.0f)
        return SeatStatus::Invalid;
    const auto* vehicle = _world->getEntityByHandle(lease.key.vehicle);
    math::FVector3 vehiclePosition;
    math::FQuaternion vehicleRotation;
    if (!vehicle->worldPose(vehiclePosition, vehicleRotation, alpha))
        return SeatStatus::PoseUnavailable;
    const auto local = row->anchor.localPosition;
    const auto transformed = vehicle->worldMatrix(alpha)
        * math::FVector4{local.x, local.y, local.z, 1.0f};
    const math::FVector3 position{transformed.x, transformed.y, transformed.z};
    const auto rotation = (vehicleRotation * row->anchor.localRotation).normalize();
    if (!std::isfinite(position.x) || !std::isfinite(position.y)
        || !std::isfinite(position.z) || !std::isfinite(rotation.dot(rotation)))
        return SeatStatus::PoseUnavailable;
    pose = {position, rotation};
    return SeatStatus::Ok;
}
SeatStatus SeatRegistry::applyPresentationPose(SeatLease lease, float alpha) {
    SeatPose pose;
    const auto status = seatWorldPose(lease, pose, alpha);
    if (status != SeatStatus::Ok) return status;
    auto* actor = _world->getEntityByHandle(lease.occupant);
    if (actor->hasComponent<SimTransformComponent>()
        || actor->hasComponent<DetSimTransformComponent>())
        return SeatStatus::AuthorityConflict;
    auto* rigid = actor->getComponent<RigidBodyComponent>();
    if (rigid && !rigid->isKinematic() && !rigid->isStatic())
        return SeatStatus::AuthorityConflict;
    return actor->setWorldPose(pose.position, pose.rotation, alpha)
        ? SeatStatus::Ok : SeatStatus::PoseUnavailable;
}
std::vector<SeatLease> SeatRegistry::pruneStale() {
    std::vector<SeatLease> ejected;
    ejected.reserve(_rows.size());
    for (auto it = _rows.begin(); it != _rows.end();) {
        if (!liveSpatial(it->key.vehicle)) {
            if (it->lease.serial && liveSpatial(it->lease.occupant))
                ejected.push_back(it->lease);
            it = _rows.erase(it);
        } else {
            if (it->lease.serial && !liveSpatial(it->lease.occupant))
                it->lease = {};
            ++it;
        }
    }
    return ejected;
}

SeatStatus SeatRegistry::capturePersistent(
    const std::function<std::string(EntityHandle)>& stableId,
    PersistentSeatState& out) const {
    if (!stableId) return SeatStatus::Invalid;
    try {
        PersistentSeatState candidate;
        candidate.seats.reserve(_rows.size());
        std::vector<std::pair<std::string, EntityHandle>> identities;
        identities.reserve(_rows.size() * 2u);
        const auto checkIdentity = [&](const std::string& id,
                                       EntityHandle handle) {
            if (!validStableId(id)) return false;
            for (const auto& [priorId, priorHandle] : identities)
                if ((priorId == id && priorHandle != handle) ||
                    (priorId != id && priorHandle == handle)) return false;
            identities.emplace_back(id, handle);
            return true;
        };
        for (const auto& row : _rows) {
            if (!liveSpatial(row.key.vehicle) ||
                (row.lease.serial && !liveSpatial(row.lease.occupant)))
                return SeatStatus::Stale;
            PersistentSeatRow saved;
            saved.vehicleId = stableId(row.key.vehicle);
            saved.slot = row.key.slot;
            saved.role = row.role;
            saved.anchor = row.anchor;
            if (row.lease.serial)
                saved.occupantId = stableId(row.lease.occupant);
            if (!checkIdentity(saved.vehicleId, row.key.vehicle) ||
                (row.lease.serial &&
                 !checkIdentity(saved.occupantId, row.lease.occupant)))
                return SeatStatus::Invalid;
            candidate.seats.push_back(std::move(saved));
        }
        std::sort(candidate.seats.begin(), candidate.seats.end(),
            [](const PersistentSeatRow& a, const PersistentSeatRow& b) {
                return a.vehicleId == b.vehicleId ? a.slot < b.slot :
                    a.vehicleId < b.vehicleId;
            });
        out = std::move(candidate);
        return SeatStatus::Ok;
    } catch (const std::bad_alloc&) { return SeatStatus::Capacity; }
      catch (...) { return SeatStatus::Invalid; }
}

SeatStatus SeatRegistry::restorePersistent(
    const PersistentSeatState& state,
    const std::function<EntityHandle(std::string_view)>& resolve) {
    if (!resolve || state.schema != 1 || state.seats.size() > kMaxSeats)
        return SeatStatus::Invalid;
    try {
        std::vector<Row> candidate;
        candidate.reserve(state.seats.size());
        std::vector<std::pair<std::string, EntityHandle>> vehicleIds;
        vehicleIds.reserve(state.seats.size());
        std::vector<std::pair<std::string, EntityHandle>> occupantIds;
        occupantIds.reserve(state.seats.size());
        std::size_t occupied = 0;
        for (const auto& saved : state.seats) {
            if (!validStableId(saved.vehicleId) || !saved.slot ||
                (saved.role != SeatRole::Passenger && saved.role != SeatRole::Driver) ||
                (!saved.occupantId.empty() && !validStableId(saved.occupantId)))
                return SeatStatus::Invalid;
            auto anchor = saved.anchor;
            if (!validAnchor(anchor)) return SeatStatus::Invalid;
            const auto vehicle = resolve(saved.vehicleId);
            if (!liveSpatial(vehicle)) return SeatStatus::Stale;
            for (const auto& [id, handle] : vehicleIds)
                if ((id == saved.vehicleId && handle != vehicle) ||
                    (id != saved.vehicleId && handle == vehicle))
                    return SeatStatus::Invalid;
            vehicleIds.emplace_back(saved.vehicleId, vehicle);
            const SeatKey key{vehicle, saved.slot};
            for (const auto& previous : candidate)
                if (previous.key == key ||
                    (saved.role == SeatRole::Driver &&
                     previous.key.vehicle == vehicle &&
                     previous.role == SeatRole::Driver))
                    return SeatStatus::Occupied;
            EntityHandle occupant;
            if (!saved.occupantId.empty()) {
                occupant = resolve(saved.occupantId);
                if (!liveSpatial(occupant)) return SeatStatus::Stale;
                if (occupant == vehicle) return SeatStatus::Invalid;
                for (const auto& [id, handle] : occupantIds)
                    if ((id == saved.occupantId && handle != occupant) ||
                        (id != saved.occupantId && handle == occupant))
                        return SeatStatus::Invalid;
                occupantIds.emplace_back(saved.occupantId, occupant);
                for (const auto& previous : candidate)
                    if (previous.lease.serial && previous.lease.occupant == occupant)
                        return SeatStatus::AlreadySeated;
                ++occupied;
            }
            candidate.push_back({key, saved.role,
                {key, occupant, occupant.isValid() ? 1ull : 0ull}, anchor});
        }
        if (occupied >= std::numeric_limits<std::uint64_t>::max() - _nextSerial)
            return SeatStatus::Capacity;
        auto nextSerial = _nextSerial;
        for (auto& row : candidate)
            if (row.lease.serial) row.lease.serial = nextSerial++;
        _rows.swap(candidate);
        _nextSerial = nextSerial;
        return SeatStatus::Ok;
    } catch (const std::bad_alloc&) { return SeatStatus::Capacity; }
      catch (...) { return SeatStatus::Invalid; }
}

} // namespace ayt::entity
