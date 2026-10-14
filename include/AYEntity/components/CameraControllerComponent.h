#pragma once
#include <AYCore.h>
#include <AYEntity/IEntity.h>
#include <AYEntity/CameraFrame.h>
#include <AYEntity/EntityHandle.h>
#include <string>

namespace ayt::entity {
#define AY_CURRENT_CLASS CameraControllerComponent
/// Optional presentation controller for a camera Entity. Target names are scene references;
/// targets are re-resolved every frame and never retained as raw pointers.
struct CameraControllerComponent : IComponent {
    const char* getName() const override { return "CameraControllerComponent"; }
    /// 0=manual, 1=follow, 2=orbit, 3=free. Set twoD for XY tracking.
    AY_PROPERTY(int32_t, mode, kAttrSerialize)
    AY_PROPERTY(bool, twoD, kAttrSerialize)
    AY_PROPERTY(std::string, targetName, kAttrSerialize)
    AY_PROPERTY(math::FVector3, offset, kAttrSerialize)
    AY_PROPERTY(float, positionDamping, kAttrSerialize)
    AY_PROPERTY(float, rotationDamping, kAttrSerialize)
    AY_PROPERTY(math::FVector2, deadZone, kAttrSerialize)
    AY_PROPERTY(float, lookAheadSeconds, kAttrSerialize)
    AY_PROPERTY(bool, boundsEnabled, kAttrSerialize)
    AY_PROPERTY(math::FVector3, boundsMin, kAttrSerialize)
    AY_PROPERTY(math::FVector3, boundsMax, kAttrSerialize)
    AY_PROPERTY(bool, lookAtTarget, kAttrSerialize)
    AY_PROPERTY(float, yawRadians, kAttrSerialize)
    AY_PROPERTY(float, pitchRadians, kAttrSerialize)
    AY_PROPERTY(float, distance, kAttrSerialize)
    AY_PROPERTY(float, moveSpeed, kAttrSerialize)
    AY_PROPERTY(float, collisionRadius, kAttrSerialize)
    AY_PROPERTY(float, collisionPadding, kAttrSerialize)

    CameraControllerComponent() : mode(0), twoD(false), offset(0,0,-5),
        positionDamping(8), rotationDamping(8), deadZone(0,0), lookAheadSeconds(0),
        boundsEnabled(false), boundsMin(-10000,-10000,-10000), boundsMax(10000,10000,10000),
        lookAtTarget(true), yawRadians(0), pitchRadians(0), distance(5), moveSpeed(6),
        collisionRadius(.2f), collisionPadding(.05f) {}

    /// Local movement input (right/up/forward), set by the game's input facade.
    math::FVector3 movementInput{};
    math::FVector3 previousTarget{};
    bool hasPreviousTarget = false;
    std::string previousTargetName;
    EntityHandle previousTargetHandle{};
    math::FVector3 collisionAnchor{};
    bool hasCollisionAnchor = false;
    /// Runtime-only offsets applied to camera snapshots, never to authoritative Transform.
    math::FVector3 shakePosition{};
    math::FQuaternion shakeRotation{0,0,0,1};
    float shakeDuration = 0, shakeElapsed = 0, shakeFrequency = 20;
    float shakePositionAmplitude = 0, shakeRotationAmplitude = 0;
    uint32_t shakeSeed = 1;

    /// Restart a bounded, seeded shake; angles are radians and duration is presentation seconds.
    bool shake(float duration, float positionAmplitude, float rotationAmplitude,
               float frequency = 20, uint32_t seed = 1) noexcept {
        if (!std::isfinite(duration) || duration <= 0 || duration > 60
            || !std::isfinite(positionAmplitude) || positionAmplitude < 0
            || !std::isfinite(rotationAmplitude) || rotationAmplitude < 0
            || !std::isfinite(frequency) || frequency <= 0 || frequency > 1000) return false;
        shakeDuration = duration; shakeElapsed = 0; shakeFrequency = frequency;
        shakePositionAmplitude = positionAmplitude; shakeRotationAmplitude = rotationAmplitude;
        shakeSeed = seed; return true;
    }
    /// Reset follow velocity and transient effects after a teleport or target discontinuity.
    void resetHistory() noexcept {
        hasPreviousTarget = false; hasCollisionAnchor = false; shakeDuration = 0; shakeElapsed = 0;
        shakePosition = {}; shakeRotation = {0,0,0,1};
    }
};
#undef AY_CURRENT_CLASS
}
