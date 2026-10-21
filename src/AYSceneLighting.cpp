#include <AYEntity/SceneLighting.h>
#include <AYEntity.h>
#include <AYEntity/World.h>
#include <AYEntity/EntityImpl.h>
#include <AYEntity/components/LightComponent.h>
#include <AYRenderer/RenderScene.h>
#include <array>
#include <algorithm>
#include <cmath>

namespace ayt::entity {
SceneLightingBuildResult collectSceneLighting(World& world, render::SceneLights& output, float alpha)
{
    SceneLightingBuildResult result;render::SceneLights next;
    struct Candidate {render::Light light;int32_t priority=0;const std::string* identity=nullptr;};
    std::array<Candidate,render::kMaxSceneLights> selected{};std::size_t count=0;uint32_t eligible=0;
    const auto earlier=[](const Candidate& a,const Candidate& b){return a.priority>b.priority
        ||(a.priority==b.priority&&*a.identity<*b.identity);};
    for(auto* object:world.query<LightComponent>()){
        const auto* component=object->getComponent<LightComponent>();++result.authored;
        if(!component->enabled){++result.disabled;continue;}
        math::FVector3 position;math::FQuaternion rotation;
        if(!component->valid()||!std::isfinite(alpha)||!object->worldPose(position,rotation,std::clamp(alpha,0.f,1.f))){++result.invalid;continue;}
        const auto finite=[](const math::FVector3& v){return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z);};
        auto direction=rotation.toMatrix().transformDirection({0,0,1});
        if(!finite(position)||!finite(direction)||direction.lengthSq()<1e-10f){++result.invalid;continue;}
        direction=direction.normalize();Candidate candidate;candidate.priority=component->priority;candidate.identity=&object->persistentId();
        auto& light=candidate.light;
        switch(SceneLightType(component->type)){
        case SceneLightType::Directional: light=render::Light::directional(direction,component->color*component->intensity);break;
        case SceneLightType::Point: light=render::Light::point(position,component->range,component->intensity,component->color);break;
        case SceneLightType::Spot: light=render::Light::spot(position,direction,component->range,component->intensity,
            std::cos(component->innerAngleDegrees*.01745329251994329577f),
            std::cos(component->outerAngleDegrees*.01745329251994329577f),component->color);break;
        }
        light.castShadow=component->castShadow;light.shadowBias=component->shadowBias;
        ++eligible;std::size_t at=0;while(at<count&&!earlier(candidate,selected[at]))++at;
        if(at<selected.size()){
            const auto limit=std::min(count,selected.size()-1);
            for(auto i=limit;i>at;--i)selected[i]=selected[i-1];selected[at]=candidate;
            count=std::min(count+1,selected.size());
        }
    }
    for(std::size_t i=0;i<count;++i)next.add(selected[i].light);
    result.emitted=uint32_t(count);result.overflow=eligible-result.emitted;output=next;return result;
}
} // namespace ayt::entity
