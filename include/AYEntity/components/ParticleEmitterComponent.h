#pragma once
#include <AYEntity/IEntity.h>
#include <AYParticle/Particle.h>
#include <memory>
namespace ayt::entity {
#define AY_CURRENT_CLASS ParticleGradientKey
/// Serializable authoring key; RGB can be HDR and alpha is normalized.
struct ParticleGradientKey {
    AY_PROPERTY(float, time, kAttrSerialize)
    AY_PROPERTY(math::FVector4, color, kAttrSerialize)
    ParticleGradientKey() : time(0), color(1,1,1,1) {}
};
#undef AY_CURRENT_CLASS
AY_FINALIZE_REGISTRATION_METADATA(ParticleGradientKey)
#define AY_CURRENT_CLASS ParticleEmitterComponent
/// Scene authoring values only are serialized. Playback and live particles reset on load.
/// Add Transform + this component to an Entity; registered Particle systems own ticking.
struct ParticleEmitterComponent : IComponent {
    const char* getName() const override { return "ParticleEmitterComponent"; }
    AY_PROPERTY(std::string, texturePath, kAttrSerialize)
    AY_PROPERTY(int32_t, capacity, kAttrSerialize)
    AY_PROPERTY(int32_t, burst, kAttrSerialize)
    AY_PROPERTY(int32_t, seed, kAttrSerialize)
    AY_PROPERTY(float, rate, kAttrSerialize)
    AY_PROPERTY(float, duration, kAttrSerialize)
    AY_PROPERTY(bool, looping, kAttrSerialize)
    AY_PROPERTY(bool, autoPlay, kAttrSerialize)
    AY_PROPERTY(int32_t, dimension, kAttrSerialize)
    AY_PROPERTY(int32_t, shape, kAttrSerialize)
    AY_PROPERTY(int32_t, simulationSpace, kAttrSerialize)
    AY_PROPERTY(int32_t, blend, kAttrSerialize)
    /// 0=CPU, 1=GPU with fallback, 2=Auto. Changing it restarts playback.
    /// 0=CPU (legacy default), 1=GPU with diagnosed CPU fallback, 2=Auto.
    AY_PROPERTY(int32_t, backend, kAttrSerialize)
    AY_PROPERTY(math::FVector3, extent, kAttrSerialize)
    AY_PROPERTY(math::FVector3, velocityMin, kAttrSerialize)
    AY_PROPERTY(math::FVector3, velocityMax, kAttrSerialize)
    AY_PROPERTY(math::FVector3, gravity, kAttrSerialize)
    AY_PROPERTY(float, drag, kAttrSerialize)
    AY_PROPERTY(math::FVector2, lifetime, kAttrSerialize)
    AY_PROPERTY(math::FVector2, size, kAttrSerialize)
    AY_PROPERTY(math::FVector2, rotation, kAttrSerialize)
    AY_PROPERTY(math::FVector2, angularVelocity, kAttrSerialize)
    AY_PROPERTY(float, endSizeScale, kAttrSerialize)
    AY_PROPERTY(math::FVector4, startColor, kAttrSerialize)
    AY_PROPERTY(math::FVector4, endColor, kAttrSerialize)
    /// Empty tracks keep legacy endpoint ramps. Points are normalized time/value.
    AY_PROPERTY(std::vector<math::FVector2>, sizeCurve, kAttrSerialize)
    AY_PROPERTY(std::vector<math::FVector2>, opacityCurve, kAttrSerialize)
    AY_PROPERTY(std::vector<ParticleGradientKey>, colorGradient, kAttrSerialize)
    AY_PROPERTY(int32_t, columns, kAttrSerialize)
    AY_PROPERTY(int32_t, rows, kAttrSerialize)
    AY_PROPERTY(int32_t, firstFrame, kAttrSerialize)
    AY_PROPERTY(int32_t, frameCount, kAttrSerialize)
    AY_PROPERTY(float, framesPerSecond, kAttrSerialize)
    /// Serialized typed sources, animation and 3D orientation; empty keeps legacy fields.
    AY_PROPERTY(std::string, propertySources, kAttrSerialize)
    AY_PROPERTY(int32_t, layer, kAttrSerialize)
    AY_PROPERTY(int32_t, sortingKey, kAttrSerialize)

    bool visible = true;
    std::unique_ptr<particle::ParticleInstance> runtime;
    std::string error;
    particle::PlaybackControl playback;
    bool usingGpu=false;
    std::string backendStatus="CPU";
    uint32_t gpuCapacity=0;
    ParticleEmitterComponent() { setEffect(particle::ParticleEffect{}); autoPlay=true; layer=sortingKey=0; }
    particle::ParticleEffect effect() const;
    void setEffect(const particle::ParticleEffect& effect);
    /// Rebind valid changed authoring values. Invalid edits stop and clear the instance.
    bool prepare();
    void play() { if (prepare()) { runtime->play(); playback.play(); } }
    void pause() { if (runtime) runtime->pause(); playback.pause(); }
    void resume() { if (runtime) runtime->resume(); playback.resume(); }
    void stop(bool clear=false) { if (runtime) runtime->stop(clear); playback.stop(clear); }
};
#undef AY_CURRENT_CLASS
}
