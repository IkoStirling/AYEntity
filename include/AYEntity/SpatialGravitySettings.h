#pragma once

#include <AYMath/MathTypes.h>
#include <cstdint>

namespace ayt::entity {

enum class SpatialGravityMode : uint8_t { World, None, Field, Custom };
enum class SpatialGravityShape : uint8_t { Uniform, Box, Radial };

/** Intrinsic physics authoring for a Spatial Entity. This is value data, not
 * a removable component or part of the local pose. The receiver settings are
 * used by 3D rigid bodies and may also be passed to character controllers.
 * A source contributes only to Field receivers with a matching channel bit. */
struct SpatialGravitySettings {
    SpatialGravityMode mode = SpatialGravityMode::World;
    uint32_t receiverChannels = 0xffffffffu;
    float multiplier = 1.0f;
    math::FVector3 customAcceleration{};

    bool sourceEnabled = false;
    SpatialGravityShape sourceShape = SpatialGravityShape::Box;
    uint32_t sourceChannels = 0xffffffffu;
    int32_t sourcePriority = 0;
    math::FVector3 sourceDirection{0.0f, -1.0f, 0.0f};
    math::FVector3 sourceHalfExtent{1.0f, 1.0f, 1.0f};
    float sourceRadius = 1.0f;
    float sourceAcceleration = 9.81f;
    bool sourceLinearFalloff = false;
};

/** Validates finite magnitudes and source shape before an atomic Entity edit. */
bool validateSpatialGravitySettings(const SpatialGravitySettings& settings) noexcept;

} // namespace ayt::entity
