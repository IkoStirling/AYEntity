#include <AYEntity.h>
#include <AYEntity/EntityModule.h>
#include <AYEntity/ComponentFactory.h>
#include <AYSerializer/SerializeError.h>
#include <AYEntity/SceneSerializer.h>
#include <AYEntity/components/PerspectiveCameraComponent.h>
#include <AYEntity/PerspectiveCameraSelection.h>
#include <AYMath/MathTransform.h>
#include <AYTest.h>
#include <fstream>
#include <cstdio>
#include <limits>

using namespace ayt::entity;
namespace {
struct Fixture {
    World& world = World::instance();
    Fixture() { CHECK(registerEntityCoreComponents(ComponentRegistry::instance())); world.initialize(); }
    ~Fixture() { world.shutdown(); }
};
}
TEST_SUITE(SpatialEntityTests)
TEST_CASE(explicit_kinds_have_intrinsic_pose_and_checked_conversion) {
    Fixture f;
    auto* data = f.world.createDataEntity();
    auto* space = f.world.createSpatialEntity();
    CHECK(data->kind() == EntityKind::Data); CHECK(data->transform() == nullptr);
    CHECK(space->isSpatial()); CHECK(space->transform() != nullptr);
    CHECK(data->addComponent<Transform>() == nullptr);
    CHECK(data->createComponent<PerspectiveCameraComponent>() == nullptr);
    auto* pose = space->transform();
    CHECK(space->addComponent<Transform>() == pose);
    CHECK(space->createComponent<Transform>() == nullptr);
    space->removeComponent<Transform>(); CHECK(space->transform() == pose);
    CHECK_FALSE(ComponentFactory::removeComponent(*space,"Transform"));
    CHECK_FALSE(space->removeComponentById(space->componentInstance(pose)->id));
    CHECK(space->createComponent<PerspectiveCameraComponent>() != nullptr);
    CHECK_FALSE(space->convertKind(EntityKind::Data));
    const auto cameraId = space->getComponents<PerspectiveCameraComponent>()[0];
    CHECK(space->removeComponentById(space->componentInstance(cameraId)->id));
    CHECK(space->convertKind(EntityKind::Data)); CHECK(space->transform() == nullptr);
    CHECK(data->convertKind(EntityKind::Spatial)); CHECK(data->transform() != nullptr);
}
TEST_CASE(multilevel_composition_reparent_and_cycle_refusal) {
    Fixture f;
    auto* a = f.world.createSpatialEntity(); auto* b = f.world.createSpatialEntity(); auto* c = f.world.createSpatialEntity();
    a->transform()->position = {10,0,0}; b->transform()->position = {0,2,0}; c->transform()->position = {0,0,3};
    CHECK(b->setSpatialParent(a)); CHECK(c->setSpatialParent(b));
    auto m = c->worldMatrix();
    CHECK_FLOAT_EQ(m(0,3),10,1e-5f); CHECK_FLOAT_EQ(m(1,3),2,1e-5f); CHECK_FLOAT_EQ(m(2,3),3,1e-5f);
    CHECK_FALSE(a->setSpatialParent(c)); CHECK(a->spatialParent() == nullptr);
    CHECK_FALSE(c->setSpatialParent(f.world.createDataEntity()));
    CHECK(c->setSpatialParent(a,ReparentMode::KeepWorld));
    CHECK_FLOAT_EQ(c->transform()->position.y,2,1e-5f);
    a->transform()->position.x = 20;
    CHECK_FLOAT_EQ(c->worldMatrix()(0,3),20,1e-5f);
    CHECK(c->setSpatialParent(nullptr,ReparentMode::KeepWorld));
    CHECK_FLOAT_EQ(c->transform()->position.x,20,1e-5f);
}
TEST_CASE(singular_and_shear_reparent_fail_without_mutation) {
    Fixture f;
    auto* a = f.world.createSpatialEntity(); auto* b = f.world.createSpatialEntity();
    a->transform()->scale = {0,1,1}; b->transform()->position = {2,3,4};
    CHECK_FALSE(b->setSpatialParent(a,ReparentMode::KeepWorld));
    CHECK(b->spatialParent() == nullptr); CHECK_FLOAT_EQ(b->transform()->position.x,2,1e-5f);
    a->transform()->scale = {2,1,1};
    b->transform()->rotation = ayt::math::FQuaternion::fromEulerAngles({0,0,.7f});
    CHECK_FALSE(b->setSpatialParent(a,ReparentMode::KeepWorld));
    CHECK(b->spatialParent() == nullptr);
}
TEST_CASE(ownership_and_attachment_have_independent_lifetimes) {
    Fixture f;
    auto* parent = f.world.createSpatialEntity(); auto* attached = f.world.createSpatialEntity(); auto* owned = f.world.createDataEntity();
    parent->transform()->position = {8,0,0}; attached->transform()->position = {2,0,0};
    CHECK(attached->setSpatialParent(parent)); CHECK(owned->setOwner(parent));
    CHECK_FALSE(parent->setOwner(owned));
    const auto ownedId = owned->getId();
    f.world.destroyEntity(parent);
    CHECK(f.world.findEntity(ownedId) == nullptr); CHECK(attached->spatialParent() == nullptr);
    CHECK_FLOAT_EQ(attached->transform()->position.x,10,1e-5f);
    CHECK(attached->owner() == nullptr);
}
TEST_CASE(scene_roundtrip_restores_types_identity_and_forward_relations) {
    Fixture f;
    auto* child = f.world.createSpatialEntity(); auto* parent = f.world.createSpatialEntity(); auto* data = f.world.createDataEntity();
    child->setName("Child"); parent->setName("Parent"); data->setName("State");
    CHECK(child->setSpatialParent(parent)); CHECK(data->setOwner(parent));
    parent->transform()->position = {6,0,0}; child->transform()->position = {1,0,0};
    const auto id = child->persistentId();
    CHECK(saveScene(f.world,"spatial-roundtrip.scn")); CHECK(loadScene(f.world,"spatial-roundtrip.scn"));
    child = f.world.findEntity("Child"); parent = f.world.findEntity("Parent"); data = f.world.findEntity("State");
    CHECK(child && parent && data);
    if (child && parent && data) {
        CHECK(child->persistentId() == id); CHECK(child->spatialParent() == parent);
        CHECK(data->kind() == EntityKind::Data); CHECK(data->transform() == nullptr); CHECK(data->owner() == parent);
        CHECK_FLOAT_EQ(child->worldMatrix()(0,3),7,1e-5f);
    }
    std::remove("spatial-roundtrip.scn");
}
TEST_CASE(camera_uses_ancestor_pose_and_legacy_scene_promotes) {
    Fixture f;
    auto* parent = f.world.createSpatialEntity(); auto* eye = f.world.createSpatialEntity();
    parent->transform()->position = {12,0,0}; eye->transform()->position = {1,2,3};
    CHECK(eye->setSpatialParent(parent)); CHECK(eye->createComponent<PerspectiveCameraComponent>());
    PerspectiveCameraFrame frame; CHECK(evaluatePerspectiveCamera3D(f.world,1,1,frame));
    CHECK_FLOAT_EQ(frame.position.x,13,1e-5f);
    std::ofstream("legacy-space.scn") << R"({"__schemaVersion":3,"entities":[{"name":"Old","components":[{"$type":"Transform","position":[4,5,6]}]},{"name":"Logic","components":[]}]})";
    CHECK(loadScene(f.world,"legacy-space.scn"));
    auto* old = f.world.findEntity("Old"); auto* logic = f.world.findEntity("Logic");
    CHECK(old && old->isSpatial()); CHECK(logic && !logic->isSpatial());
    std::remove("legacy-space.scn");
}

TEST_CASE(deletion_preflight_refuses_lossy_detachment_and_bulk_clear_handles_it) {
    Fixture f;
    auto* parent = f.world.createSpatialEntity(); auto* child = f.world.createSpatialEntity();
    parent->transform()->scale = {2,1,1}; child->transform()->rotation = ayt::math::FQuaternion::fromEulerAngles({0,0,.7f});
    CHECK(child->setSpatialParent(parent));
    CHECK_FALSE(f.world.canDestroyEntity(parent)); CHECK(child->spatialParent() == parent);
    f.world.clearEntities(); CHECK(f.world.getAllEntities().empty());
    auto* next = f.world.createSpatialEntity(); CHECK(next && next->transform());
}

TEST_CASE(handles_do_not_rebind_after_world_shutdown_and_reinitialize) {
    Fixture f;
    auto* old = f.world.createSpatialEntity();
    const auto oldHandle = f.world.getEntityHandle(old->getId());
    f.world.shutdown(); CHECK(f.world.initialize());
    auto* replacement = f.world.createSpatialEntity();
    CHECK(replacement->getId() == oldHandle.id);
    CHECK(f.world.getEntityByHandle(oldHandle) == nullptr);
    CHECK(f.world.getEntityByHandle(f.world.getEntityHandle(replacement->getId())) == replacement);
}

TEST_CASE(malformed_relationships_refuse_load_and_restore_original_scene) {
    Fixture f;
    auto* original = f.world.createSpatialEntity(); original->setName("Original");
    original->transform()->position = {9,0,0}; const auto originalId = original->persistentId();
    const char* path = "invalid-spatial.scn";
    const char* invalid[] = {
        R"({"__schemaVersion":5,"entities":[{"kind":"data","persistentId":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa","name":"Bad","owner":"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb","components":[]}]})",
        R"({"__schemaVersion":5,"entities":[{"kind":"data","persistentId":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa","owner":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa","components":[]}]})",
        R"({"__schemaVersion":5,"entities":[{"kind":"data","persistentId":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa","components":[]},{"kind":"data","persistentId":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa","components":[]}]})",
        R"({"__schemaVersion":5,"entities":[{"kind":"data","persistentId":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa","components":[{"$type":"PerspectiveCameraComponent","$instanceId":"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"}]}]})"
    };
    for (auto* text : invalid) {
        std::ofstream(path) << text; ayt::serializer::SerializeError error;
        CHECK_FALSE(loadScene(f.world,path,&error)); CHECK_FALSE(error.ok()); CHECK(!error.message.empty());
        original = f.world.findEntityByPersistentId(originalId);
        CHECK(original != nullptr);
        if (original) { CHECK(original->isSpatial()); CHECK_FLOAT_EQ(original->transform()->position.x,9,1e-5f); }
    }
    std::remove(path);
}
TEST_CASE(affine_composition_preserves_rotation_scale_and_ancestor_interpolation) {
    Fixture f;
    auto* parent = f.world.createSpatialEntity(); auto* child = f.world.createSpatialEntity();
    parent->transform()->rotation = ayt::math::FQuaternion::fromEulerAngles({0,0,ayt::math::radians(90.f)});
    parent->transform()->scale = {2,1,1}; child->transform()->position = {1,0,0};
    CHECK(child->setSpatialParent(parent));
    parent->transform()->applySimulationPose({0,0,0},parent->transform()->rotation);
    parent->transform()->applySimulationPose({10,0,0},parent->transform()->rotation);
    const auto world = child->worldMatrix(.5f);
    CHECK_FLOAT_EQ(world(0,3),5,1e-4f); CHECK_FLOAT_EQ(world(1,3),2,1e-4f);
}

TEST_CASE(camera_skips_invalid_ancestor_and_hierarchy_depth_is_bounded) {
    Fixture f;
    auto* parent = f.world.createSpatialEntity(); auto* badEye = f.world.createSpatialEntity(); auto* goodEye = f.world.createSpatialEntity();
    CHECK(badEye->setSpatialParent(parent));
    auto* bad = badEye->createComponent<PerspectiveCameraComponent>(); bad->priority = 10;
    auto* good = goodEye->createComponent<PerspectiveCameraComponent>();
    parent->transform()->position.x = std::numeric_limits<float>::quiet_NaN();
    CHECK(selectPerspectiveCamera3D(f.world).camera == good);
    auto* root = f.world.createSpatialEntity(); auto* tail = root;
    for (int i=0;i<256;++i) {
        auto* next = f.world.createSpatialEntity(); CHECK(next->setSpatialParent(tail)); tail = next;
    }
    auto* extra = f.world.createSpatialEntity(); CHECK_FALSE(extra->setSpatialParent(tail));
    CHECK(extra->spatialParent() == nullptr);
}
TEST_SUITE_END
int main(int argc, char** argv) { return ayt::test::runTests("AYEntity_SpatialTests",argc,argv); }
