#pragma once
#include <AYEntity/components/SimTransformComponent.h>
#include <AYMath/DetQuaternion.h>

namespace ayt::entity {
/** @brief Software binary32 authoritative translation and opt-in rotation, distinct from Q16.16.
 * @note Attach with Transform, use Sim-lane writes and the core Bridge. Never
 * attach both Sim transform types to one Entity; the Bridge skips conflicting
 * authorities. Runtime-only: snapshots are field-wise inputs, not .ayscene data.
 * setRotation enables rotation ownership; scale remains presentation-only.
 */
struct DetSimTransformComponent final : public IComponent {
    static constexpr std::uint32_t kSnapshotVersion = 2;
    const char* getName() const override { return "DetSimTransformComponent"; }
    math::DetVec3 previousPosition{}, position{};
    std::uint64_t revision = 0;
    bool hasPreviousPosition = false;
    math::DetQuaternion previousRotation{}, rotation{};
    bool rotationEnabled = false;
    bool hasPreviousRotation = false;

    /// Persist fields with explicit byte order; never dump Snapshot object memory.
    struct Snapshot {
        std::uint32_t profileVersion = math::DetFloat32::kProfileVersion;
        std::array<std::uint32_t, 3> previousPosition{}, position{};
        std::uint64_t revision = 0;
        bool hasPreviousPosition = false;
        std::uint32_t snapshotVersion = kSnapshotVersion;
        std::uint32_t rotationProfileVersion = math::DetQuaternion::kRotationProfileVersion;
        std::array<std::uint32_t,4> previousRotation = math::DetQuaternion{}.bits(), rotation = math::DetQuaternion{}.bits();
        bool rotationEnabled = false, hasPreviousRotation = false;
        friend bool operator==(const Snapshot&,const Snapshot&) = default;
    };
    /// World calls once before each Sim step, retaining the interpolation sample.
    void beginSimulationStep() noexcept {
        previousPosition = position;
        hasPreviousPosition = true;
        if (rotationEnabled) { previousRotation = rotation; hasPreviousRotation = true; }
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
    /// Normalize finite nonzero input atomically and enable authoritative rotation.
    /// First activation seeds both samples; subsequent writes retain the pre-tick history.
    bool setRotation(math::DetQuaternion value) noexcept {
        const auto unit = value.normalized();
        if (!unit) return false;
        if (!rotationEnabled) { previousRotation = *unit; hasPreviousRotation = false; }
        rotation = *unit;
        rotationEnabled = true;
        ++revision;
        return true;
    }
    /// Apply delta in local space (rotation * delta); requires enabled rotation.
    bool rotateLocal(math::DetQuaternion delta) noexcept {
        if (!rotationEnabled) return false;
        const auto value = rotation.composed(delta);
        if (!value) return false;
        rotation = *value;
        ++revision;
        return true;
    }
    /// Release rotation ownership without modifying Present; next activation seeds fresh history.
    void disableRotation() noexcept {
        if (!rotationEnabled) return;
        rotationEnabled = false; hasPreviousRotation = false;
        previousRotation = rotation = {};
        ++revision;
    }
    /// Capture v2 field bits, scalar/rotation profiles, ownership and history flags.
    [[nodiscard]] Snapshot snapshot() const noexcept {
        return {math::DetFloat32::kProfileVersion, previousPosition.bits(), position.bits(), revision, hasPreviousPosition,
            kSnapshotVersion,math::DetQuaternion::kRotationProfileVersion,previousRotation.bits(),rotation.bits(),rotationEnabled,hasPreviousRotation};
    }
    /// Reject unknown schema/profiles, non-finite fields, zero rotations or invalid flags.
    /// Restore field bits, history and revision atomically without renormalization.
    bool restore(const Snapshot& state) noexcept {
        if (state.profileVersion != math::DetFloat32::kProfileVersion || state.snapshotVersion != kSnapshotVersion
            || state.rotationProfileVersion != math::DetQuaternion::kRotationProfileVersion) return false;
        const auto previous = math::DetVec3::fromBits(state.previousPosition);
        const auto current = math::DetVec3::fromBits(state.position);
        if (!previous.isFinite() || !current.isFinite()) return false;
        const auto oldRotation = math::DetQuaternion::fromBits(state.previousRotation);
        const auto newRotation = math::DetQuaternion::fromBits(state.rotation);
        if (!oldRotation.normalized() || !newRotation.normalized()
            || (state.hasPreviousRotation && !state.rotationEnabled)) return false;
        previousPosition = previous;
        position = current;
        revision = state.revision;
        hasPreviousPosition = state.hasPreviousPosition;
        previousRotation = oldRotation; rotation = newRotation;
        rotationEnabled = state.rotationEnabled; hasPreviousRotation = state.hasPreviousRotation;
        return true;
    }
    /// Explicit Q16.16 migration with nearest-even rounding; source is unchanged.
    /// Remove the old component before presentation; version the replay schema.
    /// Imports translation only and disables rotation. Decode old v1 translation
    /// snapshots into a fresh v2 Snapshot (identity rotations), then call restore.
    void importFixed(const SimTransformComponent& source) noexcept {
        const auto convert = [](math::FixedVec3 value) {
            const auto scale = math::DetFloat32::fromInt(math::Fixed32::kScale);
            return math::DetVec3::fromInts(value.x.raw(), value.y.raw(), value.z.raw()) / scale;
        };
        previousPosition = convert(source.previousPosition);
        position = convert(source.position);
        revision = source.revision;
        hasPreviousPosition = source.hasPreviousPosition;
        previousRotation = rotation = {};
        rotationEnabled = hasPreviousRotation = false;
    }
};
} // namespace ayt::entity
