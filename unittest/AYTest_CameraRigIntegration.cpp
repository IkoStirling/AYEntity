#include "../examples/camera-rig/CameraRigExample.h"
#include <AYEntity/EntityModule.h>
#include <AYEntity/EntityPhysicsIntegrationModule.h>
#include <AYEntity/PerspectiveCameraSelection.h>
#include <AYEntity/SceneSerializer.h>
#include <AYPhysics/PhysicsManager.h>
#include <AYPhysics/PhysicsWorld3D.h>
#include <AYTest.h>
using namespace ayt::entity;
TEST_SUITE(CameraRigPhysics)
#if defined(AYPHYSICS_HAS_JOLT)
TEST_CASE(real_sphere_sweep_shortens_recovers_ignores_target_and_constrains_lens_blend) {
    CHECK(registerEntityCoreComponents(ComponentRegistry::instance()));
    auto& world = World::processWorld(); CHECK(world.initialize()); auto rig=examples::makeCharacterCameraRig(world);
    ayt::physics::PhysicsBackendDescriptor descriptor; descriptor.kind3D=ayt::physics::BackendKind::DefaultJolt;
    descriptor.kind2D=ayt::physics::BackendKind::Null; descriptor.jobWorkerCount=1;
    auto manager=ayt::physics::PhysicsManager::create(descriptor); CHECK(manager!=nullptr);
    auto* physics=manager->world3D();
    const auto box=[&](ayt::math::FVector3 p,ayt::math::FVector3 half) {
        ayt::physics::RigidbodyDesc body; body.type=ayt::physics::BodyType::Static; body.position=p;
        ayt::physics::BodyHandle handle; CHECK(physics->createRigidbody(body,handle)==ayt::physics::PhysResult::Ok);
        ayt::physics::ColliderDesc shape; shape.body=handle; shape.halfExtents=half;
        ayt::physics::ColliderHandle collider; CHECK(physics->createCollider(shape,collider)==ayt::physics::PhysResult::Ok); return handle;
    };
    CHECK(registerEntityPhysicsComponents(ComponentRegistry::instance()));
    const auto playerBody=box({0,1.5f,0},{.5f,1,.5f});
    auto* rigid=rig.character->addComponent<RigidBodyComponent>(); rigid->setStatic(true); rigid->setBodyHandle(playerBody);
    const auto wallBody=box({0,1.5f,-3},{2,2,.2f});
    CHECK(manager->stepAndWait(1.f/60)==ayt::physics::PhysResult::Ok);
    installCameraPhysicsCollision(world,manager.get()); auto& runtime=cameraRuntime(world);
    auto* control=rig.thirdPerson->getComponent<CameraControllerComponent>(); control->positionDamping=0; control->rotationDamping=0;
    runtime.onUpdate(.1f); CameraFrame frame; CHECK(evaluatePerspectiveCamera3D(world,1,1,frame));
    CHECK_FLOAT_EQ(frame.position.z,-2.55f,.02f); CHECK(frame.position.z<-2); // The target's enclosing body is ignored.
    rig.thirdPerson->getComponents<PerspectiveCameraComponent>().at(0)->localOffset={0,0,-2};
    CHECK(control->shake(1,.5f,.1f)); runtime.onUpdate(.1f);
    CHECK(evaluatePerspectiveCamera3D(world,1,1,frame)); CHECK(frame.position.z>=-2.6f);
    (void)runtime.present(frame,0,false,1,false);
    rig.select(examples::CameraShot::Normal); rig.normal->transform()->position={0,.5f,-8}; runtime.onUpdate(.1f);
    CHECK(evaluatePerspectiveCamera3D(world,1,1,frame)); (void)runtime.present(frame,0,false,1,false);
    rig.select(examples::CameraShot::ThirdPerson); runtime.onUpdate(.1f);
    CHECK(evaluatePerspectiveCamera3D(world,1,1,frame)); const auto blended=runtime.present(frame,0,false,1,false);
    CHECK(blended.position.z>=-2.6f);
    CHECK(physics->destroyRigidbody(wallBody)==ayt::physics::PhysResult::Ok); CHECK(manager->stepAndWait(1.f/60)==ayt::physics::PhysResult::Ok);
    rig.thirdPerson->getComponents<PerspectiveCameraComponent>().at(0)->localOffset={}; control->resetHistory();
    control->positionDamping=2; runtime.onUpdate(.5f); CHECK(rig.thirdPerson->worldMatrix()(2,3)<-3 && rig.thirdPerson->worldMatrix()(2,3)>-5);
    runtime.onUpdate(3); CHECK_FLOAT_EQ(rig.thirdPerson->worldMatrix()(2,3),-5,.02f);
    // An explicit host callback overrides the installed default adapter.
    runtime.setCollisionQuery([](const CameraCollisionProbe&) { return 1.f; }); runtime.onUpdate(.1f);
    CHECK_FLOAT_EQ(rig.thirdPerson->worldMatrix()(2,3),-.95f,.02f);
    runtime.setCollisionQuery({}); runtime.setDefaultCollisionQuery({}); manager->shutdown(); world.shutdown();
}
#endif

TEST_CASE(portable_project_fixture_restores_data_system_world_parent_and_stable_target) {
    CHECK(registerEntityCoreComponents(ComponentRegistry::instance()));
    auto& world=World::processWorld(); CHECK(world.initialize());
    CHECK(loadScene(world,AY_CAMERA_RIG_FIXTURE_PATH)); CHECK(world.getAllEntities().size()==9);
    auto* player=world.findEntity("Character"), *system=world.findEntity("CameraSystem"), *rig=world.findEntity("CameraRig");
    auto* camera=world.findEntity("ThirdPerson"); CHECK(player && system && rig && camera);
    CHECK(system->kind()==EntityKind::Data); CHECK(system->transform()==nullptr); CHECK(system->owner()==player);
    CHECK(rig->spatialParent()==player); CHECK(rig->owner()==system); CHECK(camera->spatialParent()==rig);
    auto* controller=camera->getComponent<CameraControllerComponent>(); CHECK(controller->resolveTarget()==player);
    player->setName("Fixture Renamed Player"); CHECK(controller->resolveTarget()==player);
    cameraRuntime(world).onUpdate(.1f); CameraFrame frame; CHECK(evaluatePerspectiveCamera3D(world,1,1,frame));
    CHECK(selectPerspectiveCamera3D(world).entity==camera);
    world.destroyEntity(player); CHECK(world.getAllEntities().size()==3); // Root visual references survive ownership cleanup.
    world.shutdown();
}

TEST_SUITE_END
int main(int argc,char** argv) { std::setvbuf(stdout,nullptr,_IONBF,0); std::setvbuf(stderr,nullptr,_IONBF,0); return ayt::test::runTests("CameraRigIntegration",argc,argv); }
