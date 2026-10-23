#include <AYEntity/ActionSpritePresenter2D.h>
#include <AYTest.h>

TEST_SUITE(ActionSpritePresenter2DTests)
TEST_CASE(AppliesResultWithoutAdvancingActionState) {
    ayt::entity::SpriteComponent sprite;
    sprite.flip = 2;
    ayt::ay2d::ActionStep2D step;
    step.active = true;
    step.uv = {0.25f, 0.5f, 0.5f, 1.0f};
    step.facingRight = false;
    CHECK(ayt::entity::presentActionFrame2D(step, sprite));
    CHECK(sprite.sourceRectMin.x == 0.25f);
    CHECK(sprite.sourceRectMax.y == 1.0f);
    CHECK_INT_EQ(3, sprite.flip);
    step.active = false;
    CHECK(!ayt::entity::presentActionFrame2D(step, sprite));
    CHECK_INT_EQ(3, sprite.flip);
}
TEST_SUITE_END
