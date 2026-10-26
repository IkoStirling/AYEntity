#include <AYEntity/SeatRegistry.h>

#include <AYEntity/EntityImpl.h>
#include <AYEntity/World.h>

#include <algorithm>
#include <limits>
#include <new>

namespace ayt::entity {

bool SeatRegistry::liveSpatial(EntityHandle handle) const noexcept {
    if (!handle.isValid()) return false;
    const auto* entity = _world->getEntityByHandle(handle);
    return entity && entity->isSpatial();
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

} // namespace ayt::entity
