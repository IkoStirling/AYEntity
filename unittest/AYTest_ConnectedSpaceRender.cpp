#include <AYEntity/ConnectedSpaceRender.h>
#include <AYEntity/World.h>
#include <AYMath/MathUtils.h>
#include <AYTest.h>
#include <AYRenderer.h>

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
    CHECK(graph.addRegion(3));
    SpacePortal second; second.id=10; second.source=2; second.destination=3;
    second.entrance.position={10,0,0}; second.entrance.rotation={0,1,0,0};
    second.exit.position={20,0,0};
    CHECK(graph.setPortal(second));
    SpacePortalView nested;
    CHECK(planSpacePortalView(world,10,plan.view,9,{46},nested));
    CHECK(nested.view.spaceRegionId==3);
    CHECK_FLOAT_EQ(nested.view.position.x,19.5f,1e-4f);
    source.position={100,0,2};
    CHECK_FALSE(planSpacePortalView(world,9,source,8,{45},plan));
    source.position={.5f,0,2};
    source.spaceRegionId=2;
    CHECK_FALSE(planSpacePortalView(world,9,source,8,{45},plan));
    CHECK(plan.view.id==8);
    world.shutdown();
}
TEST_CASE(visible_portals_schedule_recursive_child_views_before_parent) {
    World& world=World::instance(); CHECK(world.initialize());
    auto& graph=world.connectedSpaces();
    CHECK(graph.addRegion(1)); CHECK(graph.addRegion(2)); CHECK(graph.addRegion(3));
    SpacePortal first; first.id=9; first.source=1; first.destination=2;
    first.exit.position={10,0,0}; CHECK(graph.setPortal(first));
    SpacePortal second; second.id=10; second.source=2; second.destination=3;
    second.entrance.position={10,0,1}; second.entrance.rotation={0,1,0,0};
    second.exit.position={20,0,0}; CHECK(graph.setPortal(second));
    SpacePortal invisible=first; invisible.id=11; invisible.entrance.position={100,0,0};
    CHECK(graph.setPortal(invisible));
    ayt::render::Renderer renderer;
    ayt::render::InitDesc desc; desc.backend=ayt::render::Backend::Noop;
    desc.width=320;desc.height=240; CHECK(renderer.initialize(desc));
    ayt::render::RenderScene scene;
    ayt::render::CameraRenderView root; root.id=77;root.scene=&scene;
    root.spaceRegionId=1;root.width=320;root.height=240;
    root.position={0,0,2};
    root.view=math::lh::lookAt(root.position,{0,0,0},{0,1,0});
    root.projection=math::lh::perspective(math::radians(60.f),4.f/3.f,.1f,100.f);
    SpacePortalViewScheduler scheduler(renderer);
    CHECK(scheduler.build(world,root));
    auto views=scheduler.views();
    CHECK(views.size()==3);
    ayt::render::CameraOutputHandle deepest{},middle{};
    if (views.size()==3) {
        CHECK(views[0].spaceRegionId==3);
        CHECK(views[1].spaceRegionId==2);
        CHECK(views[2].spaceRegionId==1);
        CHECK(views[2].id==77);
        CHECK(views[0].scene->items().empty());
        CHECK(views[1].scene->items().size()==1);
        CHECK(views[2].scene->items().size()==1);
        CHECK(views[1].output.id && views[0].output.id);
        CHECK(views[1].output.id!=views[0].output.id);
        deepest=views[0].output;middle=views[1].output;
    }
    renderer.beginFrame(); CHECK(renderer.renderViews(views)); renderer.endFrame();
    SpacePortalScheduleOptions options;options.maxViews=2;
    CHECK(scheduler.build(world,root,options));
    CHECK(scheduler.views().size()==2);
    CHECK(scheduler.views()[0].spaceRegionId==2);
    CHECK(scheduler.views()[0].output.id==middle.id);
    CHECK_FALSE(renderer.cameraOutputTexture(deepest).isValid());
    root.view=math::lh::lookAt(root.position,{0,0,4},{0,1,0});
    CHECK(scheduler.build(world,root));
    CHECK(scheduler.views().size()==1);
    CHECK_FALSE(renderer.cameraOutputTexture(middle).isValid());
    scheduler.clear();renderer.shutdown();world.shutdown();
}
TEST_SUITE_END
int main(int argc,char** argv){return ayt::test::runTests("AYEntity_ConnectedSpaceRenderTests",argc,argv);}
