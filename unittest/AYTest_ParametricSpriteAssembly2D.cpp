#include <AYEntity/ParametricSpriteAssembly2D.h>
#include <AYEntity/EntityImpl.h>
#include <AYEntity/World.h>
#include <AYEntity/components/SpriteComponent.h>
#include <AYEntity/components/TransformComponent.h>
#include <AYTest.h>

#include <vector>

using namespace ayt;

TEST_SUITE(ParametricSpriteAssembly2DTests)

TEST_CASE(creates_stable_owned_layer_hierarchy_and_presents_pose) {
    auto& world = entity::World::instance();
    world.initialize();
    auto* parent = world.createEntity();
    CHECK(parent->addComponent<entity::Transform>() != nullptr);
    ay2d::ParametricSpriteRig2D rig;
    ay2d::PuppetLayer2D eyes;
    eyes.name = "eyes";
    eyes.parentName = "head"; // Forward parent is valid after rig validation.
    eyes.texturePath = "eyes.png";
    eyes.bindings.push_back({0, {}, {.x = 2}});
    ay2d::PuppetLayer2D head;
    head.name = "head";
    head.texturePath = "head.png";
    CHECK(rig.load({{"turn", 0, 0, 1}}, {eyes, head}));

    entity::ParametricSpriteAssembly2D assembly;
    std::string error;
    CHECK(assembly.create(world, rig, parent, &error));
    CHECK(error.empty());
    CHECK(assembly.layerCount() == 2);
    auto* root = assembly.root();
    auto* eyeEntity = assembly.layer(0);
    auto* headEntity = assembly.layer(1);
    CHECK(root && eyeEntity && headEntity);
    CHECK(root->spatialParent() == parent);
    CHECK(eyeEntity->owner() == root && headEntity->owner() == root);
    CHECK(eyeEntity->spatialParent() == headEntity);
    CHECK(headEntity->spatialParent() == root);
    const auto rootHandle = world.getEntityHandle(root->getId());
    const auto eyeHandle = world.getEntityHandle(eyeEntity->getId());
    CHECK(eyeEntity->getComponent<entity::SpriteComponent>()->texturePath ==
          "eyes.png");
    CHECK(rig.setParameters(std::vector<ay2d::PuppetParameterValue2D>{{0, 1}}));
    CHECK(assembly.present(rig, 20));
    CHECK(assembly.layer(0) == eyeEntity);
    CHECK(eyeEntity->getComponent<entity::SpriteComponent>()->sortingKey == 20);
    CHECK(eyeEntity->getComponent<entity::Transform>()->position.x == 2);
    CHECK_FALSE(assembly.create(world, rig, parent, &error));
    CHECK(assembly.layer(0) == eyeEntity);
    CHECK_FALSE(assembly.present(rig, 0x00FFFF01u));
    CHECK(eyeEntity->getComponent<entity::SpriteComponent>()->sortingKey == 20);
    ay2d::ParametricSpriteRig2D changedHierarchy;
    auto rootEyes = eyes;
    rootEyes.parentName.clear();
    CHECK(changedHierarchy.load({{"turn", 0, 0, 1}}, {rootEyes, head}));
    CHECK_FALSE(assembly.present(changedHierarchy, 3));
    CHECK(eyeEntity->getComponent<entity::SpriteComponent>()->sortingKey == 20);
    CHECK(assembly.destroy(&error));
    CHECK(error.empty());
    CHECK(assembly.root() == nullptr && assembly.layerCount() == 0);
    CHECK(world.getEntityByHandle(rootHandle) == nullptr);
    CHECK(world.getEntityByHandle(eyeHandle) == nullptr);
    CHECK(assembly.destroy());
    world.shutdown();
}

TEST_CASE(rejects_empty_rig_without_creating_entities) {
    auto& world = entity::World::instance();
    world.initialize();
    const auto before = world.getAllEntities().size();
    entity::ParametricSpriteAssembly2D assembly;
    ay2d::ParametricSpriteRig2D empty;
    std::string error;
    CHECK_FALSE(assembly.create(world, empty, nullptr, &error));
    CHECK_FALSE(error.empty());
    CHECK(world.getAllEntities().size() == before);
    ay2d::PuppetLayer2D meshLayer;
    meshLayer.name = "mesh";
    meshLayer.texturePath = "mesh.png";
    ay2d::PuppetLayerMesh2D mesh;
    mesh.vertices = {{0, 0, 0, 0}, {1, 0, 1, 0}, {0, 1, 0, 1}};
    mesh.indices = {0, 1, 2};
    meshLayer.mesh = mesh;
    ay2d::ParametricSpriteRig2D meshRig;
    CHECK(meshRig.load({{"pose", 0, 0, 1}}, {meshLayer}));
    CHECK_FALSE(assembly.create(world, meshRig, nullptr, &error));
    CHECK(world.getAllEntities().size() == before);
    world.shutdown();
}

TEST_SUITE_END
