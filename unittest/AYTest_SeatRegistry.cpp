#include <AYEntity/SeatRegistry.h>
#include <AYEntity/World.h>
#include <AYEntity/EntityImpl.h>
#include <AYEntity/components/TransformComponent.h>
#include <AYEntity/components/SimTransformComponent.h>
#include <AYEntity/components/RigidBodyComponent.h>
#include <AYMath/MathUtils.h>
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

TEST_CASE(vehicle_local_anchor_follows_vehicle_and_rejects_stale_lease) {
    auto& world = World::instance();
    CHECK(world.initialize());
    auto* vehicle = world.createSpatialEntity();
    auto* actor = world.createSpatialEntity();
    const SeatKey front{world.getEntityHandle(vehicle->getId()), 1};
    const SeatKey rear{front.vehicle, 2};
    SeatRegistry seats(world);
    CHECK(seats.define(front, SeatRole::Driver) == SeatStatus::Ok);
    CHECK(seats.define(rear, SeatRole::Passenger) == SeatStatus::Ok);
    SeatAnchor anchor;
    anchor.localPosition = {1, 0, 0};
    anchor.localRotation = ayt::math::FQuaternion::fromAxisAngle(
        {0, 1, 0}, ayt::math::radians(90.f));
    CHECK(seats.setAnchor(front, anchor) == SeatStatus::Ok);
    vehicle->transform()->position = {10, 0, 0};
    vehicle->transform()->scale = {2, 1, 1};
    vehicle->transform()->rotation = anchor.localRotation;
    const auto boarding = seats.board(front, world.getEntityHandle(actor->getId()));
    CHECK(boarding.status == SeatStatus::Ok);
    CHECK(seats.setAnchor(front, anchor) == SeatStatus::Occupied);
    SeatPose pose;
    CHECK(seats.seatWorldPose(boarding.lease, pose) == SeatStatus::Ok);
    CHECK_FLOAT_EQ(pose.position.x, 10, 1e-4f);
    CHECK_FLOAT_EQ(pose.position.z, -2, 1e-4f);
    CHECK(seats.applyPresentationPose(boarding.lease) == SeatStatus::Ok);
    ayt::math::FVector3 actualPosition;
    ayt::math::FQuaternion actualRotation;
    CHECK(actor->worldPose(actualPosition, actualRotation));
    CHECK_FLOAT_EQ(actualPosition.z, -2, 1e-4f);
    vehicle->transform()->position = {20, 0, 0};
    CHECK(seats.applyPresentationPose(boarding.lease) == SeatStatus::Ok);
    CHECK(actor->worldPose(actualPosition, actualRotation));
    CHECK_FLOAT_EQ(actualPosition.x, 20, 1e-4f);
    const auto moved = seats.move(boarding.lease, rear);
    CHECK(moved.status == SeatStatus::Ok);
    CHECK(seats.seatWorldPose(boarding.lease, pose) == SeatStatus::Stale);
    CHECK(seats.applyPresentationPose(boarding.lease) == SeatStatus::Stale);
    world.shutdown();
}

TEST_CASE(authoritative_actor_pose_is_not_overwritten) {
    auto& world = World::instance();
    CHECK(world.initialize());
    auto* vehicle = world.createSpatialEntity();
    auto* actor = world.createSpatialEntity();
    const SeatKey seat{world.getEntityHandle(vehicle->getId()), 1};
    SeatRegistry seats(world);
    CHECK(seats.define(seat, SeatRole::Passenger) == SeatStatus::Ok);
    auto anchor = SeatAnchor{};
    anchor.localRotation = {0, 0, 0, 0};
    CHECK(seats.setAnchor(seat, anchor) == SeatStatus::Invalid);
    CHECK(seats.setAnchor(seat, SeatAnchor{}) == SeatStatus::Ok);
    const auto lease = seats.board(seat, world.getEntityHandle(actor->getId())).lease;
    actor->transform()->position = {7, 0, 0};
    CHECK(actor->addComponent<SimTransformComponent>() != nullptr);
    CHECK(seats.applyPresentationPose(lease) == SeatStatus::AuthorityConflict);
    CHECK_FLOAT_EQ(actor->transform()->position.x, 7, 0);
    actor->removeComponent<SimTransformComponent>();
    auto* rigid = actor->addComponent<RigidBodyComponent>();
    CHECK(rigid != nullptr);
    CHECK(seats.applyPresentationPose(lease) == SeatStatus::AuthorityConflict);
    rigid->setKinematic(true);
    CHECK(seats.applyPresentationPose(lease) == SeatStatus::Ok);
    world.shutdown();
}

TEST_SUITE_END
