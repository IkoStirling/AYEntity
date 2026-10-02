#include <AYEntity/ParticleSystem.h>
#include "ParticleGpuState.h"
#include <AYEntity.h>
#include <AYEntity/World.h>
#include <AYEntity/components/ParticleEmitterComponent.h>
#include <AYEntity/components/ParticleEffectComponent.h>
#include <AYEntity/components/ParticleSurface2DComponent.h>
#include <AYParticle/EffectResource.h>
#include <AYResource/ResourceManager.h>
#include <AYResource/AssetPath.h>
#include <AYEntity/components/TransformComponent.h>
#include <AYParticle/ParticleGeometry.h>
#include <AYRenderer/RendererSubSystem.h>
#include <AYGameLoop.h>
#include <AYMath/MathTransform.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace ayt::entity {
namespace {
particle::Pose poseOf(const Transform& transform, float alpha=1) {
    const auto matrix=math::Transform::getMatrix(transform.interpolatedPosition(alpha),
        transform.interpolatedRotation(alpha),transform.scale);
    return {{matrix(0,3),matrix(1,3),matrix(2,3)},
        {matrix(0,0),matrix(1,0),matrix(2,0)},
        {matrix(0,1),matrix(1,1),matrix(2,1)},
        {matrix(0,2),matrix(1,2),matrix(2,2)}};
}
float distance2(particle::Vec3 a, particle::Vec3 b) {
    const auto d=a-b; return d.x*d.x+d.y*d.y+d.z*d.z;
}
bool visible2D(const particle::ParticleSample& s,const render::OverlayCamera2D& camera,
               particle::Vec3 right,particle::Vec3 up) {
    if (!camera.valid) return true;
    const auto p=camera.view*math::FVector4(s.position.x,s.position.y,s.position.z,1);
    const auto clip=camera.projection*p;
    // A conservative projected bounding box of all rotated quad corners.
    const float radius=s.size*0.5f;
    const float wx=1.414214f*radius*(std::fabs(right.x)+std::fabs(up.x));
    const float wy=1.414214f*radius*(std::fabs(right.y)+std::fabs(up.y));
    const float padX=wx*std::fabs(camera.projection(0,0)*camera.view(0,0))
        +wy*std::fabs(camera.projection(0,0)*camera.view(0,1));
    const float padY=wx*std::fabs(camera.projection(1,1)*camera.view(1,0))
        +wy*std::fabs(camera.projection(1,1)*camera.view(1,1));
    return std::fabs(clip.x)<=std::fabs(clip.w)+padX
        && std::fabs(clip.y)<=std::fabs(clip.w)+padY;
}
class SceneParticleCollision2D final : public particle::CollisionQuery2D {
    struct Surface {
        float left,right,bottom,top,height;
        uint32_t mask,tag;
        bool ground,solid;
    };
    std::vector<Surface> surfaces;
public:
    explicit SceneParticleCollision2D(World& world) {
        for(auto* entity:world.query<Transform,ParticleSurface2DComponent>()) {
            const auto& transform=*entity->getComponent<Transform>();
            const auto& source=*entity->getComponent<ParticleSurface2DComponent>();
            const auto center=transform.position;
            const float hx=source.halfExtent.x,hy=source.halfExtent.y;
            if(!std::isfinite(center.x)||!std::isfinite(center.y)||!std::isfinite(hx)
                ||!std::isfinite(hy)||!std::isfinite(source.height)||hx<=0||hy<=0||source.height<0) continue;
            surfaces.push_back({center.x-hx,center.x+hx,center.y-hy,center.y+hy,source.height,
                static_cast<uint32_t>(source.collisionMask),static_cast<uint32_t>(source.surfaceTag),
                source.ground,source.solid});
        }
    }
    bool ground(particle::Vec3 anchor,float maxHeight,uint32_t mask,particle::CollisionHit2D& hit) const override {
        bool found=false;
        for(const auto& s:surfaces) {
            if(!s.ground || !(s.mask&mask) || s.height>maxHeight || anchor.x<s.left || anchor.x>s.right
                || anchor.y<s.bottom || anchor.y>s.top || (found && s.height<=hit.height)) continue;
            hit.position=anchor;hit.normal={0,1,0};hit.height=s.height;hit.surfaceTag=s.tag;hit.fraction=1;
            found=true;
        }
        return found;
    }
    bool sweep(particle::Vec3 from,particle::Vec3 to,uint32_t mask,particle::CollisionHit2D& hit) const override {
        bool found=false;
        const auto delta=to-from;
        for(const auto& s:surfaces) {
            if(!s.solid || !(s.mask&mask)) continue;
            if(from.x>=s.left&&from.x<=s.right&&from.y>=s.bottom&&from.y<=s.top) continue;
            float enter=0,leave=1;
            particle::Vec3 normal{};
            const auto slab=[&](float start,float move,float low,float high,particle::Vec3 lowNormal,particle::Vec3 highNormal) {
                if(std::fabs(move)<0.000001f) return start>=low&&start<=high;
                float first=(low-start)/move,second=(high-start)/move;
                auto firstNormal=lowNormal;
                if(first>second){std::swap(first,second);firstNormal=highNormal;}
                if(first>enter){enter=first;normal=firstNormal;}
                leave=std::min(leave,second);
                return enter<=leave;
            };
            if(!slab(from.x,delta.x,s.left,s.right,{-1,0,0},{1,0,0})
                ||!slab(from.y,delta.y,s.bottom,s.top,{0,-1,0},{0,1,0})
                ||enter<0||enter>1||enter>=leave|| (found&&enter>=hit.fraction)) continue;
            hit.position=from+delta*enter;hit.normal=normal;hit.fraction=enter;
            hit.height=s.height;hit.surfaceTag=s.tag;found=true;
        }
        return found;
    }
};
}
void ParticleSimulationSystem::onUpdate(float dt) {
    auto& world=World::instance();
    const SceneParticleCollision2D collision(world);
    for(auto& visual:_impactVisuals) visual.instance.update(dt,visual.pose);
    std::erase_if(_impactVisuals,[](const ImpactVisual& visual){return visual.instance.finished();});
    const auto collectImpacts=[&](const particle::ParticleInstance& source,int32_t layer,int32_t sortingKey) {
        const auto& c=source.effect().collision2D;
        if(c.impactSize<=0 || c.impactColor.a<=0) return;
        for(const auto& hit:source.impacts()) {
            if(_impactVisuals.size()>=256) break;
            particle::ParticleEffect pulse;
            pulse.capacity=1;pulse.burst=1;pulse.rate=0;pulse.duration=0.01f;pulse.looping=false;
            pulse.velocityMin=pulse.velocityMax=pulse.gravity={};
            pulse.lifetime={c.impactLifetime,c.impactLifetime};pulse.size={c.impactSize,c.impactSize};
            pulse.endSizeScale=1.8f;pulse.startColor=c.impactColor;
            pulse.endColor={c.impactColor.r,c.impactColor.g,c.impactColor.b,0};
            particle::Pose pose;pose.position=hit.position;
            if(c.mode==particle::CollisionMode2D::Ground)
                pose.position.y+=hit.height*c.visualHeightScale;
            ImpactVisual visual{particle::ParticleInstance(pulse),pose,layer,sortingKey};
            visual.instance.play();visual.instance.update(0,pose);
            _impactVisuals.push_back(std::move(visual));
        }
    };
    uint64_t used=0;
    uint64_t reserved=0;
    gpuEmitters=0;
    auto* renderSystem=dynamic_cast<ParticleRenderSystem*>(world.findSystemByName("ParticleRenderSystem"));
    auto gpu=renderSystem?renderSystem->gpuState():nullptr;
    if(gpu) gpu->beginFrame();
    particle::registerEffectResourceLoader();
    for(auto* entity:world.query<Transform,ParticleEffectComponent>()) {
        auto* effect=entity->getComponent<ParticleEffectComponent>();
        const auto path=resource::resolveAssetPath({},effect->effectPath);
        auto& resources=resource::ResourceManager::instance();
        auto loaded=effect->effectPath.empty() || resources.hasLoadFailed(path) ? nullptr : resources.load<particle::EffectResource>(path);
        if(loaded && loaded->isLoaded() && !resources.hasLoadFailed(path) && loaded->asset()) {
            if(effect->prepare(*loaded->asset())) used+=effect->runtime->liveParticles();
        } else {
            effect->error="Cannot load particle effect: "+effect->effectPath;
            if(effect->runtime) effect->runtime->stop(true);
            effect->usingGpu=false; effect->gpuCapacity=0;
        }
    }
    for (auto* entity:world.query<Transform,ParticleEmitterComponent>()) {
        auto* emitter=entity->getComponent<ParticleEmitterComponent>();
        if (emitter->prepare()) used+=emitter->runtime->particles().size();
    }
    const auto select=[&](auto& component,uint64_t key,const particle::EffectAsset& asset,int policy,size_t before) {
        const bool wasGpu=component.usingGpu;
        const auto other=used-before+reserved;
        const uint32_t allowance=other<maxParticles?uint32_t(maxParticles-other):0;
        component.usingGpu=gpu&&gpu->select(key,asset,policy,allowance,component.backendStatus);
        if(!gpu) component.backendStatus=policy==0?"CPU":"CPU fallback: scene renderer is unavailable";
        component.gpuCapacity=0;
        if(component.usingGpu) {
            for(const auto& source:asset.emitters) component.gpuCapacity+=particle::layerEffect(source).capacity;
            reserved+=component.gpuCapacity; gpuEmitters+=gpu->count(key);
            component.runtime->stop(true); used-=before;
        } else if(wasGpu) {
            if(component.playback.playing) component.runtime->play();
            if(component.playback.paused) component.runtime->pause();
        }
    };
    for(auto* entity:world.query<Transform,ParticleEmitterComponent>()) {
        auto& component=*entity->getComponent<ParticleEmitterComponent>();
        if(!component.runtime||!component.error.empty()) { component.usingGpu=false; continue; }
        particle::EffectAsset asset; asset.name="Emitter"; asset.duration=component.runtime->effect().duration;
        asset.emitters.push_back({"Emitter",{},0,component.runtime->effect()});
        select(component,component.playback.identity,asset,component.backend,component.runtime->particles().size());
    }
    for(auto* entity:world.query<Transform,ParticleEffectComponent>()) {
        auto& component=*entity->getComponent<ParticleEffectComponent>();
        if(!component.runtime||!component.error.empty()) { component.usingGpu=false; continue; }
        const auto& asset=component.runtime->asset();
        select(component,component.playback.identity,asset,
            component.backend==3?int(asset.backend):component.backend,component.runtime->liveParticles());
    }
    for (auto* entity:world.query<Transform,ParticleEmitterComponent>()) {
        auto* emitter=entity->getComponent<ParticleEmitterComponent>();
        if (!emitter->runtime || !emitter->error.empty()) continue;
        if(emitter->usingGpu) {
            gpu->update(emitter->playback.identity,dt,poseOf(*entity->getComponent<Transform>(),
                game::GameLoop::instance().getInterpolationFactor()),emitter->playback); continue;
        }
        const size_t before=emitter->runtime->particles().size();
        // Budget includes every other emitter's still-live particles.
        used-=before;
        const auto allowed=used+reserved<maxParticles ? static_cast<uint32_t>(maxParticles-used-reserved) : 0;
        emitter->runtime->update(dt,poseOf(*entity->getComponent<Transform>(),
            game::GameLoop::instance().getInterpolationFactor()),
            allowed>before ? allowed-static_cast<uint32_t>(before) : 0,&collision);
        collectImpacts(*emitter->runtime,emitter->layer,emitter->sortingKey);
        used+=emitter->runtime->particles().size();
    }
    for(auto* entity:world.query<Transform,ParticleEffectComponent>()) {
        auto* effect=entity->getComponent<ParticleEffectComponent>();
        if(!effect->runtime || !effect->error.empty()) continue;
        if(effect->usingGpu) {
            gpu->update(effect->playback.identity,dt,poseOf(*entity->getComponent<Transform>(),
                game::GameLoop::instance().getInterpolationFactor()),effect->playback); continue;
        }
        const size_t before=effect->runtime->liveParticles(); used-=before;
        const auto allowed=used+reserved<maxParticles ? static_cast<uint32_t>(maxParticles-used-reserved) : 0;
        effect->runtime->update(dt,poseOf(*entity->getComponent<Transform>(),
            game::GameLoop::instance().getInterpolationFactor()),
            allowed>before ? allowed-static_cast<uint32_t>(before) : 0,&collision);
        for(const auto& child:effect->runtime->instances())
            collectImpacts(child,effect->layer,effect->sortingKey);
        used+=effect->runtime->liveParticles();
    }
    liveParticles=static_cast<uint32_t>(std::min<uint64_t>(used,UINT32_MAX));
    gpuReservedParticles=uint32_t(reserved);
    if(gpu) gpu->endFrame();
}
ParticleRenderSystem::~ParticleRenderSystem() {
    if (auto* subsystem=render::RendererSubSystem::findRegistered())
        subsystem->renderer().destroyMesh(_quad);
}
void ParticleRenderSystem::onStart() {
    if (auto* renderer=render::RendererSubSystem::findRegistered()) {
        World* owner=&World::instance();
        auto state=std::make_shared<ParticleGpuState>(); _gpuState=state;
        renderer->addSceneBuilderForOwner(owner,[this,owner,state](render::RenderScene& scene) {
            if (&World::instance()==owner) buildRenderScene(scene);
        });
    }
}
void ParticleRenderSystem::buildRenderScene(render::RenderScene& scene) {
    _batches.clear();
    auto* subsystem=render::RendererSubSystem::findRegistered();
    if (!subsystem) return;
    auto& renderer=subsystem->renderer();
    if (!renderer.isInitialized()) return;
    if (!_quad.isValid()) _quad=renderer.createUnitQuad();
    if (!_quad.isValid()) return;
    math::Float4x4 view,projection;
    const bool have3D=renderer.mainCameraMatrices(view,projection);
    const auto eye=renderer.mainCameraPosition();
    const particle::Vec3 cameraPosition{eye.x,eye.y,eye.z};
    const float alpha=game::GameLoop::instance().getInterpolationFactor();
    auto camera=scene.overlayCamera2D();
    // The editor applies its viewport override after scene builders; cull against it now.
    (void)subsystem->overlayCamera2DOverride(camera);
    const auto drawGpu=[&](uint64_t key,int32_t layer,int32_t sortingKey) {
        auto gpu=_gpuState.lock(); if(!gpu) return;
        for(auto& data:gpu->draws(key)) {
            if(data.world3D ? !have3D
                : camera.valid&&!(camera.layerMask&(1u<<(uint32_t(layer)&31)))) continue;
            _batches.emplace_back(); auto& batch=_batches.back(); batch.geometry=std::move(data);
            batch.payload.packedSortKey=(uint32_t(layer)&255)<<24|(uint32_t(sortingKey)&0xFFFFFF);
            render::DrawItem item; item.particleBatch=&batch.geometry; item.payload=&batch.payload;
            if(batch.geometry.world3D) {
                item.payload=nullptr; item.sortKey=sortingKey;
                item.world(0,3)=batch.geometry.sortPosition[0];
                item.world(1,3)=batch.geometry.sortPosition[1];
                item.world(2,3)=batch.geometry.sortPosition[2];
            }
            item.shadowFlags=render::ShadowFlags::None; scene.add(item);
        }
    };
    const auto drawInstance=[&](const particle::ParticleInstance& instance,
                                const particle::Pose& pose,int32_t layer,int32_t sortingKey) {
        const auto& effect=instance.effect();
        const bool world3D=effect.dimension==particle::Dimension::ThreeD;
        if (world3D && !have3D) return;
        if (!world3D && camera.valid
            && !(camera.layerMask&(1u<<(static_cast<uint32_t>(layer)&31)))) return;
        particle::Vec3 right{1,0,0},up{0,1,0};
        if (world3D) {
            right={view(0,0),view(0,1),view(0,2)};
            up={view(1,0),view(1,1),view(1,2)};
        } else if (effect.space==particle::Space::Local) { right=pose.right; up=pose.up; }
        std::vector<particle::ParticleSample> samples;
        samples.reserve(instance.particles().size());
        for (const auto& p:instance.particles()) {
            auto s=instance.sample(p,pose);
            if (!std::isfinite(s.position.x) || !std::isfinite(s.position.y)
                || !std::isfinite(s.position.z) || !std::isfinite(s.size)
                || !std::isfinite(s.rotation) || !std::isfinite(s.color.r)
                || !std::isfinite(s.color.g) || !std::isfinite(s.color.b)
                || !std::isfinite(s.color.a)) continue;
            if (s.color.a<=0 || s.size<=0) continue;
            if (!world3D && !visible2D(s,camera,right,up)) continue;
            samples.push_back(s);
        }
        if (samples.empty()) return;
        std::stable_sort(samples.begin(),samples.end(),[&](const auto& a,const auto& b) {
            return world3D && effect.blend==particle::Blend::Alpha
                ? distance2(a.position,cameraPosition)>distance2(b.position,cameraPosition)
                : a.birthId<b.birthId;
        });
        const std::string key="particle:"+effect.texturePath+":"+std::to_string(int(effect.blend));
        auto& material=_materials[key];
        if (!material.texture.isValid()) {
            if (!effect.texturePath.empty()) material.texture=renderer.loadTexture(effect.texturePath);
            else {
                uint8_t pixels[32*32*4];
                for (int y=0;y<32;++y) for (int x=0;x<32;++x) {
                    const float dx=(x+0.5f-16)/16,dy=(y+0.5f-16)/16;
                    const int i=(y*32+x)*4;
                    pixels[i]=pixels[i+1]=pixels[i+2]=255;
                    pixels[i+3]=static_cast<uint8_t>(255*std::clamp(1-dx*dx-dy*dy,0.0f,1.0f));
                }
                material.texture=renderer.createTextureFromRgba8(32,32,pixels,"particle:default-soft-disc");
            }
        }
        if (!material.texture.isValid()) return;
        if (!material.material.isValid()) {
            material.material=renderer.createMaterialFromPhoskia(render::kParticlePhoskiaSource,key);
            renderer.setMaterialTexture(material.material,"albedoMap",material.texture);
            renderer.setMaterialBlendMode(material.material,
                effect.blend==particle::Blend::Additive ? render::BlendMode::Additive : render::BlendMode::Alpha);
            renderer.setMaterialSurfaceProperties(material.material,2,0,true);
            renderer.setMaterialModel(material.material,render::MaterialModel::Unlit);
        }
        if (!material.material.isValid()) return;
        for (size_t offset=0;offset<samples.size();offset+=16383) {
            const auto count=std::min<size_t>(16383,samples.size()-offset);
            auto subset=std::span<const particle::ParticleSample>(samples).subspan(offset,count);
            auto geometry=particle::buildGeometry(subset,effect,right,up);
            _batches.emplace_back();
            auto& batch=_batches.back();
            batch.geometry.vertices.resize(geometry.vertices.size());
            static_assert(sizeof(particle::Vertex)==sizeof(render::ParticleVertex));
            std::memcpy(batch.geometry.vertices.data(),geometry.vertices.data(),geometry.vertices.size()*sizeof(particle::Vertex));
            batch.geometry.indices=std::move(geometry.indices);
            batch.payload.packedSortKey=(static_cast<uint32_t>(layer)&255)<<24
                | (static_cast<uint32_t>(sortingKey)&0xFFFFFF);
            render::DrawItem item;
            item.mesh=_quad; item.material=material.material;
            item.shadowFlags=render::ShadowFlags::None;
            item.particleBatch=&batch.geometry;
            if (world3D) {
                particle::Vec3 center{};
                for (const auto& s:subset) center=center+s.position*(1.0f/static_cast<float>(count));
                item.world(0,3)=center.x; item.world(1,3)=center.y; item.world(2,3)=center.z;
                for (auto& v:batch.geometry.vertices) { v.x-=center.x; v.y-=center.y; v.z-=center.z; }
                item.sortKey=sortingKey;
            } else item.payload=&batch.payload;
            scene.add(item);
        }
    };
    for(auto* entity:World::instance().query<Transform,ParticleEmitterComponent>()) {
        const auto* emitter=entity->getComponent<ParticleEmitterComponent>();
        if(emitter->visible && emitter->runtime && emitter->error.empty()) {
            if(emitter->usingGpu) drawGpu(emitter->playback.identity,emitter->layer,emitter->sortingKey);
            else drawInstance(*emitter->runtime,poseOf(*entity->getComponent<Transform>(),alpha),emitter->layer,emitter->sortingKey);
        }
    }
    for(auto* entity:World::instance().query<Transform,ParticleEffectComponent>()) {
        const auto* effect=entity->getComponent<ParticleEffectComponent>();
        if(!effect->visible || !effect->runtime || !effect->error.empty()) continue;
        if(effect->usingGpu) { drawGpu(effect->playback.identity,effect->layer,effect->sortingKey); continue; }
        const auto pose=poseOf(*entity->getComponent<Transform>(),alpha);
        const auto& instances=effect->runtime->instances();
        for(size_t i=0;i<instances.size();++i)
            drawInstance(instances[i],effect->runtime->emitterPose(i,pose),effect->layer,effect->sortingKey);
    }
    if(auto* simulation=dynamic_cast<ParticleSimulationSystem*>(World::instance().findSystemByName("ParticleSimulationSystem")))
        for(const auto& visual:simulation->impactVisuals())
            drawInstance(visual.instance,visual.pose,visual.layer,visual.sortingKey);
}
}
