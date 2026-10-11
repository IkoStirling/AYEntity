// design reference: AYEntity/design.md Stage33 registered direct actions.
#include <AYEntity/DeterministicActions2D.h>
#include <AYEntity/DeterministicCollisionQuery2D.h>
#include "detail/DetSessionWire.h"
#include "detail/DetActions2DIdentity.h"
#include "detail/DetPathMotion2DIdentity.h"
#include <algorithm>
#include <map>
#include <set>
#include <stdexcept>

namespace ayt::entity {
namespace {
using D=math::DetFloat32;using V=math::DetVec2;
constexpr std::size_t policyChunkBytes=1000,maxPolicyBytes=8000;
std::uint32_t profile(const DetActions2DConfig& c){return detail::actionsIdentity2D(c).first;}
std::uint64_t logicHash(const DetActions2DConfig& c){return detail::actionsIdentity2D(c).second;}
[[noreturn]] void invalid(){throw std::invalid_argument("deterministic actions 2D: invalid configuration, policy or registered state");}
bool equal(V a,V b){return a.x.bits()==b.x.bits() && a.y.bits()==b.y.bits();}
struct Writer {
    std::vector<std::uint8_t> bytes;
    void put(std::uint64_t value,unsigned size){for(unsigned i=0;i<size;++i)bytes.push_back(static_cast<std::uint8_t>(value>>(i*8)));}
    void u32(std::uint32_t value){put(value,4);}void u64(std::uint64_t value){put(value,8);}
    void flag(bool value){put(value?1:0,1);}void scalar(D value){u32(value.bits());}
    void vector(V value){scalar(value.x);scalar(value.y);}
};
void collisionBytes(Writer& w,const DetCollision2DConfig& c,bool explicitRespawn=false){
    w.u32(c.bodySchema);w.u32(c.historySchema);w.u32(c.systemId);w.u32(static_cast<std::uint32_t>(c.priority));
    w.u32(c.eventType);w.u32(c.maxBodies);w.u32(c.maxTriggerPairs);w.flag(c.extended);w.u32(c.maxContactIterations);
    w.flag(c.oneWayPlatforms);w.flag(c.stepUp);w.scalar(c.maxStepHeight);w.u32(static_cast<std::uint32_t>(c.motionPolicy));
    w.flag(c.staticSlopes);w.flag(c.movingSlopes);w.flag(c.continuousTerrain);
    // Actions2 emits the true respawn extension; Actions3 emits it explicitly
    // even when false. Actions1/2 retain the complete old nested sequence.
    if(explicitRespawn || c.characterRespawn)w.flag(c.characterRespawn);
}
void pathBytes(Writer& w,const DetPathMotion2DConfig& p,bool explicitRole=false){
    // Only Actions3 adds these explicit role markers, including false for mixed
    // Path1 targets. The Actions1/2 literal sequence is unchanged.
    if(explicitRole){w.u32(detail::pathMotionIdentity2D(p).first);w.flag(p.movingTrigger);}
    collisionBytes(w,p.collision,explicitRole);w.u32(p.stateSchema);w.u32(p.policySchema);w.u32(p.preSystemId);w.u32(p.postSystemId);
    w.u32(p.validatorId);w.u32(p.logicProfileId);w.scalar(p.tickPeriod);w.u32(static_cast<std::uint32_t>(p.playback));
    w.u32(static_cast<std::uint32_t>(p.waypoints.size()));
    for(const auto& point:p.waypoints){w.vector(point.offset);w.u32(point.travelTicks);w.u32(point.dwellTicks);}
}
std::vector<std::uint8_t> collisionBytes(const DetCollision2DConfig& c){Writer w;collisionBytes(w,c);return std::move(w.bytes);}
std::vector<std::uint8_t> pathBytes(const DetPathMotion2DConfig& p,bool explicitRole=false){Writer w;pathBytes(w,p,explicitRole || p.movingTrigger);return std::move(w.bytes);}
DetTypedStateSchema stateSchema(const DetActions2DConfig& c){return {c.stateSchema,profile(c),{
    {DetActionsTotalCount,std::uint64_t{}},{DetActionsBudgetTick,std::uint64_t{}},{DetActionsTickCount,std::uint32_t{}},
    {DetActionsLastCode,std::uint32_t{}},{DetActionsLastTarget,DetEntityRef{}},{DetActionsLastKind,std::uint32_t{}},
    {DetActionsLastTick,std::uint64_t{}}}};}
void descriptor(const detwire::ManifestLayout& m,const DetTypedStateSchema& s){
    if(m.schemaVersions.at(s.id)!=s.version)invalid();const auto& fields=m.schemas.at(s.id);
    if(fields.size()!=s.fields.size())invalid();for(std::size_t i=0;i<fields.size();++i){const auto& actual=fields[i];const auto& expected=s.fields[i];
        if(actual.id!=expected.id || actual.type!=detStateType(expected.initial) || actual.width!=encodeDetStateValue(expected.initial).size())invalid();}
}
struct Plan {
    std::vector<std::uint8_t> bytes;
    DetTypedStateSchema state,body;
    std::vector<DetTypedStateSchema> policies;
};
Plan plan(const DetActions2DConfig& c){
    if(c.stateSchema<2 || c.stateSchema==UINT32_MAX || c.policySchemaBase<2 || c.policySchemaBase==UINT32_MAX
        || !c.validatorId || !c.logicProfileId || c.validatorId==c.collision.systemId
        || !c.maxActionsPerTick || c.maxActionsPerTick>1024 || c.targets.size()>8)invalid();
    Plan p;p.state=stateSchema(c);p.body=detCollisionBodySchema2D(c.collision);
    Writer w;w.u32(profile(c));collisionBytes(w,c.collision,profile(c)==kDetActions2DMovingTriggerProfileVersion);
    w.u32(c.stateSchema);w.u32(c.policySchemaBase);w.u32(c.validatorId);w.u32(c.logicProfileId);
    w.u32(c.maxActionsPerTick);w.u32(static_cast<std::uint32_t>(c.targets.size()));
    SimEntityId previous=0;
    for(const auto& target:c.targets){
        if(!target.actor || target.actor<=previous || !target.collisionMask)invalid();previous=target.actor;
        w.u64(target.actor);w.u32(target.collisionMask);w.flag(target.collisionControl);w.flag(bool(target.path));
        if(target.path){
            const auto& path=*target.path;
            // The public factory validates all path fields without registration.
            (void)detPathMotionState2D(path);
            (void)detPathMotionPolicySchema2D(path);
            if(collisionBytes(path.collision)!=collisionBytes(c.collision)
                || path.validatorId==c.validatorId || path.logicProfileId==c.logicProfileId)invalid();
            for(const auto& earlier:c.targets){if(earlier.actor>=target.actor)break;
                if(earlier.path && (earlier.path->stateSchema==path.stateSchema || earlier.path->policySchema==path.policySchema)
                    && pathBytes(*earlier.path,profile(c)==kDetActions2DMovingTriggerProfileVersion)!=pathBytes(path,profile(c)==kDetActions2DMovingTriggerProfileVersion))invalid();}
            pathBytes(w,path,profile(c)==kDetActions2DMovingTriggerProfileVersion);
        }
    }
    p.bytes=std::move(w.bytes);if(p.bytes.empty() || p.bytes.size()>maxPolicyBytes)invalid();
    const auto chunks=(p.bytes.size()+policyChunkBytes-1)/policyChunkBytes;
    if(!chunks || chunks>8 || c.policySchemaBase>UINT32_MAX-chunks)invalid();
    std::set<std::uint32_t> schemas{c.collision.bodySchema,c.collision.historySchema};
    if(!schemas.insert(c.stateSchema).second)invalid();
    for(std::size_t chunk=0;chunk<chunks;++chunk){
        const auto id=c.policySchemaBase+static_cast<std::uint32_t>(chunk);if(!schemas.insert(id).second)invalid();
        DetTypedStateSchema policy{id,profile(c),{{1,static_cast<std::uint32_t>(chunk)},
            {2,static_cast<std::uint32_t>(chunks)},{3,static_cast<std::uint32_t>(p.bytes.size())}}};
        const auto begin=chunk*policyChunkBytes,end=std::min(begin+policyChunkBytes,p.bytes.size());
        for(std::size_t offset=begin;offset<end;offset+=8){std::uint64_t value=0;
            for(std::size_t b=0;b<8 && offset+b<end;++b)value|=std::uint64_t(p.bytes[offset+b])<<(b*8);
            policy.fields.push_back({static_cast<std::uint32_t>(4+(offset-begin)/8),value});}
        p.policies.push_back(std::move(policy));
    }
    std::map<std::uint32_t,std::vector<std::uint8_t>> pathOwners;
    for(const auto& target:c.targets)if(target.path){const auto bytes=pathBytes(*target.path,profile(c)==kDetActions2DMovingTriggerProfileVersion);
        for(const auto id:{target.path->stateSchema,target.path->policySchema}){
            if(schemas.contains(id))invalid();const auto [it,inserted]=pathOwners.emplace(id,bytes);
            if(!inserted && it->second!=bytes)invalid();}
    }
    (void)detStateDefaults(p.state);return p;
}
void policyMatches(DetTickContext& context,const Plan& p){
    for(const auto& schema:p.policies)for(const auto& field:schema.fields)std::visit([&]<class T>(const T& expected){
        if(encodeDetStateValue(context.readGlobal<T>(schema.id,field.id))!=encodeDetStateValue(expected))invalid();},field.initial);
}
const DetActionTarget2D* find(const DetActions2DConfig& c,SimEntityId id){
    const auto it=std::lower_bound(c.targets.begin(),c.targets.end(),id,[](const auto& target,auto value){return target.actor<value;});
    return it!=c.targets.end() && it->actor==id?&*it:nullptr;
}
struct Record {std::uint64_t total=0,budgetTick=0,lastTick=0;std::uint32_t tickCount=0,code=0,kind=0;SimEntityId target=0;};
void validateRecord(const Record& r,const DetActions2DConfig& c,std::uint64_t maximumStamp){
    if(!r.total){if(r.budgetTick || r.lastTick || r.tickCount || r.code || r.kind || r.target)invalid();return;}
    if(!r.lastTick || r.lastTick>maximumStamp || r.budgetTick!=r.lastTick || !r.tickCount || r.tickCount>c.maxActionsPerTick
        || r.total<r.tickCount || r.kind<1 || r.kind>7 || r.code<1 || r.code>8
        || (r.code==8 && r.tickCount!=c.maxActionsPerTick))invalid();
    if(r.code==3?find(c,r.target)!=nullptr:(r.code!=8 && !find(c,r.target)))invalid();
}
Record readRecord(DetTickContext& context,const DetActions2DConfig& c){const auto s=c.stateSchema;
    return {context.readGlobal<std::uint64_t>(s,DetActionsTotalCount),context.readGlobal<std::uint64_t>(s,DetActionsBudgetTick),
        context.readGlobal<std::uint64_t>(s,DetActionsLastTick),context.readGlobal<std::uint32_t>(s,DetActionsTickCount),
        context.readGlobal<std::uint32_t>(s,DetActionsLastCode),context.readGlobal<std::uint32_t>(s,DetActionsLastKind),
        context.readGlobal<DetEntityRef>(s,DetActionsLastTarget).value};
}
DetCollisionBody2D readBody(DetTickContext& context,SimEntityId id,const DetCollision2DConfig& c){const auto s=c.bodySchema;
    DetCollisionBody2D body{static_cast<DetBodyMode2D>(context.read<std::uint32_t>(id,s,DetBodyMode)),
        context.read<V>(id,s,DetBodyHalf),context.read<V>(id,s,DetBodyOffset),context.read<V>(id,s,DetBodyVelocity),
        context.read<std::uint32_t>(id,s,DetBodyLayer),context.read<std::uint32_t>(id,s,DetBodyMask),context.read<bool>(id,s,DetBodyTrigger)};
    if(c.oneWayPlatforms){body.oneWay=context.read<bool>(id,s,DetBodyOneWay);body.ignoredOneWay=context.read<DetEntityRef>(id,s,DetBodyIgnoredOneWay);}
    if(c.staticSlopes)body.shape=static_cast<DetCollisionShape2D>(context.read<std::uint32_t>(id,s,DetBodyShape));return body;
}
bool collisionSupported(const DetCollisionBody2D& b){return b.shape==DetCollisionShape2D::Aabb && !b.oneWay
    && b.mode!=DetBodyMode2D::Disabled && (b.trigger || b.mode==DetBodyMode2D::Static || b.mode==DetBodyMode2D::MovingObstacle);}
DetActionCode2D apply(DetTickContext& context,const DetActions2DConfig& c,const DetActionTarget2D& target,DetActionKind2D kind){
    if(kind==DetActionKind2D::CollisionEnable || kind==DetActionKind2D::CollisionDisable){
        if(!target.collisionControl)return DetActionCode2D::Unsupported;
        const auto body=readBody(context,target.actor,c.collision);if(!collisionSupported(body))return DetActionCode2D::Unsupported;
        const auto mask=kind==DetActionKind2D::CollisionEnable?target.collisionMask:0u;
        if(body.mask==mask)return DetActionCode2D::Unchanged;
        if(body.mask!=0 && body.mask!=target.collisionMask)invalid();
        if(mask && !body.trigger){const auto position=context.pose(target.actor).position;
            const auto box=math::DetAabb2::fromCenterHalf(V{position.x,position.y}+body.offset,body.half);
            DetCollisionQuery2D query(context,c.collision);DetCollisionQueryFilter2D filter;
            filter.layer=body.layer;filter.mask=mask;filter.ignore=target.actor;filter.includeTriggers=false;
            if(!query.overlap(box,filter,true).empty())return DetActionCode2D::Occupied;
        }
        context.write(target.actor,c.collision.bodySchema,DetBodyMask,mask);return DetActionCode2D::Applied;
    }
    if(!target.path)return DetActionCode2D::Unsupported;
    const auto& p=*target.path;const auto state=detPathMotionState2D(context,target.actor,p);
    const auto body=readBody(context,target.actor,c.collision);
    if(!state.bound || body.mode!=DetBodyMode2D::MovingObstacle || body.trigger!=p.movingTrigger
        || (p.movingTrigger && (body.shape!=DetCollisionShape2D::Aabb || body.oneWay || body.ignoredOneWay.value)))return DetActionCode2D::Unsupported;
    DetPathMotionInput2D input{state.enabled,state.paused,context.read<bool>(target.actor,p.stateSchema,DetPathRestartRequested)};
    if(kind==DetActionKind2D::PathRestart){const auto at=context.pose(target.actor).position;
        if(!equal(V{at.x,at.y},state.origin))return DetActionCode2D::RestartAwayFromOrigin;
        if(input.restart)return DetActionCode2D::Unchanged;input.restart=true;
    }else {
        if(kind==DetActionKind2D::PathEnable)input.enabled=true;
        else if(kind==DetActionKind2D::PathDisable)input.enabled=false;
        else if(kind==DetActionKind2D::PathPause)input.paused=true;
        else if(kind==DetActionKind2D::PathResume)input.paused=false;
        else invalid();
        if(input.enabled==state.enabled && input.paused==state.paused)return DetActionCode2D::Unchanged;
    }
    detPathMotionInput2D(context,target.actor,p,input);return DetActionCode2D::Applied;
}
void validateCheckpoint(const DetSessionCheckpoint& cp,const DetActions2DConfig& c,const Plan& p){
    const DetStateLayout state(p.state),body(p.body);
    const auto manifest=detwire::manifestLayout(cp.manifest);
    if(manifest.validatorVersions.at(c.validatorId)!=profile(c)
        || manifest.logicProfiles.at(c.logicProfileId)!=std::pair<std::uint32_t,std::uint64_t>{profile(c),logicHash(c)}
        || manifest.systems.at(c.collision.systemId)!=c.collision.priority
        || (c.collision.extended && manifest.validatorVersions.at(c.collision.systemId)!=p.body.version))invalid();
    // A matching installed collision manifest/history is required even when no
    // enable action has run. This pure capture checks every collision body first.
    (void)DetCollisionQuery2D(cp,c.collision);
    for(const auto& schema:p.policies){const DetStateLayout policy(schema);
        if(cp.globals.at(schema.id)!=policy.defaults())invalid();
        for(const auto& [id,actor]:cp.actors){(void)id;if(actor.blocks.at(schema.id)!=policy.defaults())invalid();}}
    for(const auto& [id,actor]:cp.actors){(void)id;if(actor.blocks.at(c.stateSchema)!=state.defaults())invalid();}
    for(const auto& target:c.targets)if(target.path){const auto& path=*target.path;
        const auto identity=detail::pathMotionIdentity2D(path);
        if(manifest.systems.at(path.preSystemId)!=path.collision.priority || manifest.systems.at(path.postSystemId)!=path.collision.priority
            || manifest.validatorVersions.at(path.validatorId)!=identity.first
            || manifest.logicProfiles.at(path.logicProfileId)!=identity)invalid();
        const auto schema=detPathMotionPolicySchema2D(path);const DetStateLayout policy(schema);
        descriptor(manifest,schema);descriptor(manifest,detPathMotionStateSchema2D(path));
        if(cp.globals.at(schema.id)!=policy.defaults())invalid();
        for(const auto& [id,actor]:cp.actors){(void)id;if(actor.blocks.at(schema.id)!=policy.defaults())invalid();}}
    const auto& words=cp.globals.at(c.stateSchema);const auto read=[&]<class T>(std::uint32_t field){return std::get<T>(state.read(words,field));};
    validateRecord({read.operator()<std::uint64_t>(DetActionsTotalCount),read.operator()<std::uint64_t>(DetActionsBudgetTick),
        read.operator()<std::uint64_t>(DetActionsLastTick),read.operator()<std::uint32_t>(DetActionsTickCount),
        read.operator()<std::uint32_t>(DetActionsLastCode),read.operator()<std::uint32_t>(DetActionsLastKind),
        read.operator()<DetEntityRef>(DetActionsLastTarget).value},c,cp.nextTick);
    for(const auto& target:c.targets){const auto actor=cp.actors.find(target.actor);if(actor==cp.actors.end())continue;
        const auto& block=actor->second.blocks.at(c.collision.bodySchema);if(!body.valid(block))invalid();
        const auto mask=std::get<std::uint32_t>(body.read(block,DetBodyMask));if(mask!=0 && mask!=target.collisionMask)invalid();
        DetCollisionBody2D value{static_cast<DetBodyMode2D>(std::get<std::uint32_t>(body.read(block,DetBodyMode))),
            std::get<V>(body.read(block,DetBodyHalf)),std::get<V>(body.read(block,DetBodyOffset)),std::get<V>(body.read(block,DetBodyVelocity)),
            std::get<std::uint32_t>(body.read(block,DetBodyLayer)),mask,std::get<bool>(body.read(block,DetBodyTrigger))};
        if(c.collision.oneWayPlatforms){value.oneWay=std::get<bool>(body.read(block,DetBodyOneWay));value.ignoredOneWay=std::get<DetEntityRef>(body.read(block,DetBodyIgnoredOneWay));}
        if(c.collision.staticSlopes)value.shape=static_cast<DetCollisionShape2D>(std::get<std::uint32_t>(body.read(block,DetBodyShape)));
        (void)detCollisionBodyState2D(c.collision,value);
        if(target.path){const auto& path=*target.path;const auto& pathBlock=actor->second.blocks.at(path.stateSchema);
            // Read the actual selected descriptor. Full phase validation remains
            // path-owned, independently of this action table's literal policy.
            const auto binding=detPathMotionStateSchema2D(path);
            if(!readDetState<bool>(binding,pathBlock,DetPathBound)
                || std::get<std::uint32_t>(body.read(block,DetBodyMode))!=static_cast<std::uint32_t>(DetBodyMode2D::MovingObstacle)
                || value.trigger!=path.movingTrigger
                || (path.movingTrigger && (value.shape!=DetCollisionShape2D::Aabb || value.oneWay || value.ignoredOneWay.value)))invalid();
        }
    }
}
} // namespace
std::vector<std::uint8_t> encodeDetActionsConfig2D(const DetActions2DConfig& c){return plan(c).bytes;}
DetTypedStateSchema detActionsStateSchema2D(const DetActions2DConfig& c){return plan(c).state;}
std::vector<DetTypedStateSchema> detActionsPolicySchemas2D(const DetActions2DConfig& c){return plan(c).policies;}
bool installDetActions2D(DeterministicSession& session,DetActions2DConfig c){
    Plan p;try{p=plan(c);for(const auto& target:c.targets)if(target.path && target.path->tickPeriod.bits()!=session.fixedStep().bits())return false;}
    catch(const std::invalid_argument&){return false;}
    if(!(session.registerTypedSchema(p.state) && session.registerGlobalState(c.stateSchema)))return false;
    for(const auto& schema:p.policies)if(!(session.registerTypedSchema(schema) && session.registerGlobalState(schema.id)))return false;
    return session.registerLogicProfile(c.logicProfileId,profile(c),logicHash(c))
        && session.registerValidator(c.validatorId,profile(c),[c,p](const DetSessionCheckpoint& cp,std::string& error){
            try{validateCheckpoint(cp,c,p);return true;}catch(const std::exception& e){error=e.what();return false;}});
}
DetActionResult2D executeDetAction2D(DetTickContext& context,const DetActions2DConfig& c,DetActionRequest2D request){
    const auto p=plan(c);policyMatches(context,p);const auto kind=static_cast<std::uint32_t>(request.kind);
    if(kind<1 || kind>7 || context.tick()==UINT64_MAX)invalid();
    auto record=readRecord(context,c);validateRecord(record,c,context.tick()+1);if(record.total==UINT64_MAX)invalid();
    const auto stamp=context.tick()+1;if(record.budgetTick!=stamp)record.tickCount=0;
    DetActionCode2D code;
    if(record.tickCount==c.maxActionsPerTick)code=DetActionCode2D::BudgetExceeded;
    else {++record.tickCount;const auto* target=find(c,request.target);
        if(!target)code=DetActionCode2D::UnknownTarget;
        else {const auto actors=context.entities();
            if(!std::binary_search(actors.begin(),actors.end(),request.target))code=DetActionCode2D::MissingActor;
            else code=apply(context,c,*target,request.kind);}}
    ++record.total;const auto s=c.stateSchema;
    context.writeGlobal(s,DetActionsTotalCount,record.total);context.writeGlobal(s,DetActionsBudgetTick,stamp);
    context.writeGlobal(s,DetActionsTickCount,record.tickCount);context.writeGlobal(s,DetActionsLastCode,static_cast<std::uint32_t>(code));
    context.writeGlobal(s,DetActionsLastTarget,DetEntityRef{request.target});context.writeGlobal(s,DetActionsLastKind,kind);
    context.writeGlobal(s,DetActionsLastTick,stamp);
    return {code,context.tick(),record.total,request};
}
DetActionResult2D detActionResult2D(DetTickContext& context,const DetActions2DConfig& c){const auto p=plan(c);policyMatches(context,p);
    const auto r=readRecord(context,c);if(context.tick()==UINT64_MAX)invalid();validateRecord(r,c,context.tick()+1);if(!r.total)return {};
    return {static_cast<DetActionCode2D>(r.code),r.lastTick-1,r.total,{r.target,static_cast<DetActionKind2D>(r.kind)}};
}
std::uint64_t detActionCount2D(DetTickContext& context,const DetActions2DConfig& c){const auto p=plan(c);policyMatches(context,p);
    return context.readGlobal<std::uint64_t>(c.stateSchema,DetActionsTotalCount);}
} // namespace ayt::entity
