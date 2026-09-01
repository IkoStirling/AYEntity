#pragma once

#include <cstdint>

namespace ayt::entity
{

struct TilemapCameraVisibility {
    bool valid = false;
    float minX = 0.0f;
    float minY = 0.0f;
    float maxX = 0.0f;
    float maxY = 0.0f;
    uint32_t layerMask = 0xFFFFFFFFu;
    uint64_t revision = 0;
};

// Frame-local hand-off from TilemapStreamingSystem (priority 430) to
// TilemapRenderSystem (priority 510). No camera is a deliberate fail-open
// state: headless tools render the complete finite map.
class TilemapVisibilityRuntime {
public:
    static TilemapVisibilityRuntime& instance();

    void publish(const TilemapCameraVisibility& state) noexcept;
    void clear() noexcept;
    [[nodiscard]] const TilemapCameraVisibility& current() const noexcept;

private:
    TilemapCameraVisibility _state{};
};

} // namespace ayt::entity
