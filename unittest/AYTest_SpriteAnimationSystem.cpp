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
    frame.sockets = {{"hand", 25, 12}};
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
    ayt::entity::Transform attachment;
    attachment.setPosition(0.0f, 0.0f, 3.0f);
    CHECK(ayt::entity::presentSpriteClipSocket2D(
        player, "hand", 2.0f, false, attachment));
    CHECK_FLOAT_EQ(attachment.position.x, 18.0f, 1e-6f);
    CHECK_FLOAT_EQ(attachment.position.y, 36.0f, 1e-6f);
    CHECK_FLOAT_EQ(attachment.position.z, 3.0f, 1e-6f);
    CHECK(ayt::entity::presentSpriteClipSocket2D(
        player, "hand", 2.0f, true, attachment));
    CHECK_FLOAT_EQ(attachment.position.x, -18.0f, 1e-6f);
    CHECK_FALSE(ayt::entity::presentSpriteClipSocket2D(
        player, "missing", 2.0f, true, attachment));
    CHECK_FLOAT_EQ(attachment.position.x, -18.0f, 1e-6f);
}

TEST_CASE(sprite_visual_anchor_snap_rounds_screen_position_without_moving_input)
{
    ayt::entity::CameraFrame camera;
    camera.projectionMode = ayt::entity::CameraProjection::Orthographic;
    camera.position = {0.0f, 0.0f, -10.0f};
    camera.verticalSize = 10.0f;
    camera.aspect = 1.0f;
    CHECK(ayt::entity::rebuildCameraFrame(camera));
    const ayt::entity::CameraViewport viewport{13.0f, 17.0f, 100.0f, 100.0f};
    const ayt::math::FVector3 anchor{0.123f, 0.267f, 0.0f};
    ayt::math::FVector3 snapped{999.0f, 999.0f, 999.0f};
    CHECK(ayt::entity::snapSpriteAnchorToScreenPixel2D(
        camera, viewport, anchor, snapped));
    CHECK_FLOAT_EQ(anchor.x, 0.123f, 1e-6f);
    CHECK_FLOAT_EQ(anchor.y, 0.267f, 1e-6f);
    ayt::math::FVector3 screen;
    CHECK(ayt::entity::worldToScreen(camera, viewport, snapped, screen));
    CHECK_FLOAT_EQ(screen.x, 64.0f, 1e-4f);
    CHECK_FLOAT_EQ(screen.y, 64.0f, 1e-4f);
    CHECK_FLOAT_EQ(snapped.z, anchor.z, 1e-4f);
    camera.projectionMode = ayt::entity::CameraProjection::Perspective;
    CHECK_FALSE(ayt::entity::snapSpriteAnchorToScreenPixel2D(
        camera, viewport, anchor, snapped));
    CHECK_FLOAT_EQ(snapped.x, 0.1f, 1e-4f);
    camera.projectionMode = ayt::entity::CameraProjection::Orthographic;
    const ayt::entity::CameraViewport invalid{0.0f, 0.0f, 0.0f, 100.0f};
    CHECK_FALSE(ayt::entity::snapSpriteAnchorToScreenPixel2D(
        camera, invalid, anchor, snapped));
    CHECK_FLOAT_EQ(snapped.x, 0.1f, 1e-4f);
}

TEST_CASE(sprite_clip_attachment_follows_mirrored_socket_with_own_clock)
{
    ayt::ay2d::SpriteClipSet2D source;
    source.pages.push_back({"textures/actor.png", 32, 16});
    ayt::ay2d::SpriteClipFrame2D first;
    first.width = 16;
    first.height = 16;
    first.canvasWidth = 16;
    first.canvasHeight = 16;
    first.pivotX = 8;
    first.pivotY = 12;
    first.sockets = {{"face", 11, 5}};
    auto second = first;
    second.x = 16;
    second.sockets.clear();
    source.clips.push_back({"move", true, {first, second}});
    ayt::ay2d::SpriteClipPlayer2D sourcePlayer;
    CHECK(sourcePlayer.select(source, "move"));

    ayt::ay2d::SpriteClipSet2D effect;
    effect.pages.push_back({"textures/face-effect.png", 16, 8});
    ayt::ay2d::SpriteClipFrame2D effectFirst;
    effectFirst.width = 8;
    effectFirst.height = 8;
    effectFirst.canvasWidth = 8;
    effectFirst.canvasHeight = 8;
    effectFirst.pivotX = 4;
    effectFirst.pivotY = 4;
    auto effectSecond = effectFirst;
    effectSecond.x = 8;
    effect.clips.push_back({"mosaic", true, {effectFirst, effectSecond}});
    ayt::ay2d::SpriteClipPlayer2D effectPlayer;
    CHECK(effectPlayer.select(effect, "mosaic"));

    ayt::entity::Transform socket;
    socket.setPosition(90.0f, 90.0f, 3.0f);
    socket.rotation = ayt::math::FQuaternion::fromEulerAngles({0, 0, 0.25f});
    ayt::entity::Transform visual;
    SpriteComponent sprite;
    sprite.visible = false;
    CHECK(ayt::entity::presentSpriteClipAttachment2D(
        sourcePlayer, "face", 2.0f, false, effect, effectPlayer,
        2.0f, false, socket, visual, sprite));
    CHECK(sprite.visible);
    CHECK_FLOAT_EQ(socket.position.x, 6.0f, 1e-6f);
    CHECK_FLOAT_EQ(socket.position.y, 14.0f, 1e-6f);
    CHECK_FLOAT_EQ(socket.position.z, 3.0f, 1e-6f);
    CHECK_FLOAT_EQ(socket.rotation.toEulerAngles().z, 0.25f, 1e-5f);
    CHECK_FLOAT_EQ(visual.scale.x, 16.0f, 1e-6f);
    CHECK(sprite.texturePath == "textures/face-effect.png");

    (void)effectPlayer.advanceUs(100000);
    CHECK(ayt::entity::presentSpriteClipAttachment2D(
        sourcePlayer, "face", 2.0f, true, effect, effectPlayer,
        2.0f, false, socket, visual, sprite));
    CHECK_FLOAT_EQ(socket.position.x, -6.0f, 1e-6f);
    CHECK_FLOAT_EQ(sprite.sourceRectMin.x, 0.5f, 1e-6f);
    CHECK(sprite.flip == 0);
    CHECK(sourcePlayer.sample().frame == 0u);

    CHECK_FALSE(ayt::entity::presentSpriteClipAttachment2D(
        sourcePlayer, "face", 2.0f, false, effect, effectPlayer,
        0.0f, false, socket, visual, sprite));
    CHECK_FALSE(sprite.visible);
    CHECK_FLOAT_EQ(socket.position.x, -6.0f, 1e-6f);
    CHECK(ayt::entity::presentSpriteClipAttachment2D(
        sourcePlayer, "face", 2.0f, false, effect, effectPlayer,
        2.0f, true, socket, visual, sprite));
    CHECK(sprite.visible);
    CHECK(sprite.flip == 1);

    (void)sourcePlayer.advanceUs(100000);
    CHECK_FALSE(ayt::entity::presentSpriteClipAttachment2D(
        sourcePlayer, "face", 2.0f, false, effect, effectPlayer,
        2.0f, false, socket, visual, sprite));
    CHECK_FALSE(sprite.visible);
    CHECK_FLOAT_EQ(socket.position.x, 6.0f, 1e-6f);
    CHECK_FLOAT_EQ(sprite.sourceRectMin.x, 0.5f, 1e-6f);
}

TEST_SUITE_END
