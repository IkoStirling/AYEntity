#include <AYEntity.h>
#include <AYEntity/EntityModule.h>
#include <AYEntity/PerspectiveCameraSelection.h>
#include <AYEntity/SceneSerializer.h>
#include <AYEntity/components/PerspectiveCameraComponent.h>
#include <AYTest.h>
#include <cstdio>
#include <fstream>
#include <limits>

using namespace ayt::entity;
namespace {
struct CameraWorld {
    World& world = World::instance();
    CameraWorld() { CHECK(registerEntityCoreComponents(ComponentRegistry::instance())); world.initialize(); }
    ~CameraWorld() { world.shutdown(); }
    Entity* camera(float z = 0) {
        auto* e = world.createEntity();
        e->addComponent<Transform>()->position = {0, 0, z};
        CHECK(e->createComponent<PerspectiveCameraComponent>() != nullptr);
        return e;
    }
};
}
TEST_SUITE(PerspectiveCamera3DTests)

TEST_CASE(selection_is_active_priority_then_entity_and_persistent_component_id)
{
    CameraWorld f;
    auto* first = f.camera();
    auto* a = first->getComponents<PerspectiveCameraComponent>()[0];
    CHECK(first->setComponentInstanceId(a, "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"));
    auto* b = first->createComponent<PerspectiveCameraComponent>();
    CHECK(first->setComponentInstanceId(b, "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"));
    auto* second = f.camera();
    auto* c = second->getComponents<PerspectiveCameraComponent>()[0];
    CHECK(selectPerspectiveCamera3D(f.world).camera == b);
    c->priority = 10;
    CHECK(selectPerspectiveCamera3D(f.world).camera == c);
    c->active = false;
    CHECK(selectPerspectiveCamera3D(f.world).camera == b);
    f.world.destroyEntity(first);
    CHECK_FALSE(static_cast<bool>(selectPerspectiveCamera3D(f.world)));
}

TEST_CASE(camera_pose_is_transform_driven_lh_and_ignores_scale)
{
    CameraWorld f;
    auto* entity = f.camera(4);
    auto* transform = entity->getComponent<Transform>();
    transform->scale = {-10, 0.1f, 7};
    transform->rotation = ayt::math::FQuaternion::fromAxisAngle({0, 1, 0},
        ayt::math::radians(90.0f));
    PerspectiveCameraFrame frame;
    CHECK(evaluatePerspectiveCamera3D(f.world, 2.0f, 1, frame));
    CHECK_FLOAT_EQ(frame.position.z, 4, 0.0001f);
    // Camera forward follows +X after a quarter turn around Y.
    CHECK_FLOAT_EQ(frame.view(2, 0), 1, 0.0001f);
    CHECK_FLOAT_EQ(frame.view(0, 3), 4, 0.0001f);
    CHECK_FLOAT_EQ(frame.projection(1, 1) / frame.projection(0, 0), 2, 0.0001f);
    CHECK(frame.identity != 0);
}

TEST_CASE(interpolation_and_explicit_camera_cut_are_runtime_only)
{
    CameraWorld f;
    auto* entity = f.camera(10);
    auto* transform = entity->getComponent<Transform>();
    transform->previousPosition = {0, 0, 2};
    transform->previousRotation = transform->rotation;
    transform->hasPreviousSimulationPose = true;
    PerspectiveCameraFrame frame;
    CHECK(evaluatePerspectiveCamera3D(f.world, 1, 0.25f, frame));
    CHECK_FLOAT_EQ(frame.position.z, 4, 0.0001f);
    const auto identity = frame.identity;
    entity->getComponents<PerspectiveCameraComponent>()[0]->requestCameraCut();
    CHECK(evaluatePerspectiveCamera3D(f.world, 1, 1, frame));
    CHECK(frame.identity == identity);
    CHECK(frame.cutGeneration == 1);
}

TEST_CASE(invalid_pose_is_skipped_and_invalid_lens_parameters_are_sanitized)
{
    CameraWorld f;
    auto* bad = f.camera();
    bad->getComponent<Transform>()->rotation = {0, 0, 0, 0};
    auto* good = f.camera(3);
    auto* camera = good->getComponents<PerspectiveCameraComponent>()[0];
    camera->fovYDegrees = std::numeric_limits<float>::quiet_NaN();
    camera->nearZ = -2;
    camera->farZ = 0;
    CHECK(selectPerspectiveCamera3D(f.world).entity == good);
    PerspectiveCameraFrame frame;
    CHECK(evaluatePerspectiveCamera3D(f.world, 0, 1, frame));
    for (int i = 0; i < 16; ++i) CHECK(std::isfinite(frame.projection.ptr()[i]));
    camera->active = false;
    frame.identity = 123;
    CHECK_FALSE(evaluatePerspectiveCamera3D(f.world, 1, 1, frame));
    CHECK(frame.identity == 123);
}

TEST_CASE(scene_roundtrip_preserves_multiple_camera_parameters_selection_and_pose)
{
    CameraWorld f;
    auto* entity = f.camera(7);
    entity->setName("Camera3D");
    auto* camera = entity->getComponents<PerspectiveCameraComponent>()[0];
    camera->fovYDegrees = 70;
    camera->nearZ = 0.3f;
    camera->farZ = 300;
    camera->priority = 9;
    camera->requestCameraCut();
    const auto id = entity->componentInstance(camera)->id;
    entity->createComponent<PerspectiveCameraComponent>()->active = false;
    const char* path = "perspective_camera.scn";
    CHECK(saveScene(f.world, path));
    CHECK(loadScene(f.world, path));
    entity = f.world.findEntity("Camera3D");
    CHECK(entity != nullptr);
    if (entity) {
        CHECK(entity->getComponents<PerspectiveCameraComponent>().size() == 2);
        const auto selected = selectPerspectiveCamera3D(f.world);
        CHECK(selected.camera != nullptr);
        if (selected) {
            CHECK(entity->componentInstance(selected.camera)->id == id);
            CHECK_FLOAT_EQ(selected.camera->fovYDegrees, 70, 0.0001f);
            CHECK_FLOAT_EQ(selected.camera->nearZ, 0.3f, 0.0001f);
            CHECK_FLOAT_EQ(selected.camera->farZ, 300, 0.0001f);
            CHECK_FLOAT_EQ(selected.transform->position.z, 7, 0.0001f);
            CHECK(selected.camera->cutGeneration == 0);
        }
    }
    std::remove(path);
}

TEST_CASE(load_synthesizes_missing_transform_but_keeps_later_authored_transform)
{
    CameraWorld f;
    const char* path = "perspective_camera_transform.scn";
    std::ofstream(path) << R"({"__schemaVersion":3,"entities":[
        {"name":"Missing","components":[{"$type":"PerspectiveCameraComponent"}]},
        {"name":"Explicit","components":[{"$type":"PerspectiveCameraComponent"},
         {"$type":"Transform","position":[3,4,5]}]}]})";
    const bool loaded = loadScene(f.world, path);
    CHECK(loaded);
    if (loaded) {
        CHECK(f.world.findEntity("Missing")->getComponent<Transform>() != nullptr);
        CHECK_FLOAT_EQ(f.world.findEntity("Explicit")->getComponent<Transform>()->position.z, 5, 0.0001f);
    }
    std::remove(path);
}
TEST_SUITE_END
