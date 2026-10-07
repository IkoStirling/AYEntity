#pragma once
#include <AYEntity/components/SimTransformComponent.h>
#include <AYMath/DetVector.h>

namespace ayt::entity {
/** @brief Software binary32 authoritative translation state, distinct from Q16.16.
 * @note Attach with Transform, use Sim-lane writes and the core Bridge. Never
 * attach both Sim transform types to one Entity; the Bridge skips conflicting
 * authorities. Runtime-only: snapshots are field-wise inputs, not .ayscene data.
 * Rotation/scale remain presentation values in this phase.
 */
struct DetSimTransformComponent final : public IComponent {
    const char* getName() const override { return "DetSimTransformComponent"; }
    math::DetVec3 previousPosition{}, position{};
    std::uint64_t revision = 0;
    bool hasPreviousPosition = false;

    /// Persist fields with explicit byte order; never dump Snapshot object memory.
    struct Snapshot {
        std::uint32_t profileVersion = math::DetFloat32::kProfileVersion;
        std::array<std::uint32_t, 3> previousPosition{}, position{};
        std::uint64_t revision = 0;
        bool hasPreviousPosition = false;
    };
    /// World calls once before each Sim step, retaining the interpolation sample.
    void beginSimulationStep() noexcept {
        previousPosition = position;
        hasPreviousPosition = true;
    }
    /// Reject non-finite coordinates before mutation; success advances revision.
    bool setPosition(math::DetVec3 value) noexcept {
        if (!value.isFinite()) return false;
        position = value;
        ++revision;
        return true;
    }
    /// Reject invalid inputs/overflow before mutation; no implicit Fixed/native conversion.
    bool translate(math::DetVec3 delta) noexcept {
        if (!delta.isFinite()) return false;
        return setPosition(position + delta);
    }
    /// Capture canonical field bits and the scalar profile identifier.
    [[nodiscard]] Snapshot snapshot() const noexcept {
        return {math::DetFloat32::kProfileVersion, previousPosition.bits(), position.bits(), revision, hasPreviousPosition};
    }
    /// Reject unknown profile/non-finite fields atomically; restore history and revision.
    bool restore(const Snapshot& state) noexcept {
        if (state.profileVersion != math::DetFloat32::kProfileVersion) return false;
        const auto previous = math::DetVec3::fromBits(state.previousPosition);
        const auto current = math::DetVec3::fromBits(state.position);
        if (!previous.isFinite() || !current.isFinite()) return false;
        previousPosition = previous;
        position = current;
        revision = state.revision;
        hasPreviousPosition = state.hasPreviousPosition;
        return true;
    }
    /// Explicit Q16.16 migration with nearest-even rounding; source is unchanged.
    /// Remove the old component before presentation; version the replay schema.
    void importFixed(const SimTransformComponent& source) noexcept {
        const auto convert = [](math::FixedVec3 value) {
            const auto scale = math::DetFloat32::fromInt(math::Fixed32::kScale);
            return math::DetVec3::fromInts(value.x.raw(), value.y.raw(), value.z.raw()) / scale;
        };
        previousPosition = convert(source.previousPosition);
        position = convert(source.position);
        revision = source.revision;
        hasPreviousPosition = source.hasPreviousPosition;
    }
};
} // namespace ayt::entity
