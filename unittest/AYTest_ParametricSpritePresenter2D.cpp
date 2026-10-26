#include <AYEntity/ParametricSpritePresenter2D.h>
#include <AYTest.h>
#include <cmath>

using namespace ayt;

TEST_SUITE(ParametricSpritePresenter2DTests)
TEST_CASE(evaluated_layer_updates_sprite_entity) {
    ay2d::ParametricSpriteRig2D rig;
    ay2d::PuppetLayer2D layer;
    layer.name = "mouth";
    layer.texturePath = "mouth.png";
    layer.renderLayer = 4;
    layer.u0 = 0.25f;
    layer.u1 = 0.75f;
    layer.bindings.push_back({0, {}, {.x = 2, .opacity = -0.5f}});
    CHECK(rig.load({{"open", 0, 0, 1}}, {layer}));
    CHECK(rig.setParameters(std::vector<ay2d::PuppetParameterValue2D>{{0, 1}}));
    std::vector<ay2d::PuppetLayerPose2D> pose;
    CHECK(rig.evaluate(pose));
    entity::Transform transform;
    transform.position.z = 7;
    entity::SpriteComponent sprite;
    sprite.colorRGBA.x = 0.2f;
    CHECK(entity::presentParametricSpriteLayer2D(
        rig, 0, pose[0], transform, sprite, 100));
    CHECK(sprite.texturePath == "mouth.png");
    CHECK(std::abs(transform.position.x - 2) < 0.0001f);
    CHECK(std::abs(transform.position.z - 7) < 0.0001f);
    CHECK(std::abs(sprite.colorRGBA.w - 0.5f) < 0.0001f);
    CHECK(std::abs(sprite.colorRGBA.x - 0.2f) < 0.0001f);
    CHECK(std::abs(sprite.sourceRectMin.x - 0.25f) < 0.0001f);
    CHECK_INT_EQ(sprite.layer, 4);
    CHECK_INT_EQ(sprite.sortingKey, 100);
    CHECK_FALSE(transform.hasPreviousSimulationPose);
}
TEST_CASE(invalid_pose_leaves_both_components_unmodified) {
    ay2d::ParametricSpriteRig2D rig;
    ay2d::PuppetLayer2D layer;
    layer.name = "body";
    layer.texturePath = "body.png";
    CHECK(rig.load({{"turn", -1, 0, 1}}, {layer}));
    entity::Transform transform;
    entity::SpriteComponent sprite;
    ay2d::PuppetLayerPose2D invalid;
    invalid.scaleX = -1;
    CHECK_FALSE(entity::presentParametricSpriteLayer2D(
        rig, 0, invalid, transform, sprite));
    CHECK(sprite.texturePath.empty());
    CHECK_INT_EQ(transform.revision, 0);
    CHECK_FALSE(entity::presentParametricSpriteLayer2D(
        rig, 1, {}, transform, sprite));
}
TEST_SUITE_END
