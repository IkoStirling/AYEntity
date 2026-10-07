#include <AYEntity/DeterministicCollision2D.h>
#include <algorithm>
#include <stdexcept>
#include <set>

namespace ayt::entity {
namespace {
using D=math::DetFloat32;
using V=math::DetVec2;
[[noreturn]] void invalid(){throw std::invalid_argument("deterministic collision 2D: invalid policy, body, history or unrepresentable motion");}
bool validConfig(const DetCollision2DConfig& c) {
    return c.bodySchema>=2 && c.bodySchema!=UINT32_MAX && c.historySchema>=2 && c.historySchema!=UINT32_MAX
        && c.bodySchema!=c.historySchema && c.systemId && c.eventType && c.maxBodies>=1 && c.maxBodies<=64
        && c.maxTriggerPairs>=1 && c.maxTriggerPairs<=(c.extended?60u:61u)
        && c.maxContactIterations>=1 && c.maxContactIterations<=256;
}
std::uint32_t profile(const DetCollision2DConfig& c){return c.extended?kDetCollision2DExtendedProfileVersion:kDetCollision2DProfileVersion;}
void validateBody(const DetCollisionBody2D& b,bool extended=false) {
    if(static_cast<std::uint32_t>(b.mode)>(extended?3u:2u) || !b.half.isFinite() || !b.offset.isFinite() || !b.velocity.isFinite()
        || b.half.x<D{} || b.half.y<D{}) invalid();
    if(b.mode!=DetBodyMode2D::Disabled && (!(b.half.x>D{}) || !(b.half.y>D{}) || !b.layer)) invalid();
    if(b.mode==DetBodyMode2D::Static && (!b.velocity.x.isZero() || !b.velocity.y.isZero())) invalid();
}
DetTypedStateSchema bodySchema(const DetCollision2DConfig& c) {
    return {c.bodySchema,profile(c),{{DetBodyMode,std::uint32_t{0}}, {DetBodyHalf,V{}},
        {DetBodyOffset,V{}},{DetBodyVelocity,V{}},{DetBodyLayer,std::uint32_t{1}},
        {DetBodyMask,UINT32_MAX},{DetBodyTrigger,false}}};
}
DetTypedStateSchema historySchema(const DetCollision2DConfig& c) {
    DetTypedStateSchema s{c.historySchema,profile(c),{{1,profile(c)},
        {2,c.maxBodies},{3,c.maxTriggerPairs},{4,c.eventType},{5,std::uint32_t{0}},{6,math::kDetGeometry2DProfileVersion}}};
    if(c.extended){s.fields.push_back({7,true});s.fields.push_back({8,c.maxContactIterations});}
    for(std::uint32_t i=0;i<c.maxTriggerPairs;++i){s.fields.push_back({10+2*i,DetEntityRef{}});s.fields.push_back({11+2*i,DetEntityRef{}});}
    return s;
}
bool masks(std::uint32_t al,std::uint32_t am,std::uint32_t bl,std::uint32_t bm){return (am&bl)!=0 && (bm&al)!=0;}
struct Body {SimEntityId id;DetCollisionBody2D data;V position;D z;math::DetAabb2 box;};
math::DetAabb2 bounds(const Body& b){auto box=math::DetAabb2::fromCenterHalf(b.position+b.data.offset,b.data.half);if(!box.hasArea())invalid();return box;}
bool blocks(const Body& a,const Body& b){return b.data.mode==DetBodyMode2D::Static && !a.data.trigger && !b.data.trigger
    && masks(a.data.layer,a.data.mask,b.data.layer,b.data.mask);}
D& axis(V& v,std::uint32_t i){return i==0?v.x:v.y;}
D outward(D v,bool positive) {
    const auto bits=v.bits();
    auto next=v.isZero()?D::fromBits(positive?1u:0x80000001u):D::fromBits(bits+((positive!=v.signBit())?1u:UINT32_MAX));
    if(!next.isFinite())invalid();return next;
}
void snap(Body& b,const Body& wall,const math::DetHit2D& hit) {
    const auto a=hit.axis;const bool positive=(a==0?hit.normal.x:hit.normal.y)>D{};
    const auto plane=a==0?(positive?wall.box.max.x:wall.box.min.x):(positive?wall.box.max.y:wall.box.min.y);
    auto& p=axis(b.position,a);const auto half=axis(b.data.half,a),offset=axis(b.data.offset,a);
    p=(positive?plane+half:plane-half)-offset;
    // Round each software operation separately. Move outward only when rebuilding
    // the represented edge rounds into the wall; reject after a bounded 16 ULPs.
    for(unsigned i=0;i<=16;++i){b.box=bounds(b);const auto edge=a==0?(positive?b.box.min.x:b.box.max.x):(positive?b.box.min.y:b.box.max.y);
        if(positive?edge>=plane:edge<=plane)return;
        if(i==16)invalid();p=outward(p,positive);
    }
}
void move(Body& b,const std::vector<Body>& bodies,D dt) {
    V remaining=b.data.velocity*dt;if(!remaining.isFinite())invalid();
    for(const auto& wall:bodies)if(blocks(b,wall) && b.box.interiorOverlap(wall.box))invalid();
    for(unsigned pass=0;pass<2;++pass) {
        std::optional<math::DetHit2D> best;const Body* target=nullptr;
        for(const auto& wall:bodies)if(blocks(b,wall)) {
            auto hit=math::detSweepAabb2D(b.box,remaining,wall.box);
            if(hit && hit->initialOverlap)invalid();
            if(hit && (!best || hit->fraction<best->fraction || (hit->fraction==best->fraction
                && (hit->axis<best->axis || (hit->axis==best->axis && wall.id<target->id))))){best=hit;target=&wall;}
        }
        if(!best){b.position=b.position+remaining;b.box=bounds(b);break;}
        b.position=b.position+remaining*best->fraction;snap(b,*target,*best);
        remaining=remaining*(D::fromInt(1)-best->fraction);axis(remaining,best->axis)=D{};axis(b.data.velocity,best->axis)=D{};
    }
    for(const auto& wall:bodies)if(blocks(b,wall) && b.box.interiorOverlap(wall.box))invalid();
}
#include "detail/DetCollision2DExtended.h"
std::vector<DetCollisionPair2D> history(DetTickContext& c,const DetCollision2DConfig& policy) {
    const auto schema=policy.historySchema;
    if(c.readGlobal<std::uint32_t>(schema,1)!=profile(policy) || c.readGlobal<std::uint32_t>(schema,2)!=policy.maxBodies
        || c.readGlobal<std::uint32_t>(schema,3)!=policy.maxTriggerPairs || c.readGlobal<std::uint32_t>(schema,4)!=policy.eventType
        || c.readGlobal<std::uint32_t>(schema,6)!=math::kDetGeometry2DProfileVersion)invalid();
    if(policy.extended && (!c.readGlobal<bool>(schema,7) || c.readGlobal<std::uint32_t>(schema,8)!=policy.maxContactIterations))invalid();
    const auto count=c.readGlobal<std::uint32_t>(schema,5);if(count>policy.maxTriggerPairs)invalid();
    std::vector<DetCollisionPair2D> result;
    for(std::uint32_t i=0;i<policy.maxTriggerPairs;++i){DetCollisionPair2D pair{c.readGlobal<DetEntityRef>(schema,10+2*i).value,c.readGlobal<DetEntityRef>(schema,11+2*i).value};
        if(i<count){if(!pair.first || pair.first>=pair.second || (!result.empty() && !(result.back()<pair)))invalid();result.push_back(pair);}
        else if(pair.first || pair.second)invalid();
    }return result;
}
void tick(DetTickContext& c,const DetCollision2DConfig& policy) {
    const auto previous=history(c,policy);std::vector<Body> bodies;
    for(const auto id:c.entities()) {
        const auto s=policy.bodySchema;
        DetCollisionBody2D data{static_cast<DetBodyMode2D>(c.read<std::uint32_t>(id,s,DetBodyMode)),
            c.read<V>(id,s,DetBodyHalf),c.read<V>(id,s,DetBodyOffset),c.read<V>(id,s,DetBodyVelocity),
            c.read<std::uint32_t>(id,s,DetBodyLayer),c.read<std::uint32_t>(id,s,DetBodyMask),c.read<bool>(id,s,DetBodyTrigger)};
        validateBody(data,policy.extended);if(data.mode==DetBodyMode2D::Disabled)continue;
        const auto p=c.pose(id).position;Body b{id,data,{p.x,p.y},p.z,{}};b.box=bounds(b);bodies.push_back(b);
    }
    if(bodies.size()>policy.maxBodies)invalid();
    std::sort(bodies.begin(),bodies.end(),[](const auto& a,const auto& b){return a.id<b.id;});
    auto solved=bodies;
    std::vector<TraceStep> trace;
    if(policy.extended)moveTogether(solved,c.dt(),policy.maxContactIterations,trace);
    else for(auto& b:solved)if(b.data.mode==DetBodyMode2D::Kinematic)move(b,bodies,c.dt());
    std::vector<DetCollisionProxy2D> proxies;for(const auto& b:solved)proxies.push_back({b.id,b.box,b.data.layer,b.data.mask,b.data.trigger});
    auto current=detCollisionPairs2D(proxies);
    std::erase_if(current,[&](const auto& p){auto trigger=[&](SimEntityId id){const auto it=std::lower_bound(solved.begin(),solved.end(),id,[](const Body& b,auto key){return b.id<key;});return it->data.trigger;};return !trigger(p.first)&&!trigger(p.second);});
    if(current.size()>policy.maxTriggerPairs)invalid();
    std::vector<std::vector<std::uint8_t>> events;
    std::size_t i=0,j=0;
    while(i<previous.size() || j<current.size()){
        if(j==current.size() || (i<previous.size() && previous[i]<current[j]))events.push_back(encodeDetTriggerEvent2D({DetTriggerPhase2D::Exit,previous[i++]}));
        else if(i==previous.size() || current[j]<previous[i])events.push_back(encodeDetTriggerEvent2D({DetTriggerPhase2D::Enter,current[j++]}));
        else{events.push_back(encodeDetTriggerEvent2D({DetTriggerPhase2D::Stay,current[j++]}));++i;}
    }
    if(policy.extended){
        const auto transient=transientPairs(trace,previous,current);
        if(transient.size()+current.size()>policy.maxTriggerPairs)invalid();
        // Sort all transitions by stable pair; Enter precedes Exit within a crossing.
        std::vector<DetTriggerEvent2D> ordered;
        for(const auto& bytes:events){DetTriggerEvent2D e;if(!decodeDetTriggerEvent2D(bytes,e))invalid();ordered.push_back(e);}
        for(const auto& pair:transient){ordered.push_back({DetTriggerPhase2D::Enter,pair});ordered.push_back({DetTriggerPhase2D::Exit,pair});}
        std::sort(ordered.begin(),ordered.end(),[](const auto& a,const auto& b){return a.pair<b.pair || (a.pair==b.pair && a.phase<b.phase);});
        events.clear();for(const auto& e:ordered)events.push_back(encodeDetTriggerEvent2D(e));
    }
    // All adapter semantics, shape arithmetic and capacities have passed. The
    // session remains responsible for sticky faults and explicit restore on error.
    for(const auto& b:solved)if(b.data.mode==DetBodyMode2D::Kinematic || b.data.mode==DetBodyMode2D::MovingObstacle){
        if(!c.pose(b.id).setPosition({b.position.x,b.position.y,b.z}))invalid();
        c.write(b.id,policy.bodySchema,DetBodyVelocity,b.data.velocity);
    }
    c.writeGlobal(policy.historySchema,5,static_cast<std::uint32_t>(current.size()));
    for(std::uint32_t slot=0;slot<policy.maxTriggerPairs;++slot){const auto p=slot<current.size()?current[slot]:DetCollisionPair2D{};
        c.writeGlobal(policy.historySchema,10+2*slot,DetEntityRef{p.first});c.writeGlobal(policy.historySchema,11+2*slot,DetEntityRef{p.second});}
    for(const auto& event:events)c.emit(policy.eventType,event);
}
}
std::vector<DetCollisionPair2D> detCollisionPairs2D(std::span<const DetCollisionProxy2D> proxies) {
    if(proxies.size()>64)invalid();auto sorted=std::vector(proxies.begin(),proxies.end());
    std::vector<SimEntityId> ids;for(const auto& p:sorted){if(!p.id || !p.box.hasArea() || !p.layer)invalid();ids.push_back(p.id);}
    std::sort(ids.begin(),ids.end());if(std::adjacent_find(ids.begin(),ids.end())!=ids.end())invalid();
    std::sort(sorted.begin(),sorted.end(),[](const auto& a,const auto& b){return a.box.min.x<b.box.min.x || (a.box.min.x==b.box.min.x && a.id<b.id);});
    std::vector<DetCollisionPair2D> result;
    for(std::size_t i=0;i<sorted.size();++i)for(std::size_t j=i+1;j<sorted.size();++j){const auto& a=sorted[i];const auto& b=sorted[j];
        if(b.box.min.x>=a.box.max.x)break;
        if(masks(a.layer,a.mask,b.layer,b.mask) && a.box.interiorOverlap(b.box))result.push_back({std::min(a.id,b.id),std::max(a.id,b.id)});
    }
    std::sort(result.begin(),result.end());return result;
}
std::vector<std::uint64_t> detCollisionBodyState2D(const DetCollision2DConfig& c,const DetCollisionBody2D& b) {
    if(!validConfig(c))invalid();validateBody(b,c.extended);const auto s=bodySchema(c);auto words=detStateDefaults(s);
    writeDetState(s,words,DetBodyMode,static_cast<std::uint32_t>(b.mode));writeDetState(s,words,DetBodyHalf,b.half);
    writeDetState(s,words,DetBodyOffset,b.offset);writeDetState(s,words,DetBodyVelocity,b.velocity);
    writeDetState(s,words,DetBodyLayer,b.layer);writeDetState(s,words,DetBodyMask,b.mask);writeDetState(s,words,DetBodyTrigger,b.trigger);return words;
}
bool installDetCollision2D(DeterministicSession& s,DetCollision2DConfig c) {
    if(!validConfig(c))return false;
    if(!(s.registerTypedSchema(bodySchema(c)) && s.registerTypedSchema(historySchema(c)) && s.registerGlobalState(c.historySchema)))return false;
    if(c.extended && !s.registerValidator(c.systemId,kDetCollision2DExtendedProfileVersion,
        [c,body=DetStateLayout(bodySchema(c)),history=DetStateLayout(historySchema(c))](const auto& state,std::string& e){
            try {validateExtendedState(state,c,body,history);return true;}catch(const std::exception& error){e=error.what();return false;}}))return false;
    return s.registerSystem(c.systemId,c.priority,[c](DetTickContext& context){tick(context,c);return true;});
}
std::vector<std::uint8_t> encodeDetTriggerEvent2D(const DetTriggerEvent2D& e) {
    const auto phase=static_cast<std::uint32_t>(e.phase);if(phase<1 || phase>3 || !e.pair.first || e.pair.first>=e.pair.second)invalid();
    std::vector<std::uint8_t> out;auto put=[&](std::uint64_t v,unsigned size){for(unsigned i=0;i<size;++i)out.push_back(static_cast<std::uint8_t>(v>>(i*8)));};
    put(kDetCollision2DProfileVersion,4);put(phase,4);put(e.pair.first,8);put(e.pair.second,8);return out;
}
bool decodeDetTriggerEvent2D(std::span<const std::uint8_t> bytes,DetTriggerEvent2D& out) {
    if(bytes.size()!=24)return false;auto get=[&](unsigned start,unsigned size){std::uint64_t v=0;for(unsigned i=0;i<size;++i)v|=std::uint64_t(bytes[start+i])<<(i*8);return v;};
    const auto phase=get(4,4);DetTriggerEvent2D decoded{static_cast<DetTriggerPhase2D>(phase),{get(8,8),get(16,8)}};
    if(get(0,4)!=kDetCollision2DProfileVersion || phase<1 || phase>3 || !decoded.pair.first || decoded.pair.first>=decoded.pair.second)return false;
    out=decoded;return true;
}
} // namespace ayt::entity
