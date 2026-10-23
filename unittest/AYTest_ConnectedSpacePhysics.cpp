#include <AYEntity/ConnectedSpacePhysicsQuery.h>
#include <AYEntity/ConnectedSpacePhysicsTransfer.h>
#include <AYEntity.h>
#include <AYEntity/EntityModule.h>
#include <AYEntity/EntityPhysicsIntegrationModule.h>
#include <AYEntity/World.h>
#include <AYEntity/components/RigidBodyComponent.h>
#include <AYPhysics/PhysicsManager.h>
#include <AYPhysics/PhysicsBackendTestAccess.h>
#include <AYPhysics/PhysicsWorld3D.h>
#include <AYTest.h>

using namespace ayt::entity;
namespace math = ayt::math;
namespace physics = ayt::physics;
namespace {
struct MockSpaceQuery : physics::IPhysicsQuery {
    float distance = 0;
    uint32_t calls = 0;
    bool ready = true;
    bool isReady() const override { return ready; }
    physics::PhysResult raycastSync(const math::Ray&,physics::RaycastHit& hit,
                                   physics::PhysLayerMask) override {
        ++calls; hit={}; hit.hit=true; hit.distance=distance;
        return physics::PhysResult::Ok;
    }
    physics::PhysResult overlapSphereSync(const math::FVector3&,float,
        std::vector<physics::BodyHandle>&,physics::PhysLayerMask) override {
        return physics::PhysResult::Unsupported;
    }
};
}
TEST_SUITE(ConnectedSpacePhysicsTests)
TEST_CASE(region_queries_stop_at_portal_and_continue_in_destination) {
    World& world=World::instance(); CHECK(world.initialize());
    auto& graph=world.connectedSpaces();
    CHECK(graph.addRegion(1)); CHECK(graph.addRegion(2));
    SpacePortal p; p.id=1;p.source=1;p.destination=2;p.exit.position={10,0,0};
    CHECK(graph.setPortal(p));
    MockSpaceQuery sourceQuery;sourceQuery.distance=3;
    MockSpaceQuery destinationQuery;destinationQuery.distance=1;
    const auto result=raycastConnectedSpaces(world,1,{{0,0,1},{0,0,-1}},5,2,
        [&](SpaceRegionId region)->std::optional<SpacePhysicsRegion>{
            return SpacePhysicsRegion{region==1?&sourceQuery:&destinationQuery,region==1?1u:2u};
        });
    CHECK(sourceQuery.calls==1);CHECK(destinationQuery.calls==1);
    CHECK(result.path.status==SpaceTraceStatus::Hit);
    CHECK(result.path.region==2);CHECK(result.path.crossings==1);
    CHECK_FLOAT_EQ(result.path.distance,2,2e-4f);
    CHECK(result.localHit.hit);CHECK_FLOAT_EQ(result.localHit.distance,1,1e-4f);
    destinationQuery.ready=false;
    const auto failed=raycastConnectedSpaces(world,1,{{0,0,1},{0,0,-1}},5,2,
        [&](SpaceRegionId region)->std::optional<SpacePhysicsRegion>{
            return SpacePhysicsRegion{region==1?&sourceQuery:&destinationQuery,0xFFFFFFFFu};
        });
    CHECK(failed.path.status==SpaceTraceStatus::QueryFailed);
    CHECK(failed.physicsResult==physics::PhysResult::InvalidState);
    world.shutdown();
}
TEST_CASE(dynamic_body_transfer_updates_region_only_after_one_command_is_admitted) {
    CHECK(registerEntityCoreComponents(ComponentRegistry::instance()));
    CHECK(registerEntityPhysicsComponents(ComponentRegistry::instance()));
    World& world=World::instance();CHECK(world.initialize());
    auto& graph=world.connectedSpaces();
    CHECK(graph.addRegion(1));CHECK(graph.addRegion(2));
    SpacePortal portal;portal.id=1;portal.source=1;portal.destination=2;
    portal.exit.position={10,0,0};CHECK(graph.setPortal(portal));
    auto* entity=world.createSpatialEntity();CHECK(entity!=nullptr);
    CHECK(entity->setSpaceRegion(1));
    auto* component=entity->addComponent<RigidBodyComponent>();
    CHECK(component!=nullptr);
    physics::PhysicsBackendDescriptor desc;desc.kind3D=physics::BackendKind::Mock;
    auto refusedManager=physics::PhysicsManager::create(desc);
    CHECK(refusedManager!=nullptr);
    auto* refusedWorld=refusedManager->world3D();
    refusedManager->shutdown();
    auto manager=physics::PhysicsManager::create(desc);CHECK(manager!=nullptr);
    auto* physicsWorld=manager->world3D();
    physics::BodyHandle handle=physics::InvalidBodyHandle;
    physics::RigidbodyDesc bodyDesc;
    CHECK(physicsWorld->createRigidbody(bodyDesc,handle)==physics::PhysResult::Ok);
    component->setBodyHandle(handle);
    physics::BodyTransform snapshot;snapshot.body=handle;snapshot.position={0,0,1};
    snapshot.rotation={0,0,0,1};snapshot.linearVelocity={0,0,-4};
    snapshot.angularVelocity={1,2,3};
    SpaceTraversal result;
    const auto route=[](SpaceRegionId region)->std::optional<SpaceBodyRoute> {
        if(region==2) return SpaceBodyRoute{2,1u<<2};
        return std::nullopt;
    };
    CHECK(transferSpatialRigidBodyAcrossSpaces(*entity,snapshot,{0,0,-3},2,
        *physicsWorld,{},result)==physics::PhysResult::InvalidParam);
    CHECK(entity->spaceRegion()==1);
    CHECK(transferSpatialRigidBodyAcrossSpaces(*entity,snapshot,{0,0,-3},2,
        *refusedWorld,route,result)==physics::PhysResult::InvalidState);
    CHECK(entity->spaceRegion()==1);
    CHECK_FLOAT_EQ(component->getVelocityZ(),0,1e-5f);
    CHECK(result.crossings==0);
    CHECK(transferSpatialRigidBodyAcrossSpaces(*entity,snapshot,{0,0,-3},2,
        *physicsWorld,route,result)==physics::PhysResult::Ok);
    CHECK(entity->spaceRegion()==2);CHECK(result.crossings==1);
    CHECK_FLOAT_EQ(component->getVelocityZ(),4,1e-4f);
    CHECK_FLOAT_EQ(entity->transform()->position.x,10,1e-4f);
    CHECK(manager->stepAndWait(1.f/60.f)==physics::PhysResult::Ok);
    bool captured=false;
    for(const auto& payload:physics::testaccess::mockBackendCreatePayloads()) {
        if(payload.kind!=physics::PhysicsCreatePayload::Kind::BodyTransfer3D) continue;
        captured=true;
        CHECK_FLOAT_EQ(payload.bodyTransfer3D.position.x,10,1e-4f);
        CHECK_FLOAT_EQ(payload.bodyTransfer3D.linearVelocity.z,4,1e-4f);
        CHECK_FLOAT_EQ(payload.bodyTransfer3D.angularVelocity.x,-1,1e-4f);
        CHECK_FLOAT_EQ(payload.bodyTransfer3D.angularVelocity.y,2,1e-4f);
        CHECK_FLOAT_EQ(payload.bodyTransfer3D.angularVelocity.z,-3,1e-4f);
        CHECK(payload.bodyTransfer3D.layer==2);
        CHECK(payload.bodyTransfer3D.collideMask==(1u<<2));
    }
    CHECK(captured);
    manager->shutdown();world.shutdown();
}
TEST_SUITE_END
int main(int argc,char** argv){return ayt::test::runTests("AYEntity_ConnectedSpacePhysicsTests",argc,argv);}
