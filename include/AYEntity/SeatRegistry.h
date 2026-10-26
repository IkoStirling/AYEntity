#pragma once

#include <AYEntity/EntityHandle.h>
#include <AYMath/MathTypes.h>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace ayt::entity {
class World;

struct SeatKey {
    EntityHandle vehicle{};
    std::uint32_t slot = 0;
    bool operator==(const SeatKey&) const = default;
};
enum class SeatRole : std::uint8_t { Passenger, Driver };
enum class SeatStatus : std::uint8_t {
    Ok, Invalid, Stale, Missing, Occupied, AlreadySeated, Capacity,
    AuthorityConflict, PoseUnavailable
};
struct SeatAnchor {
    math::FVector3 localPosition{};
    math::FQuaternion localRotation{0, 0, 0, 1};
};
struct SeatPose {
    math::FVector3 position{};
    math::FQuaternion rotation{0, 0, 0, 1};
};
struct SeatLease {
    SeatKey key{};
    EntityHandle occupant{};
    std::uint64_t serial = 0;
    bool operator==(const SeatLease&) const = default;
};
struct SeatDecision {
    SeatStatus status = SeatStatus::Invalid;
    SeatLease lease{};
    EntityHandle driverBefore{}, driverAfter{};
};

/// World-local seat occupancy and driver identity. Games define seat anchors,
/// eligibility, controls and vehicle physics. The registry never parents an
/// Entity or changes its input/physics authority; a successful decision gives
/// the exact driver transition for the game to apply at its safe boundary.
/// Keep the World alive and use one owner thread. Recreate/rebind after load.
class SeatRegistry {
public:
    static constexpr std::size_t kMaxSeats = 512;
    explicit SeatRegistry(World& world) noexcept : _world(&world) {}

    /// One live Spatial vehicle may have multiple seats but at most one Driver.
    [[nodiscard]] SeatStatus define(SeatKey key, SeatRole role);
    /// Store a finite vehicle-local visual anchor. The seat must be empty;
    /// changing an occupied anchor requires an explicit game transition.
    [[nodiscard]] SeatStatus setAnchor(SeatKey key, SeatAnchor anchor) noexcept;
    [[nodiscard]] SeatStatus erase(SeatKey key) noexcept;
    /// Successful boarding grants a unique lease. An occupant may hold one
    /// seat in this registry; a failed request changes nothing.
    [[nodiscard]] SeatDecision board(SeatKey key, EntityHandle occupant) noexcept;
    /// Transfer within one vehicle, including Passenger <-> Driver, without
    /// an empty-seat intermediate state. The old lease becomes stale.
    [[nodiscard]] SeatDecision move(SeatLease lease, SeatKey destination) noexcept;
    [[nodiscard]] SeatDecision leave(SeatLease lease) noexcept;
    /// Resolve a current lease against the sampled vehicle world pose. A
    /// stale lease never yields a pose, even if its actor occupies another seat.
    [[nodiscard]] SeatStatus seatWorldPose(SeatLease lease, SeatPose& pose,
                                           float interpolationAlpha = 1.0f) const;
    /// Move a presentation-owned occupant to its seat anchor. Call after the
    /// vehicle pose update. Dynamic bodies and authoritative Sim poses are
    /// refused; games still own their physics and input transitions.
    [[nodiscard]] SeatStatus applyPresentationPose(SeatLease lease,
                                                   float interpolationAlpha = 1.0f);
    [[nodiscard]] EntityHandle occupant(SeatKey key) const noexcept;
    [[nodiscard]] EntityHandle driver(EntityHandle vehicle) const noexcept;
    /// Resolve an actor's current command target. A live Driver controls its
    /// vehicle; a Passenger or unseated actor controls itself. Stale actors
    /// resolve to an invalid handle. The game still chooses which actions may
    /// be routed and applies the resulting commands.
    [[nodiscard]] EntityHandle controlTarget(EntityHandle actor) const noexcept;
    /// Remove destroyed vehicles and release destroyed occupants. Returned
    /// leases identify still-live occupants ejected by vehicle destruction;
    /// the game handles their pose and collision. Call at a quiescent boundary.
    [[nodiscard]] std::vector<SeatLease> pruneStale();

private:
    struct Row { SeatKey key; SeatRole role; SeatLease lease; SeatAnchor anchor; };
    World* _world;
    std::vector<Row> _rows;
    std::uint64_t _nextSerial = 1;
    [[nodiscard]] bool liveSpatial(EntityHandle handle) const noexcept;
    [[nodiscard]] Row* find(SeatKey key) noexcept;
    [[nodiscard]] const Row* find(SeatKey key) const noexcept;
};

} // namespace ayt::entity
