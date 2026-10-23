#include <AYEntity/ConnectedSpaceRender.h>
#include <AYEntity/World.h>
#include <AYMath/MathUtils.h>
#include <AYTest.h>

using namespace ayt::entity;
namespace math = ayt::math;

TEST_SUITE(ConnectedSpaceRenderTests)
TEST_CASE(live_camera_mapping_clip_plane_and_surface_share_portal) {
    World& world=World::instance(); CHECK(world.initialize());
    auto& graph=world.connectedSpaces();
    CHECK(graph.addRegion(1)); CHECK(graph.addRegion(2));
    SpacePortal portal; portal.id=9; portal.source=1; portal.destination=2;
    portal.exit.position={10,0,0}; portal.halfWidth=2; portal.halfHeight=3;
    CHECK(graph.setPortal(portal));
    ayt::render::RenderScene scene;
    ayt::render::CameraRenderView source;
    source.scene=&scene; source.spaceRegionId=1;
    source.width=320; source.height=240;
    source.projection=math::lh::perspective(math::radians(60.f),4.f/3.f,.1f,100.f);
    source.position={0,0,2};
    source.view=math::lh::lookAt(source.position,{0,0,0},{0,1,0});
    SpacePortalView plan;
    CHECK(planSpacePortalView(world,9,source,7,{44},plan));
    CHECK(plan.sourceRegion==1); CHECK(plan.view.spaceRegionId==2);
    CHECK(plan.view.id==7); CHECK(plan.view.output.id==44);
    CHECK_FLOAT_EQ(plan.view.position.x,10,1e-4f);
    CHECK_FLOAT_EQ(plan.view.position.z,-2,1e-4f);
    CHECK_FLOAT_EQ(plan.entranceWorld(0,0),4,1e-4f);
    CHECK_FLOAT_EQ(plan.entranceWorld(1,1),6,1e-4f);
    auto clip=[&](const math::FVector3& point) {
        const auto v=plan.view.projection*plan.view.view*
            math::FVector4{point.x,point.y,point.z,1};
        return v.z/v.w;
    };
    CHECK(clip({10,0,-1})<0);
    CHECK(clip({10,0,0})<0);
    CHECK(clip({10,0,1})>0);
    ayt::render::DrawItem draw;
    CHECK(makeSpacePortalSurfaceDraw(plan,{1},{2},draw));
    CHECK(draw.spaceRegionId==1);
    CHECK(draw.mesh.id==1); CHECK(draw.material.id==2);
    SpacePortalView unchanged=plan;
    source.position={.5f,0,2};
    source.view=math::lh::lookAt(source.position,{0,0,0},{0,1,0});
    CHECK(planSpacePortalView(world,9,source,8,{45},plan));
    CHECK_FLOAT_EQ(plan.view.position.x,9.5f,1e-4f);
    CHECK(plan.view.view(0,2)!=unchanged.view.view(0,2));
    source.position={100,0,2};
    CHECK_FALSE(planSpacePortalView(world,9,source,8,{45},plan));
    source.position={.5f,0,2};
    source.spaceRegionId=2;
    CHECK_FALSE(planSpacePortalView(world,9,source,8,{45},plan));
    CHECK(plan.view.id==8);
    world.shutdown();
}
TEST_SUITE_END
int main(int argc,char** argv){return ayt::test::runTests("AYEntity_ConnectedSpaceRenderTests",argc,argv);}
