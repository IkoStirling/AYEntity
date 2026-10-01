#pragma once
#include <AYEntity/IEntity.h>
#include <AYParticle/EffectAsset.h>
#include <memory>
namespace ayt::entity {
#define AY_CURRENT_CLASS ParticleEffectComponent
/// Reference a reusable .ayparticle composition on an Entity with Transform.
/// Only authoring fields are serialized. Default particle integration loads assets.
struct ParticleEffectComponent : IComponent {
    const char* getName() const override { return "ParticleEffectComponent"; }
    AY_PROPERTY(std::string, effectPath, kAttrSerialize)
    AY_PROPERTY(bool, autoPlay, kAttrSerialize)
    AY_PROPERTY(int32_t, layer, kAttrSerialize)
    AY_PROPERTY(int32_t, sortingKey, kAttrSerialize)
    /// 0=CPU, 1=GPU with fallback, 2=Auto, 3=asset preference (default).
    AY_PROPERTY(int32_t, backend, kAttrSerialize)
    bool visible = true;
    std::unique_ptr<particle::EffectInstance> runtime;
    std::string error;
    particle::PlaybackControl playback;
    bool usingGpu=false;
    std::string backendStatus="CPU";
    uint32_t gpuCapacity=0;
    ParticleEffectComponent() { autoPlay=true; layer=sortingKey=0; backend=3; }
    /// Apply a validated snapshot. Identical values preserve playback.
    bool prepare(const particle::EffectAsset& asset);
    void play() { if(runtime) { runtime->play(); playback.play(); } }
    void pause() { if(runtime) runtime->pause(); playback.pause(); }
    void resume() { if(runtime) runtime->resume(); playback.resume(); }
    void stop(bool clear=false) { if(runtime) runtime->stop(clear); playback.stop(clear); }
};
#undef AY_CURRENT_CLASS
}
