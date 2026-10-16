#include <AYEntity.h>
#include <AYEntity/EntityModule.h>
#include <AYEntity/CameraFrame.h>
#include <AYEntity/CameraRuntime.h>
#include <AYEntity/PerspectiveCameraSelection.h>
#include <AYEntity/components/CameraControllerComponent.h>
#include <AYEntity/components/PerspectiveCameraComponent.h>
#include <AYEntity/SceneSerializer.h>
#include <AYTest.h>
#include <cstdio>
#include <limits>

using namespace ayt::entity;
namespace {
struct CameraFixture {
    World& world = World::instance();
    CameraFixture() { CHECK(registerEntityCoreComponents(ComponentRegistry::instance())); CHECK(world.initialize()); }
    ~CameraFixture() { world.shutdown(); }
    Entity* make(const char* name) {
        auto* entity = world.createEntity(); entity->setName(name); entity->addComponent<Transform>(); return entity;
    }
};
CameraFrame lens(CameraProjection mode) {
    CameraFrame frame;
    frame.position = {3,4,-5}; frame.orientation = ayt::math::FQuaternion::fromEulerAngles({.1f,.3f,.2f});
    frame.projectionMode = mode; frame.aspect = 2; frame.verticalSize = 12;
    CHECK(rebuildCameraFrame(frame)); return frame;
}
TEST_SUITE(CameraFeatures)

TEST_CASE(pixel_depth_roundtrip_handles_viewport_origin_both_projections) {
    const CameraViewport viewport{70,30,800,400};
    for (auto mode : {CameraProjection::Perspective,CameraProjection::Orthographic}) {
        const auto frame = lens(mode);
        for (const auto pixel : {ayt::math::FVector3{70,30,.2f},ayt::math::FVector3{470,230,.5f},ayt::math::FVector3{870,430,.8f}}) {
            ayt::math::FVector3 world, result;
            CHECK(screenToWorld(frame,viewport,pixel,world)); CHECK(worldToScreen(frame,viewport,world,result));
            CHECK_FLOAT_EQ(result.x,pixel.x,.01f); CHECK_FLOAT_EQ(result.y,pixel.y,.01f); CHECK_FLOAT_EQ(result.z,pixel.z,.001f);
        }
    }
}
TEST_CASE(perspective_eye_and_orthographic_parallel_ray_origins) {
    auto frame = lens(CameraProjection::Perspective); CameraRay a,b;
    CHECK(screenPointToRay(frame,{0,0,800,400},{0,200},a));
    CHECK(screenPointToRay(frame,{0,0,800,400},{800,200},b));
    CHECK_FLOAT_EQ(a.origin.x,frame.position.x,1e-5f); CHECK((a.direction-b.direction).lengthSq() > .1f);
    frame = lens(CameraProjection::Orthographic);
    CHECK(screenPointToRay(frame,{0,0,800,400},{0,200},a));
    CHECK(screenPointToRay(frame,{0,0,800,400},{800,200},b));
    CHECK((a.origin-b.origin).lengthSq() > 100); CHECK((a.direction-b.direction).lengthSq() < 1e-5f);
}
TEST_CASE(perspective_queries_match_known_frustum_and_support_long_far_planes) {
    CameraFrame frame; frame.fovYDegrees=90; frame.aspect=2; CHECK(rebuildCameraFrame(frame));
    ayt::math::FVector3 pixel;
    CHECK(worldToScreen(frame,{0,0,800,400},{2,0,1},pixel));
    CHECK_FLOAT_EQ(pixel.x,800,1e-3f); CHECK_FLOAT_EQ(pixel.y,200,1e-3f);
    CameraRay ray; CHECK(screenPointToRay(frame,{0,0,800,400},{800,200},ray));
    CHECK_FLOAT_EQ(ray.direction.x,2/std::sqrt(5.f),1e-4f); CHECK_FLOAT_EQ(ray.direction.z,1/std::sqrt(5.f),1e-4f);
    frame.farZ=1e8f; CHECK(rebuildCameraFrame(frame));
    CHECK(screenPointToRay(frame,{0,0,800,400},{400,200},ray)); CHECK_FLOAT_EQ(ray.direction.z,1,1e-5f);
}
TEST_CASE(conversions_reject_singular_nonfinite_and_invalid_viewport_without_writes) {
    auto frame = lens(CameraProjection::Perspective); ayt::math::FVector3 result{9,8,7};
    CHECK_FALSE(screenToWorld(frame,{0,0,0,100},{0,0,.5f},result)); CHECK_FLOAT_EQ(result.x,9,0);
    CHECK_FALSE(screenToWorld(frame,{}, {0,0,2},result));
    frame.projection = ayt::math::scale(0,0,0);
    CHECK_FALSE(screenToWorld(frame,{}, {.5f,.5f,.5f},result)); CHECK_FLOAT_EQ(result.z,7,0);
    frame = lens(CameraProjection::Perspective);
    const auto behind = frame.position - frame.orientation * ayt::math::FVector3{0,0,2};
    CHECK_FALSE(worldToScreen(frame,{},behind,result));
    frame.view.row[0].x=std::numeric_limits<float>::quiet_NaN();
    CHECK_FALSE(worldToScreen(frame,{}, {0,0,1},result));
    CHECK_FALSE(screenToWorld(frame,{}, {.5f,.5f,.5f},result)); CHECK_FLOAT_EQ(result.z,7,0);
}
TEST_CASE(local_pose_orthographic_lens_and_viewport_selection) {
    CameraFixture fixture; auto* entity = fixture.make("Main");
    auto* transform = entity->getComponent<Transform>();
    transform->position = {1,2,3}; transform->rotation = ayt::math::FQuaternion::fromAxisAngle({0,1,0},ayt::math::radians(90.f));
    auto* camera = entity->createComponent<PerspectiveCameraComponent>(); camera->localOffset = {0,0,2};
    camera->projectionMode = 1; camera->orthographicHeight = 20; camera->layerMask = 4;
    auto* second = entity->createComponent<PerspectiveCameraComponent>(); second->viewportId = 1; second->priority = 100;
    CHECK(selectPerspectiveCamera3D(fixture.world).camera == camera);
    CHECK(selectPerspectiveCamera3D(fixture.world,1).camera == second);
    CameraFrame frame; CHECK(evaluatePerspectiveCamera3D(fixture.world,2,1,frame));
    CHECK_FLOAT_EQ(frame.position.x,3,1e-4f); CHECK_FLOAT_EQ(frame.position.z,3,1e-4f);
    CHECK(frame.projectionMode == CameraProjection::Orthographic); CHECK(frame.layerMask == 4);
    CHECK_FLOAT_EQ(frame.projection(1,1),.1f,1e-5f);
}
TEST_CASE(teleport_discards_sim_interpolation_and_marks_render_cut) {
    CameraFixture fixture; auto* entity = fixture.make("Teleport");
    auto* transform = entity->getComponent<Transform>(); auto* camera = entity->createComponent<PerspectiveCameraComponent>();
    transform->applySimulationPose({0,0,0},{0,0,0,1}); transform->applySimulationPose({10,0,0},{0,0,0,1});
    camera->teleport(*transform,{100,0,0},{0,0,0,1});
    CameraFrame frame; CHECK(evaluatePerspectiveCamera3D(fixture.world,1,0,frame));
    CHECK_FLOAT_EQ(frame.position.x,100,0); CHECK(frame.cutGeneration == 1); CHECK_FALSE(transform->hasPreviousSimulationPose);
}
TEST_CASE(blends_interruption_cut_and_projection_change_have_explicit_behavior) {
    CameraTransition transition; auto a = lens(CameraProjection::Perspective); a.position={0,0,0}; a.identity=1; CHECK(rebuildCameraFrame(a));
    auto b=a; b.identity=2; b.position={10,0,0}; CHECK(rebuildCameraFrame(b));
    CHECK_FLOAT_EQ(transition.update(a,0,1,false).position.x,0,0);
    CHECK_FLOAT_EQ(transition.update(b,.25f,1,false).position.x,2.5f,1e-5f);
    CHECK_FLOAT_EQ(transition.update(b,.25f,1,false).position.x,5,1e-5f);
    auto c=b; c.identity=3; c.position={20,0,0}; CHECK(rebuildCameraFrame(c));
    CHECK_FLOAT_EQ(transition.update(c,0,1,false).position.x,5,1e-5f);
    CHECK_FLOAT_EQ(transition.update(c,.5f,1,false).position.x,12.5f,1e-5f);
    ++c.cutGeneration; CHECK_FLOAT_EQ(transition.update(c,0,1,false).position.x,20,0);
    c.identity=4; c.projectionMode=CameraProjection::Orthographic; c.position.x=30; CHECK(rebuildCameraFrame(c));
    CHECK_FLOAT_EQ(transition.update(c,0,1,false).position.x,30,0);
}
TEST_CASE(world_runtime_advances_transitions_once_per_tick_and_per_viewport) {
    CameraFixture fixture; auto& runtime = cameraRuntime(fixture.world);
    auto a=lens(CameraProjection::Perspective); a.identity=1; a.position.x=0; CHECK(rebuildCameraFrame(a));
    auto b=a; b.identity=2; b.position.x=10; CHECK(rebuildCameraFrame(b));
    CameraFrame snapshot; snapshot.identity=99;
    CHECK_FALSE(runtime.snapshot(0,false,snapshot)); CHECK(snapshot.identity==99);
    (void)runtime.present(a,0,false,1,false); (void)runtime.present(a,1,false,1,false);
    fixture.world.updatePresentation(.25f,1);
    CHECK_FLOAT_EQ(runtime.present(b,0,false,1,false).position.x,2.5f,1e-5f);
    CHECK(runtime.snapshot(0,false,snapshot)); CHECK_FLOAT_EQ(snapshot.position.x,2.5f,1e-5f);
    CHECK_FLOAT_EQ(runtime.present(b,0,false,1,false).position.x,2.5f,1e-5f);
    CHECK_FLOAT_EQ(runtime.present(a,1,false,1,false).position.x,0,0);
    runtime.resetView(0,false); CHECK_FLOAT_EQ(runtime.present(b,0,false,1,false).position.x,10,0);
}
TEST_CASE(follow_damping_is_frame_rate_independent_for_stationary_target) {
    CameraFixture fixture; auto* target=fixture.make("Target"); target->getComponent<Transform>()->position={10,0,0};
    auto* entity=fixture.make("Follow"); auto* c=entity->addComponent<CameraControllerComponent>();
    c->mode=1; c->twoD=true; c->targetName="Target"; c->offset={}; c->positionDamping=2;
    auto& runtime=cameraRuntime(fixture.world); runtime.onUpdate(.5f);
    const float oneStep=entity->getComponent<Transform>()->position.x;
    entity->getComponent<Transform>()->setPresentationPose({0,0,0},{0,0,0,1}); c->resetHistory();
    runtime.onUpdate(.25f); runtime.onUpdate(.25f);
    CHECK_FLOAT_EQ(entity->getComponent<Transform>()->position.x,oneStep,1e-4f);
    CHECK_FLOAT_EQ(oneStep,10 * (1-std::exp(-1.f)),1e-4f);
}
TEST_CASE(dead_zone_lookahead_bounds_and_missing_target) {
    CameraFixture fixture; auto* target=fixture.make("Target"); target->getComponent<Transform>()->position={1,1,0};
    auto* entity=fixture.make("Follow"); auto* t=entity->getComponent<Transform>(); auto* c=entity->addComponent<CameraControllerComponent>();
    c->mode=1; c->twoD=true; c->targetName="Target"; c->offset={}; c->positionDamping=0; c->deadZone={2,2};
    auto& runtime=cameraRuntime(fixture.world); runtime.onUpdate(1); CHECK_FLOAT_EQ(t->position.x,0,0);
    c->lookAheadSeconds=1; c->boundsEnabled=true; c->boundsMin={-5,-5,0}; c->boundsMax={5,5,0};
    target->getComponent<Transform>()->position={10,0,0}; runtime.onUpdate(1); CHECK_FLOAT_EQ(t->position.x,5,0);
    fixture.world.destroyEntity(target); runtime.onUpdate(1); CHECK_FLOAT_EQ(t->position.x,5,0); CHECK_FALSE(c->hasPreviousTarget);
}
TEST_CASE(follow_samples_fixed_step_target_at_presentation_alpha) {
    CameraFixture fixture; auto* target=fixture.make("InterpolatedTarget");
    auto* targetTransform=target->getComponent<Transform>();
    targetTransform->applySimulationPose({0,0,0},{0,0,0,1}); targetTransform->applySimulationPose({10,0,0},{0,0,0,1});
    auto* entity=fixture.make("Follow"); auto* c=entity->addComponent<CameraControllerComponent>();
    c->mode=1; c->twoD=true; c->targetName="InterpolatedTarget"; c->offset={}; c->positionDamping=0;
    (void)cameraRuntime(fixture.world); fixture.world.updatePresentation(.1f,.5f);
    CHECK_FLOAT_EQ(entity->getComponent<Transform>()->position.x,5,1e-5f);
    fixture.world.updatePresentation(.1f,std::numeric_limits<float>::quiet_NaN());
    CHECK_FLOAT_EQ(entity->getComponent<Transform>()->position.x,0,1e-5f);
}
TEST_CASE(orbit_look_at_and_collision_sweep_keep_camera_outside_obstruction) {
    CameraFixture fixture; fixture.make("Target"); auto* entity=fixture.make("Orbit");
    auto* c=entity->addComponent<CameraControllerComponent>(); c->mode=2; c->targetName="Target"; c->offset={};
    c->positionDamping=0; c->rotationDamping=0; c->distance=5; c->collisionPadding=.1f;
    auto& runtime=cameraRuntime(fixture.world); int calls=0;
    runtime.setCollisionQuery([&](const CameraCollisionProbe& probe) { ++calls; CHECK(probe.cameraEntity==entity); CHECK_FLOAT_EQ(probe.radius,.2f,1e-5f); return 2.f; });
    runtime.onUpdate(1); auto* t=entity->getComponent<Transform>();
    CHECK(calls==1); CHECK_FLOAT_EQ(t->position.z,-1.9f,1e-4f);
    CHECK_FLOAT_EQ((t->rotation * ayt::math::FVector3{0,0,1}).z,1,1e-5f);
    runtime.setCollisionQuery({}); c->yawRadians=ayt::math::radians(90.f); runtime.onUpdate(1);
    CHECK_FLOAT_EQ(t->position.x,-5,1e-4f);
    CHECK_FLOAT_EQ((t->rotation * ayt::math::FVector3{0,0,1}).x,1,1e-4f);
}
TEST_CASE(free_input_is_local_normalized_and_shake_never_changes_entity_pose) {
    CameraFixture fixture; auto* entity=fixture.make("Free"); auto* c=entity->addComponent<CameraControllerComponent>();
    auto* camera=entity->createComponent<PerspectiveCameraComponent>(); (void)camera;
    c->mode=3; c->positionDamping=0; c->rotationDamping=0; c->moveSpeed=4; c->movementInput={0,0,1}; c->yawRadians=ayt::math::radians(90.f);
    auto& runtime=cameraRuntime(fixture.world); runtime.onUpdate(.5f);
    CHECK_FLOAT_EQ(entity->getComponent<Transform>()->position.x,2,1e-4f);
    c->mode=0; CHECK(c->shake(1,1,.1f,3,7)); runtime.onUpdate(.2f);
    CameraFrame frame; CHECK(evaluatePerspectiveCamera3D(fixture.world,1,1,frame));
    CHECK((frame.position-entity->getComponent<Transform>()->position).lengthSq()>.001f);
    CHECK_FLOAT_EQ(entity->getComponent<Transform>()->position.x,2,1e-4f);
    runtime.onUpdate(1); CHECK(evaluatePerspectiveCamera3D(fixture.world,1,1,frame));
    CHECK((frame.position-entity->getComponent<Transform>()->position).lengthSq()<1e-8f);
}
TEST_CASE(look_at_uses_final_bounded_eye_and_normalizes_authored_pose) {
    CameraFixture fixture; auto* target=fixture.make("Target"); target->getComponent<Transform>()->position={10,0,0};
    auto* entity=fixture.make("Look"); auto* pose=entity->getComponent<Transform>(); pose->rotation={0,0,0,2};
    auto* c=entity->addComponent<CameraControllerComponent>(); c->mode=1; c->targetName="Target";
    c->positionDamping=0; c->rotationDamping=0; c->boundsEnabled=true; c->boundsMin={-100,-100,-10}; c->boundsMax={5,100,10};
    cameraRuntime(fixture.world).onUpdate(1);
    const auto direction=pose->rotation * ayt::math::FVector3{0,0,1};
    CHECK_FLOAT_EQ(pose->position.x,5,0); CHECK_FLOAT_EQ(pose->position.z,-5,0);
    CHECK_FLOAT_EQ(direction.x,1/std::sqrt(2.f),1e-4f); CHECK_FLOAT_EQ(direction.z,1/std::sqrt(2.f),1e-4f);
    CHECK_FLOAT_EQ(pose->rotation.dot(pose->rotation),1,1e-4f);
}
TEST_CASE(local_lens_offset_and_shake_cannot_bypass_camera_collision) {
    CameraFixture fixture; fixture.make("Target"); auto* entity=fixture.make("CollisionLens");
    auto* camera=entity->createComponent<PerspectiveCameraComponent>(); camera->localOffset={2,0,0};
    auto* c=entity->addComponent<CameraControllerComponent>(); c->mode=1; c->targetName="Target";
    c->positionDamping=0; c->rotationDamping=0; CHECK(c->shake(1,1,.1f,3,7));
    auto& runtime=cameraRuntime(fixture.world); runtime.setCollisionQuery([](const CameraCollisionProbe&) { return 2.f; });
    runtime.onUpdate(.2f); CameraFrame frame; CHECK(evaluatePerspectiveCamera3D(fixture.world,1,1,frame));
    CHECK(frame.position.length()<=1.951f);
    CHECK_FLOAT_EQ(entity->getComponent<Transform>()->position.z,-1.95f,1e-4f);
}
TEST_CASE(scene_roundtrip_persists_controller_and_new_lens_fields_but_not_effect_state) {
    CameraFixture fixture; auto* entity=fixture.make("PersistCamera"); auto* camera=entity->createComponent<PerspectiveCameraComponent>();
    camera->projectionMode=1; camera->orthographicHeight=30; camera->localOffset={1,2,3}; camera->viewportId=2; camera->blendDuration=.7f; camera->layerMask=8;
    auto* c=entity->addComponent<CameraControllerComponent>(); c->mode=2; c->targetName="Player"; c->distance=12; CHECK(c->shake(1,1,.1f));
    auto& runtime=cameraRuntime(fixture.world); CameraFrame frame;
    CHECK(evaluatePerspectiveCamera3D(fixture.world,1,1,frame,2)); (void)runtime.present(frame,2,false,.7f);
    const char* path="camera_features_roundtrip.scn"; CHECK(saveScene(fixture.world,path)); CHECK(loadScene(fixture.world,path));
    entity=fixture.world.findEntity("PersistCamera"); CHECK(entity!=nullptr);
    CHECK_FALSE(runtime.snapshot(2,false,frame));
    if(entity) {
        camera=entity->getComponents<PerspectiveCameraComponent>()[0]; c=entity->getComponent<CameraControllerComponent>();
        CHECK(camera->projectionMode==1); CHECK(camera->viewportId==2); CHECK(camera->layerMask==8);
        CHECK_FLOAT_EQ(camera->localOffset.z,3,0); CHECK_FLOAT_EQ(camera->blendDuration,.7f,1e-5f);
        CHECK(c && c->targetName=="Player"); if(c) { CHECK_FLOAT_EQ(c->distance,12,0); CHECK_FLOAT_EQ(c->shakeDuration,0,0); }
    }
    std::remove(path);
}
TEST_CASE(debug_collision_snapshot_copies_real_probe_without_extra_sweeps) {
    CameraFixture fixture;fixture.make("Target");auto* entity=fixture.make("DebugOrbit");auto* controller=entity->addComponent<CameraControllerComponent>();
    controller->mode=2;controller->targetName="Target";controller->offset={};controller->positionDamping=0;controller->distance=5;
    auto& runtime=cameraRuntime(fixture.world);int calls=0;runtime.setCollisionQuery([&](const CameraCollisionProbe&){++calls;return 2.f;});
    runtime.setDebugCaptureEnabled(true);runtime.onUpdate(1);auto snapshot=runtime.debugSnapshot();CHECK(calls==1);CHECK(snapshot.collisions.size()==1);
    (void)runtime.debugSnapshot();CHECK(calls==1);runtime.clearDebugHistory();CHECK(runtime.debugSnapshot().collisions.empty());CHECK(snapshot.collisions.size()==1);
}
TEST_SUITE_END
}
