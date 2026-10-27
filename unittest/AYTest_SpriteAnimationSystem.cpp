#include <AYEntity.h>
#include <AYEntity/SpriteAnimationSystem.h>
#include <AYEntity/SpriteClipPresenter2D.h>
#include <AYEntity/World.h>
#include <AYEntity/components/SpriteAnimationComponent.h>
#include <AYEntity/components/SpriteComponent.h>
#include <AYEntity/components/TransformComponent.h>

#include <AYTest.h>

#include <limits>

using ayt::entity::Entity;
using ayt::entity::SpriteAnimationComponent;
using ayt::entity::SpriteAnimationSystem;
using ayt::entity::SpriteComponent;
using ayt::entity::World;

namespace
{

struct AnimationFixture {
    World& world = World::instance();
    Entity* entity = nullptr;
    SpriteComponent* sprite = nullptr;
    SpriteAnimationComponent* animation = nullptr;
    SpriteAnimationSystem system;

    AnimationFixture()
    {
        world.shutdown();
        world.initialize();
        entity = world.createEntity();
        sprite = entity->addComponent<SpriteComponent>();
        animation = entity->addComponent<SpriteAnimationComponent>();
        animation->columns = 2;
        animation->rows = 2;
        animation->firstFrame = 0;
        animation->frameCount = 4;
        animation->frameDurationMs = 100;
    }

    ~AnimationFixture()
    {
        world.shutdown();
    }
};

void checkRect(const SpriteComponent& sprite,
               float minX, float minY, float maxX, float maxY)
{
    CHECK_FLOAT_EQ(sprite.sourceRectMin.x, minX, 1e-6f);
    CHECK_FLOAT_EQ(sprite.sourceRectMin.y, minY, 1e-6f);
    CHECK_FLOAT_EQ(sprite.sourceRectMax.x, maxX, 1e-6f);
    CHECK_FLOAT_EQ(sprite.sourceRectMax.y, maxY, 1e-6f);
}

} // namespace

TEST_SUITE(AYEntitySpriteAnimation)

TEST_CASE(grid_frames_advance_left_to_right_then_top_to_bottom)
{
    AnimationFixture fixture;

    fixture.system.onUpdate(0.0f);
    checkRect(*fixture.sprite, 0.0f, 0.0f, 0.5f, 0.5f);
    fixture.system.onUpdate(0.1f);
    checkRect(*fixture.sprite, 0.5f, 0.0f, 1.0f, 0.5f);
    fixture.system.onUpdate(0.2f);
    checkRect(*fixture.sprite, 0.5f, 0.5f, 1.0f, 1.0f);
    fixture.system.onUpdate(0.1f);
    checkRect(*fixture.sprite, 0.0f, 0.0f, 0.5f, 0.5f);
}

TEST_CASE(pausing_preserves_subframe_time_and_resume_continues)
{
    AnimationFixture fixture;

    fixture.system.onUpdate(0.05f);
    CHECK(fixture.animation->elapsedMicroseconds == 50000u);
    fixture.animation->playing = false;
    fixture.system.onUpdate(1.0f);
    CHECK(fixture.animation->currentFrame == 0u);
    CHECK(fixture.animation->elapsedMicroseconds == 50000u);
    fixture.animation->playing = true;
    fixture.system.onUpdate(0.05f);
    CHECK(fixture.animation->currentFrame == 1u);
    CHECK(fixture.animation->elapsedMicroseconds == 0u);
}

TEST_CASE(once_mode_holds_last_frame_and_toggle_restarts)
{
    AnimationFixture fixture;
    fixture.animation->frameCount = 3;
    fixture.animation->playbackMode = 1;

    fixture.system.onUpdate(0.35f);
    CHECK(fixture.animation->currentFrame == 2u);
    CHECK(fixture.animation->finished);
    fixture.system.onUpdate(1.0f);
    CHECK(fixture.animation->currentFrame == 2u);

    fixture.animation->playing = false;
    fixture.system.onUpdate(0.0f);
    fixture.animation->playing = true;
    fixture.system.onUpdate(0.0f);
    CHECK(fixture.animation->currentFrame == 0u);
    CHECK_FALSE(fixture.animation->finished);
}

TEST_CASE(configuration_change_restarts_at_new_first_frame)
{
    AnimationFixture fixture;
    fixture.system.onUpdate(0.2f);
    CHECK(fixture.animation->currentFrame == 2u);

    fixture.animation->firstFrame = 1;
    fixture.animation->frameCount = 2;
    fixture.system.onUpdate(0.0f);
    CHECK(fixture.animation->currentFrame == 0u);
    checkRect(*fixture.sprite, 0.5f, 0.0f, 1.0f, 0.5f);
}

TEST_CASE(invalid_configuration_does_not_change_authored_source_rect)
{
    AnimationFixture fixture;
    fixture.sprite->sourceRectMin = {0.2f, 0.3f};
    fixture.sprite->sourceRectMax = {0.7f, 0.9f};
    fixture.animation->columns = 0;

    fixture.system.onUpdate(10.0f);
    checkRect(*fixture.sprite, 0.2f, 0.3f, 0.7f, 0.9f);
}

TEST_CASE(negative_or_non_finite_delta_does_not_advance)
{
    AnimationFixture fixture;

    fixture.system.onUpdate(-1.0f);
    fixture.system.onUpdate((std::numeric_limits<float>::quiet_NaN)());
    CHECK(fixture.animation->currentFrame == 0u);
    CHECK(fixture.animation->elapsedMicroseconds == 0u);
}

TEST_CASE(named_clip_presenter_places_visual_without_moving_actor)
{
    ayt::ay2d::SpriteClipSet2D set;
    set.pages.push_back({"textures/actor.png", 64, 64});
    ayt::ay2d::SpriteClipFrame2D frame;
    frame.width = 20;
    frame.height = 24;
    frame.canvasWidth = 32;
    frame.canvasHeight = 32;
    frame.trimX = 4;
    frame.trimY = 4;
    frame.pivotX = 16;
    frame.pivotY = 30;
    set.clips.push_back({"idle", true, {frame}});
    ayt::ay2d::SpriteClipPlayer2D player;
    CHECK(player.select(set, "idle"));

    ayt::entity::Transform actor;
    actor.setPosition(100.0f, 200.0f, 0.0f);
    ayt::entity::Transform visual;
    SpriteComponent sprite;
    CHECK(ayt::entity::presentSpriteClip2D(set, player, 2.0f,
                                           visual, sprite));
    CHECK_FLOAT_EQ(actor.position.x, 100.0f, 1e-6f);
    CHECK_FLOAT_EQ(actor.position.y, 200.0f, 1e-6f);
    CHECK_FLOAT_EQ(visual.position.x, -4.0f, 1e-6f);
    CHECK_FLOAT_EQ(visual.position.y, 28.0f, 1e-6f);
    CHECK_FLOAT_EQ(visual.scale.x, 40.0f, 1e-6f);
    CHECK_FLOAT_EQ(visual.scale.y, 48.0f, 1e-6f);
    CHECK(sprite.texturePath == "textures/actor.png");
    sprite.flip = 2;
    CHECK(ayt::entity::presentSpriteClip2D(set, player, 2.0f,
                                           visual, sprite, true));
    CHECK_FLOAT_EQ(visual.position.x, 4.0f, 1e-6f);
    CHECK(sprite.flip == 3);
    CHECK(ayt::entity::presentSpriteClip2D(set, player, 2.0f,
                                           visual, sprite, false));
    CHECK_FLOAT_EQ(visual.position.x, -4.0f, 1e-6f);
    CHECK(sprite.flip == 2);
}

TEST_SUITE_END
