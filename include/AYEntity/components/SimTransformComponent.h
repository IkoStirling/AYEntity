#pragma once
// AYEntity/components/SimTransformComponent.h
// DET-04 deterministic translation state. Rotation and scale remain in the
// presentation Transform until AYMath defines their fixed-point contracts.

#include <AYEntity/IComponent.h>
#include <AYMath/Fixed.h>

#include <cstdint>

namespace ayt::entity
{

struct SimTransformComponent final : public IComponent {
    const char* getName() const override { return "SimTransformComponent"; }

    math::FixedVec3 previousPosition{};
    math::FixedVec3 position{};
    uint64_t revision = 0;
    bool hasPreviousPosition = false;

    // Called exactly once by World before each Sim-lane step. Sim systems may
    // then write position any number of times without destroying the previous
    // fixed-tick sample used by presentation interpolation.
    void beginSimulationStep() noexcept
    {
        previousPosition = position;
        hasPreviousPosition = true;
    }

    void setPosition(const math::FixedVec3& value) noexcept
    {
        position = value;
        ++revision;
    }

    void translate(const math::FixedVec3& delta) noexcept
    {
        position = position + delta;
        ++revision;
    }
};

} // namespace ayt::entity
