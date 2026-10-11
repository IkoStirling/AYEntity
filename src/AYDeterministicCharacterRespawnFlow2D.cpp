// design reference: AYEntity/design.md finite registered checkpoint/death routing.
#include <AYEntity/DeterministicCharacterRespawnFlow2D.h>
#include "detail/DetCharacterRespawn2D.h"
#include "detail/DetSessionWire.h"
#include <algorithm>
#include <set>
#include <stdexcept>

namespace ayt::entity {
namespace {
constexpr std::size_t chunkBytes=1000;
constexpr std::uint64_t logicHash=0x4452463244503031ULL;
[[noreturn]] void invalid(){throw std::invalid_argument("deterministic respawn flow 2D: invalid configuration, permission, event, state or order");}
std::uint64_t add(std::uint64_t a,std::uint64_t b){if(a>UINT64_MAX-b)invalid();return a+b;}
std::uint64_t stamp(std::uint64_t tick){return add(tick,1);}
std::uint32_t field(std::size_t index,std::uint32_t member){return static_cast<std::uint32_t>(1000*(index+1))+member;}
std::size_t bindingIndex(const DetCharacterRespawnFlow2DConfig& c,SimEntityId actor){
    const auto it=std::lower_bound(c.bindings.begin(),c.bindings.end(),actor,[](const auto& row,auto id){return row.actor<id;});
    if(it==c.bindings.end() || it->actor!=actor)invalid();return static_cast<std::size_t>(it-c.bindings.begin());
}
const DetRespawnFlowRegion2D* region(const DetCharacterRespawnFlow2DConfig& c,std::uint32_t id){
    const auto it=std::lower_bound(c.regions.begin(),c.regions.end(),id,[](const auto& row,auto value){return row.id<value;});
    return it!=c.regions.end() && it->id==id?&*it:nullptr;
}
bool authority(const DetCharacterRespawnFlow2DConfig& c,SimEntityId actor,std::uint32_t spawn){
    const auto* row=detail::respawnSpawn(c.respawn,spawn);return row && row->actor==actor;
}
struct Writer {
    std::vector<std::uint8_t> bytes;
    void put(std::uint64_t value,unsigned count){if(bytes.size()>kDetCharacterRespawnFlow2DMaxPolicyBytes-count)invalid();
        for(unsigned i=0;i<count;++i)bytes.push_back(static_cast<std::uint8_t>(value>>(8*i)));}
    void u32(std::uint32_t v){put(v,4);}void u64(std::uint64_t v){put(v,8);}
    void lanes(std::span<const std::uint64_t> value){u32(static_cast<std::uint32_t>(value.size()));for(auto lane:value)u64(lane);}
};
struct Plan {std::vector<std::uint8_t> bytes;DetTypedStateSchema state;std::vector<DetTypedStateSchema> policies;};
Plan plan(const DetCharacterRespawnFlow2DConfig& c){
    if(!detail::validRespawnConfig(c.respawn) || c.stateSchema<2 || c.stateSchema==UINT32_MAX
        || c.policySchemaBase<2 || c.policySchemaBase==UINT32_MAX || !c.systemId || !c.validatorId || !c.logicProfileId
        || c.bindings.empty() || c.bindings.size()>kDetCharacterRespawnFlow2DMaxBindings || c.regions.size()>kDetCharacterRespawnFlow2DMaxRegions)invalid();
    const auto& r=c.respawn;const auto& k=r.character;const auto& p=k.collision;
    if(c.systemId>=k.preSystemId || c.systemId==k.postSystemId || c.systemId==p.systemId
        || c.validatorId==r.validatorId || c.validatorId==k.validatorId || c.validatorId==p.systemId
        || c.logicProfileId==r.logicProfileId || c.logicProfileId==k.logicProfileId)invalid();
    Plan out;out.state={c.stateSchema,1,{{1,std::uint64_t{}}}};
    Writer w;w.u32(1);w.u32(c.stateSchema);w.u32(c.policySchemaBase);w.u32(c.systemId);w.u32(c.validatorId);w.u32(c.logicProfileId);
    const auto nested=detail::respawnPolicySchema(r);w.u32(nested.id);w.u32(nested.version);w.u32(static_cast<std::uint32_t>(nested.fields.size()));
    // Literal typed fields retain all nested policy bits, not only a digest.
    for(const auto& f:nested.fields){w.u32(f.id);w.u32(static_cast<std::uint32_t>(detStateType(f.initial)));w.lanes(encodeDetStateValue(f.initial));}
    w.u32(static_cast<std::uint32_t>(c.bindings.size()));SimEntityId previousActor=0;
    for(std::size_t n=0;n<c.bindings.size();++n){const auto& b=c.bindings[n];
        if(!b.actor || b.actor<=previousActor || !authority(c,b.actor,b.spawn))invalid();previousActor=b.actor;w.u64(b.actor);w.u32(b.spawn);
        out.state.fields.insert(out.state.fields.end(),{{field(n,10),b.spawn},{field(n,20),std::uint64_t{}},
            {field(n,30),std::uint64_t{}},{field(n,40),std::uint32_t{}},{field(n,50),std::uint64_t{}},
            {field(n,60),std::uint64_t{}},{field(n,70),std::uint32_t{}},{field(n,80),std::uint64_t{}},
            {field(n,90),std::uint64_t{}},{field(n,100),std::uint64_t{}},{field(n,110),std::uint32_t{}},
            {field(n,120),std::uint32_t{}},{field(n,130),std::uint32_t{}},{field(n,140),false},{field(n,150),std::uint64_t{}}});
    }
    w.u32(static_cast<std::uint32_t>(c.regions.size()));std::uint32_t previousRegion=0;
    for(const auto& row:c.regions){const auto kind=static_cast<std::uint32_t>(row.kind);
        if(!row.id || row.id<=previousRegion || !row.trigger || row.trigger==row.actor || kind>1)invalid();
        (void)bindingIndex(c,row.actor);if((kind==0 && !authority(c,row.actor,row.spawn)) || (kind==1 && row.spawn))invalid();
        previousRegion=row.id;w.u32(row.id);w.u64(row.trigger);w.u64(row.actor);w.u32(kind);w.u32(row.spawn);
    }
    out.bytes=std::move(w.bytes);const auto chunks=(out.bytes.size()+chunkBytes-1)/chunkBytes;
    if(!chunks || chunks>5 || c.policySchemaBase>UINT32_MAX-chunks)invalid();
    std::set<std::uint32_t> schemas;for(auto id:{p.bodySchema,p.historySchema,k.stateSchema,k.policySchema,r.stateSchema,r.policySchema,c.stateSchema})
        if(!schemas.insert(id).second)invalid();
    for(std::size_t n=0;n<chunks;++n){const auto id=c.policySchemaBase+static_cast<std::uint32_t>(n);if(!schemas.insert(id).second)invalid();
        DetTypedStateSchema s{id,1,{{1,static_cast<std::uint32_t>(n)},{2,static_cast<std::uint32_t>(chunks)},{3,static_cast<std::uint32_t>(out.bytes.size())}}};
        const auto begin=n*chunkBytes,end=std::min(begin+chunkBytes,out.bytes.size());
        for(std::size_t offset=begin;offset<end;offset+=8){std::uint64_t word=0;for(std::size_t b=0;b<8 && offset+b<end;++b)word|=std::uint64_t{out.bytes[offset+b]}<<(8*b);
            s.fields.push_back({static_cast<std::uint32_t>(4+(offset-begin)/8),word});}out.policies.push_back(std::move(s));
    }
    (void)detStateDefaults(out.state);return out;
}
void policiesMatch(DetTickContext& ctx,const DetCharacterRespawnFlow2DConfig& c,const Plan& p){
    detail::respawnPolicyMatches(ctx,c.respawn);for(const auto& s:p.policies)for(const auto& f:s.fields)std::visit([&]<class T>(const T& value){
        if(encodeDetStateValue(ctx.readGlobal<T>(s.id,f.id))!=encodeDetStateValue(value))invalid();},f.initial);
}
void descriptor(const detwire::ManifestLayout& m,const DetTypedStateSchema& s){
    if(m.schemaVersions.at(s.id)!=s.version || !m.globals.contains(s.id))invalid();const auto& actual=m.schemas.at(s.id);
    if(actual.size()!=s.fields.size())invalid();for(std::size_t n=0;n<actual.size();++n)if(actual[n].id!=s.fields[n].id
        || actual[n].type!=detStateType(s.fields[n].initial) || actual[n].width!=encodeDetStateValue(s.fields[n].initial).size())invalid();
}
struct Record {DetCharacterRespawnFlowState2D state;std::uint64_t checkpointStamp=0,deathStamp=0,requestStamp=0,processed=0;bool manual=false;};
template<class Getter>Record read(Getter&& get){Record r;auto& s=r.state;
    s.activeSpawn=get.template operator()<std::uint32_t>(10);s.checkpointCount=get.template operator()<std::uint64_t>(20);
    r.checkpointStamp=get.template operator()<std::uint64_t>(30);s.lastCheckpointRegion=get.template operator()<std::uint32_t>(40);
    s.deathCount=get.template operator()<std::uint64_t>(50);r.deathStamp=get.template operator()<std::uint64_t>(60);s.lastDeathRegion=get.template operator()<std::uint32_t>(70);
    s.manualCount=get.template operator()<std::uint64_t>(80);s.requestCount=get.template operator()<std::uint64_t>(90);r.requestStamp=get.template operator()<std::uint64_t>(100);
    s.lastRequestCode=static_cast<DetRespawnRequestCode2D>(get.template operator()<std::uint32_t>(110));
    s.lastRequestKind=static_cast<DetRespawnFlowRequestKind2D>(get.template operator()<std::uint32_t>(120));s.lastRequestSpawn=get.template operator()<std::uint32_t>(130);
    r.manual=get.template operator()<bool>(140);r.processed=get.template operator()<std::uint64_t>(150);
    s.lastCheckpointTick=r.checkpointStamp?r.checkpointStamp-1:0;s.lastDeathTick=r.deathStamp?r.deathStamp-1:0;s.lastRequestTick=r.requestStamp?r.requestStamp-1:0;return r;
}
Record read(DetTickContext& ctx,const DetCharacterRespawnFlow2DConfig& c,std::size_t n){return read([&]<class T>(std::uint32_t f){return ctx.readGlobal<T>(c.stateSchema,field(n,f));});}
Record read(const DetSessionCheckpoint& cp,const Plan& p,std::size_t n){const DetStateLayout l(p.state);return read([&]<class T>(std::uint32_t f){return std::get<T>(l.read(cp.globals.at(p.state.id),field(n,f)));});}
void valid(const Record& r,const DetCharacterRespawnFlow2DConfig& c,std::size_t n,std::uint64_t maximum,std::uint64_t processed,bool committed){
    const auto& s=r.state;const auto actor=c.bindings[n].actor;
    if(!authority(c,actor,s.activeSpawn) || r.processed!=processed || (committed && r.manual)
        || s.checkpointCount>maximum || s.deathCount>maximum || s.manualCount>maximum || s.requestCount>maximum
        || add(s.deathCount,s.manualCount)!=s.requestCount)invalid();
    if(!s.checkpointCount){if(r.checkpointStamp || s.lastCheckpointRegion || s.activeSpawn!=c.bindings[n].spawn)invalid();}
    else {const auto* row=region(c,s.lastCheckpointRegion);if(!r.checkpointStamp || r.checkpointStamp>maximum || s.checkpointCount>r.checkpointStamp || !row
        || row->kind!=DetRespawnFlowRegionKind2D::Checkpoint || row->actor!=actor || row->spawn!=s.activeSpawn)invalid();}
    if(!s.deathCount){if(r.deathStamp || s.lastDeathRegion)invalid();}
    else {const auto* row=region(c,s.lastDeathRegion);if(!r.deathStamp || r.deathStamp>maximum || s.deathCount>r.deathStamp || !row
        || row->kind!=DetRespawnFlowRegionKind2D::Death || row->actor!=actor)invalid();}
    const auto code=static_cast<std::uint32_t>(s.lastRequestCode),kind=static_cast<std::uint32_t>(s.lastRequestKind);
    if(!s.requestCount){if(r.requestStamp || code || kind || s.lastRequestSpawn)invalid();}
    else if(!r.requestStamp || r.requestStamp>maximum || s.requestCount>r.requestStamp || code>3 || code==2 || kind<1 || kind>2
        || !authority(c,actor,s.lastRequestSpawn) || r.deathStamp>r.requestStamp
        || (kind==1 && (!s.deathCount || r.requestStamp!=r.deathStamp))
        || (kind==2 && (!s.manualCount || (s.deathCount && r.deathStamp>=r.requestStamp)))
        || (r.requestStamp>=r.checkpointStamp && s.lastRequestSpawn!=s.activeSpawn))invalid();
}
void write(DetTickContext& ctx,const DetCharacterRespawnFlow2DConfig& c,std::size_t n,const Record& r){const auto id=c.stateSchema;const auto& s=r.state;
    ctx.writeGlobal(id,field(n,10),s.activeSpawn);ctx.writeGlobal(id,field(n,20),s.checkpointCount);ctx.writeGlobal(id,field(n,30),r.checkpointStamp);
    ctx.writeGlobal(id,field(n,40),s.lastCheckpointRegion);ctx.writeGlobal(id,field(n,50),s.deathCount);ctx.writeGlobal(id,field(n,60),r.deathStamp);
    ctx.writeGlobal(id,field(n,70),s.lastDeathRegion);ctx.writeGlobal(id,field(n,80),s.manualCount);ctx.writeGlobal(id,field(n,90),s.requestCount);
    ctx.writeGlobal(id,field(n,100),r.requestStamp);ctx.writeGlobal(id,field(n,110),static_cast<std::uint32_t>(s.lastRequestCode));
    ctx.writeGlobal(id,field(n,120),static_cast<std::uint32_t>(s.lastRequestKind));ctx.writeGlobal(id,field(n,130),s.lastRequestSpawn);
    ctx.writeGlobal(id,field(n,140),r.manual);ctx.writeGlobal(id,field(n,150),r.processed);
}
void reference(const DetSessionCheckpoint& cp,const DetCharacterRespawnFlow2DConfig& c,SimEntityId id,bool trigger){
    const auto found=cp.actors.find(id);if(found==cp.actors.end()){
        if(!cp.nextTick || !std::binary_search(cp.retiredIds.begin(),cp.retiredIds.end(),id))invalid();return;}
    const DetStateLayout body(detCollisionBodySchema2D(c.respawn.character.collision));const auto& words=found->second.blocks.at(c.respawn.character.collision.bodySchema);
    const auto mode=std::get<std::uint32_t>(body.read(words,DetBodyMode));const bool isTrigger=std::get<bool>(body.read(words,DetBodyTrigger));
    if(trigger){if(mode==0 || !isTrigger)invalid();}else if(mode!=2 || isTrigger)invalid();
}
void validateCheckpoint(const DetSessionCheckpoint& cp,const DetCharacterRespawnFlow2DConfig& c,const Plan& p){
    detail::validateRespawnCheckpoint(cp,c.respawn);const auto m=detwire::manifestLayout(cp.manifest);
    if(m.systems.at(c.systemId)!=c.respawn.character.collision.priority || m.validatorVersions.at(c.validatorId)!=1
        || m.logicProfiles.at(c.logicProfileId)!=std::pair<std::uint32_t,std::uint64_t>{1,logicHash})invalid();descriptor(m,p.state);
    const DetStateLayout state(p.state);if(!state.valid(cp.globals.at(p.state.id)) || std::get<std::uint64_t>(state.read(cp.globals.at(p.state.id),1)))invalid();
    for(const auto& s:p.policies){descriptor(m,s);const auto expected=detStateDefaults(s);if(cp.globals.at(s.id)!=expected)invalid();
        for(const auto& [id,a]:cp.actors){(void)id;if(a.blocks.at(s.id)!=expected)invalid();}}
    for(const auto& [id,a]:cp.actors){(void)id;if(a.blocks.at(p.state.id)!=state.defaults())invalid();}
    for(std::size_t n=0;n<c.bindings.size();++n){reference(cp,c,c.bindings[n].actor,false);const auto r=read(cp,p,n);valid(r,c,n,cp.nextTick,cp.nextTick,true);
        if(r.state.requestCount && r.requestStamp==cp.nextTick && r.state.lastRequestCode==DetRespawnRequestCode2D::Queued
            && cp.actors.contains(c.bindings[n].actor)){
            const auto last=detCharacterRespawnState2D(cp,c.bindings[n].actor,c.respawn);
            if(last.result==DetRespawnResult2D::None || last.resultTick!=cp.nextTick || last.spawn!=r.state.lastRequestSpawn)invalid();}}
    for(const auto& row:c.regions)reference(cp,c,row.trigger,true);
    for(const auto& event:cp.pendingEvents)if(event.source==c.respawn.character.collision.systemId && event.type==c.respawn.character.collision.eventType){
        DetTriggerEvent2D decoded;if(!decodeDetTriggerEvent2D(event.payload,decoded))invalid();}
}
bool run(DetTickContext& ctx,const DetCharacterRespawnFlow2DConfig& c,const Plan& p){
    policiesMatch(ctx,c,p);if(ctx.readGlobal<std::uint64_t>(c.stateSchema,1))invalid();const auto next=stamp(ctx.tick());
    std::vector<Record> records;for(std::size_t n=0;n<c.bindings.size();++n){auto r=read(ctx,c,n);valid(r,c,n,ctx.tick(),ctx.tick(),false);records.push_back(r);}
    std::vector<const DetTickCommand*> events;for(const auto& event:ctx.events())if(event.source==c.respawn.character.collision.systemId && event.type==c.respawn.character.collision.eventType)events.push_back(&event);
    std::sort(events.begin(),events.end(),[](const auto* a,const auto* b){return detwire::commandKey(*a)<detwire::commandKey(*b);});
    std::vector<const DetRespawnFlowRegion2D*> checkpoints(c.bindings.size(),nullptr),deaths(c.bindings.size(),nullptr);
    for(const auto* event:events){DetTriggerEvent2D decoded;if(!decodeDetTriggerEvent2D(event->payload,decoded))invalid();if(decoded.phase!=DetTriggerPhase2D::Enter)continue;
        for(const auto& row:c.regions){if(decoded.pair!=DetCollisionPair2D{std::min(row.trigger,row.actor),std::max(row.trigger,row.actor)})continue;
            const auto n=bindingIndex(c,row.actor);auto& selected=row.kind==DetRespawnFlowRegionKind2D::Checkpoint?checkpoints[n]:deaths[n];if(!selected)selected=&row;}}
    // Apply every checkpoint before any request, independent of actor/event ordering.
    for(std::size_t n=0;n<records.size();++n)if(checkpoints[n]){auto& r=records[n];r.state.activeSpawn=checkpoints[n]->spawn;
        r.state.checkpointCount=add(r.state.checkpointCount,1);r.checkpointStamp=next;r.state.lastCheckpointRegion=checkpoints[n]->id;}
    for(std::size_t n=0;n<records.size();++n){auto& r=records[n];auto& s=r.state;
        if(deaths[n] || r.manual){s.lastRequestKind=deaths[n]?DetRespawnFlowRequestKind2D::Death:DetRespawnFlowRequestKind2D::Manual;
            if(deaths[n]){s.deathCount=add(s.deathCount,1);r.deathStamp=next;s.lastDeathRegion=deaths[n]->id;}else s.manualCount=add(s.manualCount,1);
            s.requestCount=add(s.requestCount,1);r.requestStamp=next;s.lastRequestSpawn=s.activeSpawn;
            s.lastRequestCode=detCharacterRespawnRequest2D(ctx,c.bindings[n].actor,c.respawn,s.activeSpawn);}
        r.manual=false;r.processed=next;write(ctx,c,n,r);
    }return true;
}
} // namespace
bool installDetCharacterRespawnFlow2D(DeterministicSession& session,DetCharacterRespawnFlow2DConfig c){Plan p;
    try{p=plan(c);if(session.sealed() || session.faulted() || session.nextTick())return false;}catch(const std::invalid_argument&){return false;}
    if(!(session.registerTypedSchema(p.state) && session.registerGlobalState(p.state.id)))return false;
    for(const auto& s:p.policies)if(!(session.registerTypedSchema(s) && session.registerGlobalState(s.id)))return false;
    return session.registerLogicProfile(c.logicProfileId,1,logicHash) && session.registerValidator(c.validatorId,1,[c,p](const auto& cp,std::string& error){
        try{validateCheckpoint(cp,c,p);return true;}catch(const std::exception& e){error=e.what();return false;}})
        && session.registerSystem(c.systemId,c.respawn.character.collision.priority,[c,p](DetTickContext& ctx){return run(ctx,c,p);});
}
void detCharacterRespawnFlowInput2D(DetTickContext& ctx,SimEntityId actor,const DetCharacterRespawnFlow2DConfig& c,bool manual){
    const auto p=plan(c);policiesMatch(ctx,c,p);const auto n=bindingIndex(c,actor);const auto r=read(ctx,c,n);valid(r,c,n,ctx.tick(),ctx.tick(),false);
    if(manual)ctx.writeGlobal(c.stateSchema,field(n,140),true);
}
DetCharacterRespawnFlowState2D detCharacterRespawnFlowState2D(DetTickContext& ctx,SimEntityId actor,const DetCharacterRespawnFlow2DConfig& c){
    const auto p=plan(c);policiesMatch(ctx,c,p);const auto n=bindingIndex(c,actor);const auto r=read(ctx,c,n);
    if(r.processed!=ctx.tick() && r.processed!=stamp(ctx.tick()))invalid();valid(r,c,n,stamp(ctx.tick()),r.processed,false);return r.state;
}
DetCharacterRespawnFlowState2D detCharacterRespawnFlowState2D(const DetSessionCheckpoint& cp,SimEntityId actor,const DetCharacterRespawnFlow2DConfig& c){
    const auto p=plan(c);validateCheckpoint(cp,c,p);return read(cp,p,bindingIndex(c,actor)).state;
}
DetTypedStateSchema detCharacterRespawnFlowStateSchema2D(const DetCharacterRespawnFlow2DConfig& c){return plan(c).state;}
std::vector<DetTypedStateSchema> detCharacterRespawnFlowPolicySchemas2D(const DetCharacterRespawnFlow2DConfig& c){return plan(c).policies;}
std::uint32_t detCharacterRespawnFlowField2D(const DetCharacterRespawnFlow2DConfig& c,SimEntityId actor,DetRespawnFlowField2D member){
    (void)plan(c);const auto value=static_cast<std::uint32_t>(member);if(value<10 || value>150 || value%10)invalid();return field(bindingIndex(c,actor),value);
}
std::vector<std::uint8_t> encodeDetCharacterRespawnFlowConfig2D(const DetCharacterRespawnFlow2DConfig& c){return plan(c).bytes;}
} // namespace ayt::entity
