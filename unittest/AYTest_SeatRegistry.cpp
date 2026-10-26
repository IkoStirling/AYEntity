#include <AYEntity/SeatRegistry.h>
#include <AYEntity/World.h>
#include <AYEntity/EntityImpl.h>
#include <AYTest.h>

using namespace ayt::entity;

TEST_SUITE(SeatRegistryTests)

TEST_CASE(board_move_and_leave_transfer_driver_without_double_occupancy) {
    auto& world = World::instance();
    CHECK(world.initialize());
    auto* vehicle = world.createSpatialEntity();
    auto* actor = world.createSpatialEntity();
    auto* passenger = world.createSpatialEntity();
    auto* otherVehicle = world.createSpatialEntity();
    const auto vehicleHandle = world.getEntityHandle(vehicle->getId());
    const auto actorHandle = world.getEntityHandle(actor->getId());
    const auto passengerHandle = world.getEntityHandle(passenger->getId());
    const auto otherVehicleHandle = world.getEntityHandle(otherVehicle->getId());
    SeatRegistry seats(world);
    const SeatKey driverSeat{vehicleHandle, 1};
    const SeatKey passengerSeat{vehicleHandle, 2};
    const SeatKey spareSeat{vehicleHandle, 3};
    CHECK(seats.define(driverSeat, SeatRole::Driver) == SeatStatus::Ok);
    CHECK(seats.define(passengerSeat, SeatRole::Passenger) == SeatStatus::Ok);
    CHECK(seats.define(spareSeat, SeatRole::Passenger) == SeatStatus::Ok);
    CHECK(seats.define({otherVehicleHandle, 1}, SeatRole::Driver)
        == SeatStatus::Ok);
    CHECK(seats.define({vehicleHandle, 4}, SeatRole::Driver)
        == SeatStatus::Occupied);
    const auto p = seats.board(passengerSeat, passengerHandle);
    CHECK(p.status == SeatStatus::Ok && !p.driverAfter.isValid());
    CHECK(seats.controlTarget(passengerHandle) == passengerHandle);
    const auto d = seats.board(driverSeat, actorHandle);
    CHECK(d.status == SeatStatus::Ok && d.driverAfter == actorHandle);
    CHECK(seats.driver(vehicleHandle) == actorHandle);
    CHECK(seats.controlTarget(actorHandle) == vehicleHandle);
    CHECK(seats.board(spareSeat, passengerHandle).status
        == SeatStatus::AlreadySeated);
    CHECK(seats.board({otherVehicleHandle, 1}, passengerHandle).status
        == SeatStatus::AlreadySeated);
    CHECK(seats.move(p.lease, driverSeat).status == SeatStatus::Occupied);
    const auto left = seats.leave(d.lease);
    CHECK(left.status == SeatStatus::Ok
        && left.driverBefore == actorHandle
        && !left.driverAfter.isValid());
    const auto moved = seats.move(p.lease, driverSeat);
    CHECK(moved.status == SeatStatus::Ok && moved.driverAfter == passengerHandle);
    CHECK(seats.move(p.lease, spareSeat).status == SeatStatus::Stale);
    CHECK(!seats.occupant(passengerSeat).isValid());
    CHECK(seats.occupant(driverSeat) == passengerHandle);
    CHECK(seats.controlTarget(passengerHandle) == vehicleHandle);
    CHECK(seats.leave(moved.lease).status == SeatStatus::Ok);
    CHECK(!seats.driver(vehicleHandle).isValid());
    CHECK(seats.controlTarget(passengerHandle) == passengerHandle);
    const auto secondVehicle = seats.board({otherVehicleHandle, 1}, passengerHandle);
    CHECK(secondVehicle.status == SeatStatus::Ok
        && seats.controlTarget(passengerHandle) == otherVehicleHandle);
    CHECK(seats.leave(secondVehicle.lease).status == SeatStatus::Ok);
    world.shutdown();
}

TEST_CASE(destroyed_entities_revoke_control_and_prune_stale_leases) {
    auto& world = World::instance();
    CHECK(world.initialize());
    auto* vehicle = world.createSpatialEntity();
    auto* actor = world.createSpatialEntity();
    auto* replacement = world.createSpatialEntity();
    const auto vehicleHandle = world.getEntityHandle(vehicle->getId());
    const auto actorHandle = world.getEntityHandle(actor->getId());
    const auto replacementHandle = world.getEntityHandle(replacement->getId());
    SeatRegistry seats(world);
    const SeatKey driverSeat{vehicleHandle, 1};
    CHECK(seats.define(driverSeat, SeatRole::Driver) == SeatStatus::Ok);
    const auto first = seats.board(driverSeat, actorHandle);
    CHECK(first.status == SeatStatus::Ok);
    world.destroyEntity(actor);
    CHECK(!seats.driver(vehicleHandle).isValid());
    CHECK(!seats.controlTarget(actorHandle).isValid());
    CHECK(seats.board(driverSeat, replacementHandle).status
        == SeatStatus::Occupied);
    CHECK(seats.pruneStale().empty());
    const auto second = seats.board(driverSeat, replacementHandle);
    CHECK(second.status == SeatStatus::Ok);
    CHECK(seats.leave(first.lease).status == SeatStatus::Stale);
    world.destroyEntity(vehicle);
    CHECK(!seats.driver(vehicleHandle).isValid());
    CHECK(seats.controlTarget(replacementHandle) == replacementHandle);
    const auto ejected = seats.pruneStale();
    CHECK(ejected.size() == 1 && ejected.front() == second.lease);
    CHECK(seats.leave(second.lease).status == SeatStatus::Missing);
    world.shutdown();
}

TEST_SUITE_END
