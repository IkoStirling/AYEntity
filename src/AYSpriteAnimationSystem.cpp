#include <AYEntity/SpriteAnimationSystem.h>

#include <AYEntity.h>
#include <AYEntity/World.h>
#include <AYEntity/components/SpriteAnimationComponent.h>
#include <AYEntity/components/SpriteComponent.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace ayt::entity
{
namespace
{

bool configurationChanged(const SpriteAnimationComponent& animation) noexcept
{
    return animation.runtimeColumns != animation.columns
        || animation.runtimeRows != animation.rows
        || animation.runtimeFirstFrame != animation.firstFrame
        || animation.runtimeFrameCount != animation.frameCount
        || animation.runtimeFrameDurationMs != animation.frameDurationMs
        || animation.runtimePlaybackMode != animation.playbackMode;
}

void rememberConfiguration(SpriteAnimationComponent& animation) noexcept
{
    animation.runtimeColumns = animation.columns;
    animation.runtimeRows = animation.rows;
    animation.runtimeFirstFrame = animation.firstFrame;
    animation.runtimeFrameCount = animation.frameCount;
    animation.runtimeFrameDurationMs = animation.frameDurationMs;
    animation.runtimePlaybackMode = animation.playbackMode;
}

void applyFrame(const SpriteAnimationComponent& animation,
                SpriteComponent& sprite) noexcept
{
    const uint32_t count = animation.effectiveFrameCount();
    if (count == 0u) return;
    const uint32_t relative = std::min(animation.currentFrame, count - 1u);
    const uint32_t frame = static_cast<uint32_t>(animation.firstFrame)
        + relative;
    const uint32_t columns = static_cast<uint32_t>(animation.columns);
    const uint32_t rows = static_cast<uint32_t>(animation.rows);
    const uint32_t col = frame % columns;
    const uint32_t row = frame / columns;
    const float width = 1.0f / static_cast<float>(columns);
    const float height = 1.0f / static_cast<float>(rows);
    sprite.sourceRectMin = {
        static_cast<float>(col) * width,
        static_cast<float>(row) * height};
    sprite.sourceRectMax = {
        static_cast<float>(col + 1u) * width,
        static_cast<float>(row + 1u) * height};
}

uint64_t deltaMicroseconds(float dt) noexcept
{
    if (!std::isfinite(dt) || dt <= 0.0f) return 0u;
    constexpr double maxSeconds = 3600.0;
    const double seconds = std::min(static_cast<double>(dt), maxSeconds);
    return static_cast<uint64_t>(std::llround(seconds * 1000000.0));
}

void advanceAnimation(SpriteAnimationComponent& animation,
                      SpriteComponent& sprite, float dt) noexcept
{
    if (configurationChanged(animation)) {
        animation.restart();
        rememberConfiguration(animation);
    }
    const uint32_t count = animation.effectiveFrameCount();
    if (count == 0u) return;

    if (!animation.playing) {
        animation.wasPlaying = false;
        applyFrame(animation, sprite);
        return;
    }
    if (!animation.wasPlaying && animation.finished) {
        animation.restart();
        rememberConfiguration(animation);
    }
    animation.wasPlaying = true;

    if (!animation.finished) {
        const uint64_t delta = deltaMicroseconds(dt);
        const uint64_t duration = static_cast<uint64_t>(
            std::max(animation.frameDurationMs, 1)) * 1000u;
        const uint64_t room = (std::numeric_limits<uint64_t>::max)()
            - animation.elapsedMicroseconds;
        animation.elapsedMicroseconds += std::min(delta, room);
        const uint64_t steps = animation.elapsedMicroseconds / duration;
        animation.elapsedMicroseconds %= duration;
        if (steps != 0u) {
            if (animation.playbackMode == 1) {
                const uint64_t target = static_cast<uint64_t>(
                    animation.currentFrame) + steps;
                if (target >= count - 1u) {
                    animation.currentFrame = count - 1u;
                    animation.elapsedMicroseconds = 0u;
                    animation.finished = true;
                } else {
                    animation.currentFrame = static_cast<uint32_t>(target);
                }
            } else {
                animation.currentFrame = static_cast<uint32_t>(
                    (static_cast<uint64_t>(animation.currentFrame) + steps)
                    % count);
            }
        }
    }
    applyFrame(animation, sprite);
}

} // namespace

void SpriteAnimationSystem::onUpdate(float dt)
{
    for (Entity* entity : World::instance().query<
             SpriteComponent, SpriteAnimationComponent>()) {
        if (entity == nullptr) continue;
        auto* sprite = entity->getComponent<SpriteComponent>();
        auto* animation = entity->getComponent<SpriteAnimationComponent>();
        if (sprite == nullptr || animation == nullptr) continue;
        advanceAnimation(*animation, *sprite, dt);
    }
}

void registerSpriteAnimationSystem()
{
    World::instance().registerSystem<SpriteAnimationSystem>(
        SpriteAnimationSystem::kPriority);
}

} // namespace ayt::entity
