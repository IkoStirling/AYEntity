#include <AYEntity/components/ParticleEmitterComponent.h>
#include <AYEntity/components/ParticleEffectComponent.h>
#include <AYEntity/components/ParticleSurface2DComponent.h>
#include <AYEntity/ComponentRegistration.h>
#include <AYParticle/EffectAssetIO.h>
#include <AYEntity/EntityParticleIntegrationModule.h>
namespace ayt::entity {
AY_FINALIZE_REGISTRATION_METADATA(ParticleEmitterComponent)
AY_FINALIZE_REGISTRATION_METADATA(ParticleEffectComponent)
AY_FINALIZE_REGISTRATION_METADATA(ParticleSurface2DComponent)
namespace {
particle::Vec3 v3(const math::FVector3& v) { return {v.x,v.y,v.z}; }
particle::Color color(const math::FVector4& v) { return {v.x,v.y,v.z,v.w}; }
}
particle::ParticleEffect ParticleEmitterComponent::effect() const {
    particle::ParticleEffect e;
    e.texturePath=texturePath; e.capacity=static_cast<uint32_t>(capacity);
    e.burst=static_cast<uint32_t>(burst); e.seed=static_cast<uint32_t>(seed);
    e.rate=rate; e.duration=duration; e.looping=looping;
    e.dimension=static_cast<particle::Dimension>(dimension);
    e.shape=static_cast<particle::Shape>(shape);
    e.space=static_cast<particle::Space>(simulationSpace);
    e.blend=static_cast<particle::Blend>(blend);
    e.backend=static_cast<particle::Backend>(backend);
    e.extent=v3(extent); e.velocityMin=v3(velocityMin); e.velocityMax=v3(velocityMax);
    e.gravity=v3(gravity); e.drag=drag;
    e.lifetime={lifetime.x,lifetime.y}; e.size={size.x,size.y};
    e.rotation={rotation.x,rotation.y}; e.angularVelocity={angularVelocity.x,angularVelocity.y};
    e.endSizeScale=endSizeScale; e.startColor=color(startColor); e.endColor=color(endColor);
    for(const auto& k:sizeCurve) e.sizeCurve.push_back({k.x,k.y});
    for(const auto& k:opacityCurve) e.opacityCurve.push_back({k.x,k.y});
    for(const auto& k:colorGradient) e.colorGradient.push_back({k.time,color(k.color)});
    e.columns=static_cast<uint32_t>(columns); e.rows=static_cast<uint32_t>(rows);
    e.firstFrame=static_cast<uint32_t>(firstFrame); e.frameCount=static_cast<uint32_t>(frameCount);
    e.framesPerSecond=framesPerSecond;
    particle::decodeParticleExtensions(propertySources,e); return e;
}
void ParticleEmitterComponent::setEffect(const particle::ParticleEffect& e) {
    texturePath=e.texturePath; capacity=static_cast<int32_t>(e.capacity);
    burst=static_cast<int32_t>(e.burst); seed=static_cast<int32_t>(e.seed);
    rate=e.rate; duration=e.duration; looping=e.looping;
    dimension=static_cast<int32_t>(e.dimension); shape=static_cast<int32_t>(e.shape);
    simulationSpace=static_cast<int32_t>(e.space); blend=static_cast<int32_t>(e.blend);
    extent={e.extent.x,e.extent.y,e.extent.z};
    velocityMin={e.velocityMin.x,e.velocityMin.y,e.velocityMin.z};
    velocityMax={e.velocityMax.x,e.velocityMax.y,e.velocityMax.z};
    gravity={e.gravity.x,e.gravity.y,e.gravity.z}; drag=e.drag;
    lifetime={e.lifetime.min,e.lifetime.max}; size={e.size.min,e.size.max};
    rotation={e.rotation.min,e.rotation.max}; angularVelocity={e.angularVelocity.min,e.angularVelocity.max};
    endSizeScale=e.endSizeScale;
    startColor={e.startColor.r,e.startColor.g,e.startColor.b,e.startColor.a};
    endColor={e.endColor.r,e.endColor.g,e.endColor.b,e.endColor.a};
    backend=static_cast<int32_t>(e.backend);
    sizeCurve.clear(); opacityCurve.clear(); colorGradient.clear();
    for(const auto& k:e.sizeCurve) sizeCurve.push_back({k.time,k.value});
    for(const auto& k:e.opacityCurve) opacityCurve.push_back({k.time,k.value});
    for(const auto& k:e.colorGradient) {
        ParticleGradientKey key; key.time=k.time; key.color={k.color.r,k.color.g,k.color.b,k.color.a};
        colorGradient.push_back(std::move(key));
    }
    columns=static_cast<int32_t>(e.columns); rows=static_cast<int32_t>(e.rows);
    firstFrame=static_cast<int32_t>(e.firstFrame); frameCount=static_cast<int32_t>(e.frameCount);
    framesPerSecond=e.framesPerSecond;
    std::string encoded; if(particle::encodeParticleExtensions(e,encoded)) propertySources=std::move(encoded);
    else propertySources="invalid";
}
bool ParticleEmitterComponent::prepare() {
    auto next=effect();
    if(!particle::decodeParticleExtensions(propertySources,next,&error)) { if(runtime)runtime->stop(true);return false; }
    if (backend<0 || backend>2 || dimension<0 || dimension>1 || shape<0 || shape>4 || simulationSpace<0 || simulationSpace>1 || blend<0 || blend>1 || !next.validate(&error)) {
        if (error.empty()) error="Invalid particle mode";
        if (runtime) runtime->stop(true);
        return false;
    }
    if (!runtime || !(runtime->effect()==next)) {
        runtime=std::make_unique<particle::ParticleInstance>(next);
        if (autoPlay) { runtime->play(); playback.play(); }
        else playback.stop(true);
    }
    error.clear(); return true;
}
bool ParticleEffectComponent::prepare(const particle::EffectAsset& asset) {
    if(backend<0||backend>3) { error="Invalid particle backend"; if(runtime) runtime->stop(true); return false; }
    const bool recovering=!error.empty();
    if(!asset.validate(&error)) { if(runtime) runtime->stop(true); return false; }
    if(!runtime || !(runtime->asset()==asset)) {
        runtime=std::make_unique<particle::EffectInstance>(asset);
        if(autoPlay) { runtime->play(); playback.play(); }
        else playback.stop(true);
    }
    if(recovering && autoPlay) { runtime->play(); playback.play(); }
    error.clear(); return true;
}
ComponentRegistryResult registerEntityParticleComponents(ComponentRegistry& registry) {
    const auto result=registerSceneComponent<ParticleEmitterComponent>(registry,
        "ParticleEmitterComponent","Particle Emitter","Effects");
    if(!result) return result;
    const auto effect=registerSceneComponent<ParticleEffectComponent>(registry,
        "ParticleEffectComponent","Particle Effect","Effects");
    if(!effect) return effect;
    return registerSceneComponent<ParticleSurface2DComponent>(registry,
        "ParticleSurface2DComponent","Particle Surface 2D","Effects");
}
}
