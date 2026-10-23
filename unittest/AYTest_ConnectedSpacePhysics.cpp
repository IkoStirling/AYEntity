#include <AYEntity/ConnectedSpacePhysicsQuery.h>
#include <AYEntity/EntityModule.h>
#include <AYEntity/World.h>
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
TEST_SUITE_END
int main(int argc,char** argv){return ayt::test::runTests("AYEntity_ConnectedSpacePhysicsTests",argc,argv);}
