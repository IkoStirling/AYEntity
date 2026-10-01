#include "ParticleGpuState.h"
#include <AYResource/ResourceManager.h>
#include <AYResource/AssetPath.h>
#include <AYResource/assetsDefs/ITexture.h>
#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <unordered_set>
#include <stdexcept>
#if defined(AY_ENTITY_PARTICLE_GPU)
#include <AYParticle/GpuParticle.h>
#endif
namespace ayt::entity {
struct ParticleGpuState::Impl {
#if defined(AY_ENTITY_PARTICLE_GPU)
    struct Entry {
        particle::EffectAsset asset;
        std::vector<std::unique_ptr<particle::GpuEmitter>> emitters;
        std::vector<std::shared_ptr<resource::ITexture>> textures;
        std::vector<bool> started,enabled;
        std::vector<float> delays;
        uint64_t cycle=0;
        uint64_t restart=0,clear=0;
        double phase=0;
        bool playing=false;
    };
    // Entries must die before the context; all live inside the scene builder callback.
    std::unique_ptr<particle::GpuParticleContext> context;
    std::unordered_map<uint64_t,Entry> entries;
    std::unordered_set<uint64_t> seen;
    std::string failure;
#endif
};
ParticleGpuState::ParticleGpuState():_impl(std::make_unique<Impl>()) {}
ParticleGpuState::~ParticleGpuState()=default;
void ParticleGpuState::beginFrame() {
#if defined(AY_ENTITY_PARTICLE_GPU)
    _impl->seen.clear();
#endif
}
void ParticleGpuState::endFrame() {
#if defined(AY_ENTITY_PARTICLE_GPU)
    std::erase_if(_impl->entries,[&](const auto& pair){ return !_impl->seen.contains(pair.first); });
#endif
}
bool ParticleGpuState::select(uint64_t key,const particle::EffectAsset& asset,int policy,uint32_t allowance,std::string& status) {
    if(policy==0) { status="CPU"; return false; }
#if defined(AY_ENTITY_PARTICLE_GPU)
    auto& state=*_impl;
    const auto fallback=[&](const std::string& message){ state.entries.erase(key); status="CPU fallback: "+message; return false; };
    if(!particle::GpuParticleContext::supported()) return fallback("device has no supported D3D11 compute backend");
    if(asset.duration<.001f) return fallback("composition duration is below 1ms");
    uint64_t capacity=0; std::string error;
    for(const auto& emitter:asset.emitters) {
        if(!particle::GpuEmitter::validate(particle::layerEffect(emitter),&error)) return fallback(emitter.name+": "+error);
        capacity+=particle::layerEffect(emitter).capacity;
    }
    if(capacity>allowance) return fallback("World particle budget cannot reserve GPU capacity");
    if(!state.failure.empty()) return fallback(state.failure);
    try {
        if(!state.context) state.context=std::make_unique<particle::GpuParticleContext>();
        auto found=state.entries.find(key);
        if(found==state.entries.end() || !(found->second.asset==asset)) {
            Impl::Entry entry; entry.asset=asset; entry.started.resize(asset.emitters.size(),false); entry.enabled.resize(asset.emitters.size(),true); entry.delays.resize(asset.emitters.size());
            for(const auto& emitter:asset.emitters) {
                entry.emitters.push_back(std::make_unique<particle::GpuEmitter>(*state.context,particle::layerEffect(emitter)));
                entry.textures.push_back(nullptr);
            }
            found=state.entries.insert_or_assign(key,std::move(entry)).first;
        }
        // Changed resource snapshots update texture bytes without restarting motion.
        for(size_t index=0;index<asset.emitters.size();++index) {
            const auto& path=asset.emitters[index].effect.texturePath;
            if(path.empty()) continue;
            const auto logical=resource::resolveAssetPath({},path);
            auto& resources=resource::ResourceManager::instance();
            auto texture=resources.load<resource::ITexture>(logical);
            if(!texture||resources.hasLoadFailed(logical)||texture->getFormat()!=resource::TextureFormat::RGBA8
                ||texture->getWidth()>65535||texture->getHeight()>65535||!texture->getMipmapData())
                return fallback("texture needs a valid RGBA8 sprite: "+path);
            if(found->second.textures[index]!=texture) {
                found->second.emitters[index]->setTexture(uint16_t(texture->getWidth()),uint16_t(texture->getHeight()),
                    texture->getMipmapData(),size_t(texture->getWidth())*texture->getHeight()*4);
                found->second.textures[index]=std::move(texture);
            }
        }
        state.seen.insert(key); status="GPU"; return true;
    } catch(const std::exception& ex) {
        if(!state.context) state.failure=ex.what();
        return fallback(ex.what());
    }
#else
    (void)key; (void)asset; (void)allowance;
    status="CPU fallback: GPU backend is not built"; return false;
#endif
}
void ParticleGpuState::update(uint64_t key,float dt,const particle::Pose& parent,particle::PlaybackControl& control,uint16_t computeView) {
#if defined(AY_ENTITY_PARTICLE_GPU)
    auto found=_impl->entries.find(key); if(found==_impl->entries.end()) return;
    auto& entry=found->second;
    const auto finite=[](particle::Vec3 v){ return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z); };
    if(!finite(parent.position)||!finite(parent.right)||!finite(parent.up)||!finite(parent.forward)
        ||!std::isfinite(dt)||dt<0) return;
    const auto reset=[&]() {
        for(auto& emitter:entry.emitters) emitter->stop(true);
        std::fill(entry.started.begin(),entry.started.end(),false);
        for(size_t n=0;n<entry.emitters.size();++n) {entry.enabled[n]=particle::layerEnabled(entry.asset,n,entry.cycle);entry.delays[n]=particle::layerDelay(entry.asset,n,entry.cycle);}
    };
    if(entry.restart!=control.restart) { entry.cycle=0; reset(); entry.phase=0; entry.playing=true; entry.restart=control.restart; }
    if(entry.clear!=control.clear && control.clear>control.restart) { reset(); entry.playing=false; }
    entry.clear=control.clear;
    if(!control.playing) { entry.playing=false; for(auto& emitter:entry.emitters) emitter->stop(false); }
    const bool stepped=control.stepSeconds>0;
    const bool paused=control.paused&&!stepped;
    double step=paused?0:std::clamp(stepped?control.stepSeconds:dt,0.0f,.25f);
    control.stepSeconds=0;
    if(entry.playing) {
        const double end=entry.phase+step;
        if(entry.asset.looping&&end>=entry.asset.duration) { entry.cycle+=uint64_t(std::floor(end/entry.asset.duration)); reset(); entry.phase=std::fmod(end,double(entry.asset.duration)); step=entry.phase; }
        else entry.phase=end;
    }
    for(size_t index=0;index<entry.emitters.size();++index) {
        auto& emitter=*entry.emitters[index]; const auto& source=entry.asset.emitters[index];
        float active=float(step);
        if(!entry.started[index]) {
            if(entry.enabled[index]&&entry.playing&&!paused&&entry.phase>=entry.delays[index]) {
                emitter.play(particle::layerSeed(entry.asset,index),entry.cycle); entry.started[index]=true;
                active=float(std::min(step,entry.phase-entry.delays[index]));
            } else active=0;
        }
        if(paused) emitter.pause(); else emitter.resume();
        auto pose=parent; pose.position=parent.point(source.offset);
        emitter.updatePose(computeView,active,pose,entry.started[index]?UINT32_MAX:0);
    }
    if(entry.playing&&!entry.asset.looping) {
        double latestDeath=0; bool repeating=false;
        for(size_t index=0;index<entry.asset.emitters.size();++index) {
            if(!entry.enabled[index])continue; const auto e=particle::layerEffect(entry.asset.emitters[index]);
            repeating=repeating||e.looping;
            const auto& life=e.value(particle::FloatAttribute::Lifetime);float maxLife=e.lifetime.max;
            if(life.mode==particle::NumberMode::Constant)maxLife=life.value;
            else if(life.mode==particle::NumberMode::Uniform)maxLife=life.max;
            else if(life.mode==particle::NumberMode::Weighted){maxLife=0;for(const auto& c:life.choices)if(c.weight>0)maxLife=std::max(maxLife,c.value);}
            else if(life.mode!=particle::NumberMode::Legacy){maxLife=0;for(const auto* keys:{&life.curve,&life.upperCurve})for(const auto& k:*keys)maxLife=std::max(maxLife,k.value);}
            latestDeath=std::max(latestDeath,double(entry.delays[index])+e.duration+maxLife);
        }
        // Conservative transport completion without synchronizing particle state to the CPU.
        if(!repeating&&entry.phase>=latestDeath) { entry.playing=false; control.playing=false; }
    }
#else
    (void)key; (void)dt; (void)parent; (void)control; (void)computeView;
#endif
}
uint32_t ParticleGpuState::count(uint64_t key) const {
#if defined(AY_ENTITY_PARTICLE_GPU)
    auto found=_impl->entries.find(key); return found==_impl->entries.end()?0:uint32_t(found->second.emitters.size());
#else
    (void)key; return 0;
#endif
}
std::vector<render::ParticleDrawData> ParticleGpuState::draws(uint64_t key) const {
    std::vector<render::ParticleDrawData> result;
#if defined(AY_ENTITY_PARTICLE_GPU)
    auto found=_impl->entries.find(key); if(found==_impl->entries.end()) return result;
    for(const auto& emitter:found->second.emitters) {
        render::ParticleDrawData data; data.gpuStream=emitter.get();
        data.submitGpu=[](void* pointer,uint16_t view){ static_cast<particle::GpuEmitter*>(pointer)->draw(view); };
        result.push_back(std::move(data));
    }
#else
    (void)key;
#endif
    return result;
}
}
