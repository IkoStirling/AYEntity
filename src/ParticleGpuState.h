#pragma once
#include <AYParticle/EffectAsset.h>
#include <AYRenderer/ParticleDraw.h>
#include <memory>
#include <vector>
namespace ayt::entity {
/// Internal runtime bridge retained by World scene builders or isolated authoring previews.
/// Owners release it before renderer shutdown; standard-only stub on CPU builds.
class ParticleGpuState {
public:
    ParticleGpuState();
    ~ParticleGpuState();
    void beginFrame();
    void endFrame();
    bool select(uint64_t key,const particle::EffectAsset& asset,int policy,uint32_t allowance,std::string& status);
    void update(uint64_t key,float dt,const particle::Pose& pose,particle::PlaybackControl& control,
                uint16_t computeView=render::kParticleComputeView);
    uint32_t count(uint64_t key) const;
    std::vector<render::ParticleDrawData> draws(uint64_t key) const;
private:
    struct Impl;
    std::unique_ptr<Impl> _impl;
};
}
