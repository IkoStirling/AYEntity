#include <AYEntity/ConnectedSpaces.h>
#include <AYEntity/ConnectedSpaceEntity.h>
#include <AYEntity.h>
#include <AYEntity/EntityModule.h>
#include <AYEntity/components/RigidBodyComponent.h>
#include <AYTest.h>
#include <limits>

using namespace ayt::entity;
namespace math = ayt::math;
namespace {
SpacePortal door() {
    SpacePortal p;
    p.id = 9; p.source = 1; p.destination = 2;
    p.exit.position = {10, 0, 0};
    return p;
}
ConnectedSpaces pair() {
    ConnectedSpaces spaces;
    spaces.addRegion(1); spaces.addRegion(2);
    spaces.setPortal(door());
    return spaces;
}
}

TEST_SUITE(ConnectedSpacesTests)
TEST_CASE(regions_and_portals_validate_before_mutation) {
    ConnectedSpaces spaces;
    CHECK_FALSE(spaces.addRegion(0));
    CHECK(spaces.addRegion(1)); CHECK(spaces.addRegion(2));
    CHECK_FALSE(spaces.addRegion(1));
    auto p = door();
    p.destination = 3; CHECK_FALSE(spaces.setPortal(p));
    p.destination = 2; p.halfWidth = 0; CHECK_FALSE(spaces.setPortal(p));
    p.halfWidth = 1;
    p.entrance.rotation = {0,0,0,0}; CHECK_FALSE(spaces.setPortal(p));
    p.entrance.rotation = {0,0,0,1}; CHECK(spaces.setPortal(p));
    CHECK(spaces.portal(9) != nullptr);
    CHECK(spaces.removeRegion(2)); CHECK(spaces.portal(9) == nullptr);
}
TEST_CASE(view_motion_and_query_share_one_mapping) {
    auto spaces = pair();
    SpacePose camera{{1,0,2},{0,0,0,1}};
    SpacePose mapped;
    CHECK(spaces.mapPose(9,camera,mapped));
    CHECK_FLOAT_EQ(mapped.position.x,9,1e-4f);
    CHECK_FLOAT_EQ(mapped.position.z,-2,1e-4f);
    math::FVector3 mappedVelocity;
    CHECK(spaces.mapVector(9,{0,0,-4},mappedVelocity));
    CHECK_FLOAT_EQ(mappedVelocity.z,4,1e-4f);

    SpaceTraversal travel;
    CHECK(spaces.travel({1,{{0,0,1},{0,0,0,1}},{0,0,-4}},
                        {0,0,-3},2,travel));
    CHECK(travel.state.region == 2); CHECK(travel.crossings == 1);
    CHECK_FLOAT_EQ(travel.state.pose.position.x,10,1e-4f);
    CHECK_FLOAT_EQ(travel.state.pose.position.z,2,1e-4f);
    CHECK_FLOAT_EQ(travel.state.velocity.z,4,1e-4f);

    uint32_t calls = 0;
    const auto trace = spaces.traceRay(1,{{0,0,1},{0,0,-1}},5,2,
        [&](SpaceRegionId region,const math::Ray& ray,float limit)
            -> std::optional<SpaceLocalHit> {
            ++calls;
            if(region==1) { CHECK_FLOAT_EQ(limit,1,1e-4f); return SpaceLocalHit{}; }
            CHECK(region==2); CHECK_FLOAT_EQ(ray.origin.x,10,1e-4f);
            CHECK_FLOAT_EQ(ray.dir.z,1,1e-4f);
            return SpaceLocalHit{true,1};
        });
    CHECK(calls==2); CHECK(trace.status==SpaceTraceStatus::Hit);
    CHECK(trace.region==2); CHECK(trace.crossings==1);
    CHECK_FLOAT_EQ(trace.distance,2,2e-4f);
}
TEST_CASE(aperture_and_hop_budget_are_explicit) {
    auto spaces=pair(); SpaceTraversal result;
    CHECK(spaces.travel({1,{{2,0,1},{0,0,0,1}},{}},{0,0,-3},1,result));
    CHECK(result.state.region==1); CHECK(result.crossings==0);
    CHECK_FLOAT_EQ(result.state.pose.position.z,-2,1e-4f);
    CHECK(spaces.travel({1,{{0,0,1},{0,0,0,1}},{}},{0,0,-3},0,result));
    CHECK(result.hopLimitReached); CHECK(result.state.region==1);
    CHECK_FLOAT_EQ(result.state.pose.position.z,0,1e-4f);
    const auto trace=spaces.traceRay(1,{{0,0,1},{0,0,-1}},5,0,
        [](SpaceRegionId,const math::Ray&,float){return SpaceLocalHit{};});
    CHECK(trace.status==SpaceTraceStatus::HopLimit);
    CHECK_FLOAT_EQ(trace.distance,1,1e-4f);
    const auto shortRay=spaces.traceRay(1,{{0,0,1},{0,0,-1}},1.00005f,2,
        [](SpaceRegionId,const math::Ray&,float){return SpaceLocalHit{};});
    CHECK(shortRay.status==SpaceTraceStatus::Miss);
    CHECK(shortRay.region==2);
    CHECK_FLOAT_EQ(shortRay.distance,1.00005f,1e-5f);
    const auto frontHit=spaces.traceRay(1,{{0,0,1},{0,0,-1}},5,2,
        [](SpaceRegionId,const math::Ray&,float){return SpaceLocalHit{true,.5f};});
    CHECK(frontHit.status==SpaceTraceStatus::Hit);
    CHECK(frontHit.region==1);
}
TEST_CASE(fixed_step_landing_on_portal_plane_crosses_on_next_step) {
    auto spaces=pair();
    SpaceTraversal first, second;
    CHECK(spaces.travel({1,{{0,0,.05f},{0,0,0,1}},{0,0,-3}},
                        {0,0,-.05f},2,first));
    CHECK(first.crossings==0);
    CHECK_FLOAT_EQ(first.state.pose.position.z,0,1e-6f);
    CHECK(spaces.travel(first.state,{0,0,-.05f},2,second));
    CHECK(second.crossings==1);
    CHECK(second.state.region==2);
    CHECK_FLOAT_EQ(second.state.pose.position.x,10,1e-4f);
    CHECK_FLOAT_EQ(second.state.pose.position.z,.05f,1e-4f);
}
TEST_CASE(invalid_and_failed_queries_do_not_masquerade_as_empty_space) {
    auto spaces=pair();
    SpaceTraversal unchanged; unchanged.state.region=42;
    SpaceMotion invalid{1,{{0,0,0},{0,0,0,1}}, {}};
    invalid.velocity.x=std::numeric_limits<float>::quiet_NaN();
    CHECK_FALSE(spaces.travel(invalid,{0,0,1},2,unchanged));
    CHECK(unchanged.state.region==42);
    const auto bad=spaces.traceRay(1,{{0,0,1},{0,0,-1}},5,2,
        [](SpaceRegionId,const math::Ray&,float)->std::optional<SpaceLocalHit>{return std::nullopt;});
    CHECK(bad.status==SpaceTraceStatus::QueryFailed);
    const auto tooFar=spaces.traceRay(1,{{0,0,1},{0,0,-1}},5,2,
        [](SpaceRegionId,const math::Ray&,float){return SpaceLocalHit{true,100};});
    CHECK(tooFar.status==SpaceTraceStatus::QueryFailed);
}
TEST_CASE(spatial_entity_move_updates_region_pose_and_velocity_together) {
    CHECK(registerEntityCoreComponents(ComponentRegistry::instance()));
    World& world=World::instance(); CHECK(world.initialize());
    world.connectedSpaces()=pair();
    auto* entity=world.createSpatialEntity();
    CHECK(entity!=nullptr); CHECK(entity->setSpaceRegion(1));
    entity->transform()->position={0,0,1};
    math::FVector3 velocity{0,0,-4}; SpaceTraversal result;
    CHECK(moveSpatialEntityAcrossSpaces(*entity,{0,0,-3},velocity,2,result));
    CHECK(entity->spaceRegion()==2);
    CHECK_FLOAT_EQ(entity->transform()->position.x,10,1e-4f);
    CHECK_FLOAT_EQ(entity->transform()->position.z,2,1e-4f);
    CHECK_FLOAT_EQ(velocity.z,4,1e-4f);
    world.shutdown();
}
TEST_SUITE_END
int main(int argc,char** argv){return ayt::test::runTests("AYEntity_ConnectedSpacesTests",argc,argv);}
