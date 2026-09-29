#pragma once

// Grid-based frame animation for SpriteComponent.
//
// Frame zero is the top-left cell. Frames advance left-to-right, then
// top-to-bottom. `firstFrame` selects the first cell in that order and
// `frameCount` is clamped to the remaining cells. Runtime playback state is
// deliberately not reflected or serialized: reopening a Scene starts at the
// authored first frame.

#include <AYCore.h>
#include <AYEntity/IEntity.h>

#include <algorithm>
#include <cstdint>
#include <limits>

namespace ayt::entity
{

#define AY_CURRENT_CLASS SpriteAnimationComponent
/// Authors constant-rate playback for a regular SpriteComponent grid.
///
/// Cell zero is at the texture's top-left. Cells advance left-to-right and
/// then top-to-bottom. Runtime playback state is not serialized.
struct SpriteAnimationComponent : public IComponent {
    const char* getName() const override {
        return "SpriteAnimationComponent";
    }

    // Grid dimensions in the texture. Both values must be positive.
    AY_PROPERTY(int32_t, columns, kAttrSerialize)
    AY_PROPERTY(int32_t, rows, kAttrSerialize)
    // Zero-based top-left-origin cell index and number of consecutive frames.
    AY_PROPERTY(int32_t, firstFrame, kAttrSerialize)
    AY_PROPERTY(int32_t, frameCount, kAttrSerialize)
    // Constant duration for every frame. Runtime clamps this to at least 1 ms.
    AY_PROPERTY(int32_t, frameDurationMs, kAttrSerialize)
    // 0 = forward loop, 1 = forward once and hold the last frame.
    AY_PROPERTY(int32_t, playbackMode, kAttrSerialize)
    AY_PROPERTY(bool, playing, kAttrSerialize)

    // Runtime-only playback state. These values are intentionally excluded
    // from reflection and Scene serialization.
    uint32_t currentFrame = 0u;
    uint64_t elapsedMicroseconds = 0u;
    bool finished = false;
    bool wasPlaying = false;

    // Last applied authoring configuration. The system uses this to restart
    // playback when the Inspector changes the grid or timing contract.
    int32_t runtimeColumns = 0;
    int32_t runtimeRows = 0;
    int32_t runtimeFirstFrame = 0;
    int32_t runtimeFrameCount = 0;
    int32_t runtimeFrameDurationMs = 0;
    int32_t runtimePlaybackMode = 0;

    SpriteAnimationComponent()
    {
        columns = 1;
        rows = 1;
        firstFrame = 0;
        frameCount = 1;
        frameDurationMs = 100;
        playbackMode = 0;
        playing = true;
    }

    [[nodiscard]] bool isValid() const noexcept
    {
        if (columns <= 0 || rows <= 0 || firstFrame < 0
            || frameCount <= 0 || frameDurationMs <= 0) {
            return false;
        }
        const uint64_t cells = static_cast<uint64_t>(columns)
            * static_cast<uint64_t>(rows);
        return cells <= (std::numeric_limits<uint32_t>::max)()
            && static_cast<uint64_t>(firstFrame) < cells;
    }

    [[nodiscard]] uint32_t effectiveFrameCount() const noexcept
    {
        if (!isValid()) return 0u;
        const uint64_t cells = static_cast<uint64_t>(columns)
            * static_cast<uint64_t>(rows);
        const uint64_t remaining = cells - static_cast<uint64_t>(firstFrame);
        return static_cast<uint32_t>(
            std::min<uint64_t>(remaining, static_cast<uint64_t>(frameCount)));
    }

    void restart() noexcept
    {
        currentFrame = 0u;
        elapsedMicroseconds = 0u;
        finished = false;
        wasPlaying = false;
    }
};
#undef AY_CURRENT_CLASS

} // namespace ayt::entity
