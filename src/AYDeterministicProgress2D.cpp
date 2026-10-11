// design reference: AYEntity/design.md Stage37 finite registered gameplay progress.
#include <AYEntity/DeterministicProgress2D.h>
#include <AYEntity/DeterministicCollisionQuery2D.h>
#include "detail/DetSessionWire.h"
#include <algorithm>
#include <set>
#include <stdexcept>

namespace ayt::entity {
namespace {
constexpr std::size_t chunkBytes=1000;
constexpr std::uint64_t logicHash=0x4450523244503031ull,actionsLogicHash=0x4441433244503031ull;
[[noreturn]] void invalid(){throw std::invalid_argument("deterministic progress 2D: invalid configuration, policy or registered state");}
std::uint64_t stamp(std::uint64_t tick){if(tick==UINT64_MAX)invalid();return tick+1;}
std::uint32_t counterField(std::size_t index){return static_cast<std::uint32_t>(100+10*index);}
std::uint32_t claimField(std::size_t index,bool time=false){return static_cast<std::uint32_t>(1000+20*index+(time?10:0));}
template<class T>const T* find(const std::vector<T>& table,std::uint32_t id){
    const auto it=std::lower_bound(table.begin(),table.end(),id,[](const T& row,auto value){return row.id<value;});return it!=table.end() && it->id==id?&*it:nullptr;}
template<class T>std::size_t index(const std::vector<T>& table,std::uint32_t id){const auto* item=find(table,id);if(!item)invalid();return static_cast<std::size_t>(item-table.data());}
const DetActionTarget2D* target(const DetActions2DConfig& c,SimEntityId id){
    const auto it=std::lower_bound(c.targets.begin(),c.targets.end(),id,[](const auto& row,auto value){return row.actor<value;});return it!=c.targets.end() && it->actor==id?&*it:nullptr;}
struct Writer {std::vector<std::uint8_t> bytes;
    void put(std::uint64_t value,unsigned count){for(unsigned n=0;n<count;++n)bytes.push_back(static_cast<std::uint8_t>(value>>(8*n)));}
    void u32(std::uint32_t value){put(value,4);}void u64(std::uint64_t value){put(value,8);}};
struct Plan {std::vector<std::uint8_t> bytes;DetTypedStateSchema state,body,actionState;std::vector<DetTypedStateSchema> policies,actionPolicies;};
Plan plan(const DetProgress2DConfig& c){
    if(c.stateSchema<2 || c.stateSchema==UINT32_MAX || c.policySchemaBase<2 || c.policySchemaBase==UINT32_MAX
        || !c.validatorId || !c.logicProfileId || c.validatorId==c.actions.validatorId || c.validatorId==c.actions.collision.systemId
        || c.logicProfileId==c.actions.logicProfileId || c.counters.empty() || c.counters.size()>kDetProgress2DMaxCounters
        || c.objectives.size()>kDetProgress2DMaxObjectives || c.claims.size()>kDetProgress2DMaxClaims)invalid();
    Plan p;p.body=detCollisionBodySchema2D(c.actions.collision);p.actionState=detActionsStateSchema2D(c.actions);
    p.actionPolicies=detActionsPolicySchemas2D(c.actions);const auto actionBytes=encodeDetActionsConfig2D(c.actions);
    Writer w;w.u32(kDetProgress2DProfileVersion);w.u32(c.stateSchema);w.u32(c.policySchemaBase);w.u32(c.validatorId);w.u32(c.logicProfileId);
    w.u32(static_cast<std::uint32_t>(actionBytes.size()));w.bytes.insert(w.bytes.end(),actionBytes.begin(),actionBytes.end());
    p.state={c.stateSchema,1,{{DetProgressTotalCount,std::uint64_t{}},{DetProgressLastOperation,std::uint32_t{}},
        {DetProgressLastId,std::uint32_t{}},{DetProgressLastCounter,std::uint32_t{}},{DetProgressLastAmount,std::uint64_t{}},
        {DetProgressLastCode,std::uint32_t{}},{DetProgressLastActionCode,std::uint32_t{}},{DetProgressLastTick,std::uint64_t{}},
        {DetProgressLastActionSequence,std::uint64_t{}}}};
    std::uint32_t previous=0;w.u32(static_cast<std::uint32_t>(c.counters.size()));
    for(std::size_t i=0;i<c.counters.size();++i){const auto& row=c.counters[i];
        if(!row.id || row.id<=previous || row.minimum>row.initial || row.initial>row.maximum)invalid();previous=row.id;
        w.u32(row.id);w.u64(row.minimum);w.u64(row.maximum);w.u64(row.initial);p.state.fields.push_back({counterField(i),row.initial});}
    previous=0;w.u32(static_cast<std::uint32_t>(c.objectives.size()));
    for(const auto& row:c.objectives){if(!row.id || row.id<=previous || row.conditions.empty() || row.conditions.size()>kDetProgress2DMaxConditions)invalid();previous=row.id;
        w.u32(row.id);w.u32(static_cast<std::uint32_t>(row.conditions.size()));std::uint32_t prior=0;
        for(const auto& condition:row.conditions){const auto* counter=find(c.counters,condition.counter);
            if(!counter || condition.counter<=prior || condition.atLeast>counter->maximum)invalid();prior=condition.counter;
            w.u32(condition.counter);w.u64(condition.atLeast);}}
    previous=0;w.u32(static_cast<std::uint32_t>(c.claims.size()));
    std::set<std::pair<SimEntityId,SimEntityId>> pairs;
    for(std::size_t i=0;i<c.claims.size();++i){const auto& row=c.claims[i];const auto* controlled=target(c.actions,row.trigger);
        if(!row.id || row.id<=previous || !row.trigger || !row.actor || row.trigger==row.actor || !row.amount
            || !find(c.counters,row.counter) || !controlled || !controlled->collisionControl || controlled->path
            || !pairs.emplace(row.trigger,row.actor).second)invalid();previous=row.id;
        w.u32(row.id);w.u64(row.trigger);w.u64(row.actor);w.u32(row.counter);w.u64(row.amount);
        p.state.fields.push_back({claimField(i),false});p.state.fields.push_back({claimField(i,true),std::uint64_t{}});}
    p.bytes=std::move(w.bytes);if(p.bytes.size()>kDetProgress2DMaxPolicyBytes)invalid();
    const auto chunks=(p.bytes.size()+chunkBytes-1)/chunkBytes;if(!chunks || chunks>10 || c.policySchemaBase>UINT32_MAX-chunks)invalid();
    std::set<std::uint32_t> schemas{c.actions.collision.bodySchema,c.actions.collision.historySchema};
    if(!schemas.insert(c.actions.stateSchema).second)invalid();for(const auto& schema:p.actionPolicies)if(!schemas.insert(schema.id).second)invalid();
    if(!schemas.insert(c.stateSchema).second)invalid();
    for(std::size_t i=0;i<chunks;++i){const auto id=c.policySchemaBase+static_cast<std::uint32_t>(i);if(!schemas.insert(id).second)invalid();
        DetTypedStateSchema schema{id,1,{{1,static_cast<std::uint32_t>(i)},{2,static_cast<std::uint32_t>(chunks)},{3,static_cast<std::uint32_t>(p.bytes.size())}}};
        const auto begin=i*chunkBytes,end=std::min(begin+chunkBytes,p.bytes.size());for(std::size_t offset=begin;offset<end;offset+=8){std::uint64_t word=0;
            for(std::size_t n=0;n<8 && offset+n<end;++n)word|=std::uint64_t{p.bytes[offset+n]}<<(8*n);
            schema.fields.push_back({static_cast<std::uint32_t>(4+(offset-begin)/8),word});}p.policies.push_back(std::move(schema));}
    for(const auto& controlled:c.actions.targets)if(controlled.path){const auto& path=*controlled.path;
        if(schemas.contains(path.stateSchema) || schemas.contains(path.policySchema) || c.validatorId==path.validatorId || c.logicProfileId==path.logicProfileId)invalid();}
    (void)detStateDefaults(p.state);return p;
}
void matches(DetTickContext& context,const std::vector<DetTypedStateSchema>& policies){
    for(const auto& schema:policies)for(const auto& field:schema.fields)std::visit([&]<class T>(const T& expected){
        if(encodeDetStateValue(context.readGlobal<T>(schema.id,field.id))!=encodeDetStateValue(expected))invalid();},field.initial);}
void descriptor(const detwire::ManifestLayout& manifest,const DetTypedStateSchema& schema){
    if(manifest.schemaVersions.at(schema.id)!=schema.version || !manifest.globals.contains(schema.id))invalid();
    const auto& actual=manifest.schemas.at(schema.id);if(actual.size()!=schema.fields.size())invalid();
    for(std::size_t n=0;n<actual.size();++n)if(actual[n].id!=schema.fields[n].id || actual[n].type!=detStateType(schema.fields[n].initial)
        || actual[n].width!=encodeDetStateValue(schema.fields[n].initial).size())invalid();}
void matches(const DetSessionCheckpoint& cp,const detwire::ManifestLayout& manifest,const std::vector<DetTypedStateSchema>& policies){
    for(const auto& schema:policies){descriptor(manifest,schema);const auto expected=detStateDefaults(schema);if(cp.globals.at(schema.id)!=expected)invalid();
        for(const auto& [id,actor]:cp.actors){(void)id;if(actor.blocks.at(schema.id)!=expected)invalid();}}}
DetProgressResult2D readResult(DetTickContext& context,const DetProgress2DConfig& c){const auto s=c.stateSchema;DetProgressResult2D r;
    r.sequence=context.readGlobal<std::uint64_t>(s,DetProgressTotalCount);r.operation=static_cast<DetProgressOperation2D>(context.readGlobal<std::uint32_t>(s,DetProgressLastOperation));
    r.id=context.readGlobal<std::uint32_t>(s,DetProgressLastId);r.counter=context.readGlobal<std::uint32_t>(s,DetProgressLastCounter);
    r.amount=context.readGlobal<std::uint64_t>(s,DetProgressLastAmount);r.code=static_cast<DetProgressCode2D>(context.readGlobal<std::uint32_t>(s,DetProgressLastCode));
    r.actionCode=static_cast<DetActionCode2D>(context.readGlobal<std::uint32_t>(s,DetProgressLastActionCode));r.actionSequence=context.readGlobal<std::uint64_t>(s,DetProgressLastActionSequence);
    const auto last=context.readGlobal<std::uint64_t>(s,DetProgressLastTick);if(r.sequence){if(!last)invalid();r.tick=last-1;}else if(last)invalid();return r;}
DetProgressResult2D readResult(const DetSessionCheckpoint& cp,const DetProgress2DConfig& c,const Plan& p){
    const DetStateLayout layout(p.state);const auto& words=cp.globals.at(c.stateSchema);const auto get=[&]<class T>(std::uint32_t f){return std::get<T>(layout.read(words,f));};
    DetProgressResult2D r;r.sequence=get.operator()<std::uint64_t>(DetProgressTotalCount);r.operation=static_cast<DetProgressOperation2D>(get.operator()<std::uint32_t>(DetProgressLastOperation));
    r.id=get.operator()<std::uint32_t>(DetProgressLastId);r.counter=get.operator()<std::uint32_t>(DetProgressLastCounter);r.amount=get.operator()<std::uint64_t>(DetProgressLastAmount);
    r.code=static_cast<DetProgressCode2D>(get.operator()<std::uint32_t>(DetProgressLastCode));r.actionCode=static_cast<DetActionCode2D>(get.operator()<std::uint32_t>(DetProgressLastActionCode));
    r.actionSequence=get.operator()<std::uint64_t>(DetProgressLastActionSequence);const auto last=get.operator()<std::uint64_t>(DetProgressLastTick);
    if(r.sequence){if(!last)invalid();r.tick=last-1;}else if(last)invalid();return r;}
void validResult(const DetProgressResult2D& r,const DetProgress2DConfig& c,std::uint64_t maximumStamp,std::uint64_t actionCount,std::uint64_t actionStamp){
    const auto op=static_cast<std::uint32_t>(r.operation),code=static_cast<std::uint32_t>(r.code),action=static_cast<std::uint32_t>(r.actionCode);
    if(!r.sequence){if(op || code || r.tick || r.id || r.counter || r.amount || action || r.actionSequence)invalid();return;}
    if(op<1 || op>3 || code<1 || code>10 || stamp(r.tick)>maximumStamp || action>8 || bool(action)!=bool(r.actionSequence)
        || r.actionSequence>actionCount || (r.actionSequence && stamp(r.tick)>actionStamp))invalid();
    if(op==1 || op==2){const auto* counter=find(c.counters,r.id);
        if(r.counter!=r.id || action || (counter?(code!=1 && code!=2 && code!=4):(code!=3)) || (code==2 && r.amount) || (code==1 && !r.amount))invalid();}
    else {const auto* claim=find(c.claims,r.id);
        if(!claim){if(code!=5 || r.counter || r.amount || action)invalid();}
        else if(r.counter!=claim->counter || r.amount!=claim->amount || code==2 || code==3 || code==5
            || (code==1 && action!=1) || (code==10 && (action<2 || action>8)) || (code!=1 && code!=10 && action))invalid();}
}
void validValue(std::uint64_t value,const DetProgressCounter2D& counter){if(value<counter.minimum || value>counter.maximum)invalid();}
void validateCheckpoint(const DetSessionCheckpoint& cp,const DetProgress2DConfig& c,const Plan& p){
    const auto manifest=detwire::manifestLayout(cp.manifest);
    if(manifest.validatorVersions.at(c.validatorId)!=1 || manifest.logicProfiles.at(c.logicProfileId)!=std::pair<std::uint32_t,std::uint64_t>{1,logicHash}
        || manifest.validatorVersions.at(c.actions.validatorId)!=1 || manifest.logicProfiles.at(c.actions.logicProfileId)!=std::pair<std::uint32_t,std::uint64_t>{1,actionsLogicHash}
        || manifest.systems.at(c.actions.collision.systemId)!=c.actions.collision.priority
        || (c.actions.collision.extended && manifest.validatorVersions.at(c.actions.collision.systemId)!=p.body.version))invalid();
    (void)DetCollisionQuery2D(cp,c.actions.collision);descriptor(manifest,p.state);descriptor(manifest,p.actionState);
    matches(cp,manifest,p.policies);matches(cp,manifest,p.actionPolicies);
    const DetStateLayout state(p.state),actions(p.actionState),body(p.body);const auto& words=cp.globals.at(c.stateSchema),&actionWords=cp.globals.at(c.actions.stateSchema);
    if(!state.valid(words) || !actions.valid(actionWords))invalid();
    for(const auto& [id,actor]:cp.actors){(void)id;if(actor.blocks.at(c.stateSchema)!=state.defaults() || actor.blocks.at(c.actions.stateSchema)!=actions.defaults())invalid();}
    const auto actionCount=std::get<std::uint64_t>(actions.read(actionWords,DetActionsTotalCount)),actionStamp=std::get<std::uint64_t>(actions.read(actionWords,DetActionsLastTick));
    const auto result=readResult(cp,c,p);validResult(result,c,cp.nextTick,actionCount,actionStamp);
    for(std::size_t n=0;n<c.counters.size();++n)validValue(std::get<std::uint64_t>(state.read(words,counterField(n))),c.counters[n]);
    std::uint64_t claimedCount=0;std::set<SimEntityId> claimedTriggers;
    for(std::size_t n=0;n<c.claims.size();++n){const auto& claim=c.claims[n];const bool claimed=std::get<bool>(state.read(words,claimField(n)));
        const auto at=std::get<std::uint64_t>(state.read(words,claimField(n,true)));
        if(claimed){if(!at || at>cp.nextTick || at>actionStamp || !claimedTriggers.insert(claim.trigger).second)invalid();++claimedCount;}
        else if(at)invalid();
        for(const auto id:{claim.trigger,claim.actor}){const auto it=cp.actors.find(id);if(it==cp.actors.end()){
                if(!cp.nextTick || !std::binary_search(cp.retiredIds.begin(),cp.retiredIds.end(),id))invalid();continue;}
            const auto& block=it->second.blocks.at(c.actions.collision.bodySchema);
            const auto mode=std::get<std::uint32_t>(body.read(block,DetBodyMode));const bool isTrigger=std::get<bool>(body.read(block,DetBodyTrigger));
            if(id==claim.trigger){if(mode==0 || !isTrigger || (c.actions.collision.oneWayPlatforms && std::get<bool>(body.read(block,DetBodyOneWay)))
                    || (c.actions.collision.staticSlopes && std::get<std::uint32_t>(body.read(block,DetBodyShape))!=0)
                    || (claimed && std::get<std::uint32_t>(body.read(block,DetBodyMask))))invalid();}
            else if(mode!=2 || isTrigger)invalid();}
    }
    if(claimedCount>result.sequence || claimedCount>actionCount)invalid();
    if(result.operation==DetProgressOperation2D::Claim && (result.code==DetProgressCode2D::Applied || result.code==DetProgressCode2D::AlreadyClaimed)){
        const auto n=index(c.claims,result.id);if(!std::get<bool>(state.read(words,claimField(n)))
            || (result.code==DetProgressCode2D::Applied && std::get<std::uint64_t>(state.read(words,claimField(n,true)))!=stamp(result.tick)))invalid();}
    for(const auto& event:cp.pendingEvents)if(event.source==c.actions.collision.systemId && event.type==c.actions.collision.eventType){
        DetTriggerEvent2D decoded;if(!decodeDetTriggerEvent2D(event.payload,decoded))invalid();}
}
Plan access(DetTickContext& context,const DetProgress2DConfig& c){auto p=plan(c);matches(context,p.policies);matches(context,p.actionPolicies);
    const auto action=detActionResult2D(context,c.actions);const auto r=readResult(context,c);
    const auto maximum=stamp(context.tick()),actionStamp=action.sequence?stamp(action.tick):0;
    validResult(r,c,maximum,action.sequence,actionStamp);
    for(std::size_t n=0;n<c.counters.size();++n)validValue(context.readGlobal<std::uint64_t>(c.stateSchema,counterField(n)),c.counters[n]);
    std::uint64_t count=0;std::set<SimEntityId> triggers;
    for(std::size_t n=0;n<c.claims.size();++n){const bool claimed=context.readGlobal<bool>(c.stateSchema,claimField(n));
        const auto at=context.readGlobal<std::uint64_t>(c.stateSchema,claimField(n,true));
        if(claimed){if(!at || at>maximum || at>actionStamp || !triggers.insert(c.claims[n].trigger).second)invalid();++count;}else if(at)invalid();}
    if(count>r.sequence || count>action.sequence)invalid();return p;}
DetProgressResult2D record(DetTickContext& context,const DetProgress2DConfig& c,DetProgressResult2D r){
    const auto before=context.readGlobal<std::uint64_t>(c.stateSchema,DetProgressTotalCount);if(before==UINT64_MAX)invalid();r.sequence=before+1;r.tick=context.tick();
    const auto s=c.stateSchema;context.writeGlobal(s,DetProgressTotalCount,r.sequence);context.writeGlobal(s,DetProgressLastOperation,static_cast<std::uint32_t>(r.operation));
    context.writeGlobal(s,DetProgressLastId,r.id);context.writeGlobal(s,DetProgressLastCounter,r.counter);context.writeGlobal(s,DetProgressLastAmount,r.amount);
    context.writeGlobal(s,DetProgressLastCode,static_cast<std::uint32_t>(r.code));context.writeGlobal(s,DetProgressLastActionCode,static_cast<std::uint32_t>(r.actionCode));
    context.writeGlobal(s,DetProgressLastTick,stamp(r.tick));context.writeGlobal(s,DetProgressLastActionSequence,r.actionSequence);return r;}
bool entered(DetTickContext& context,const DetProgress2DConfig& c,const DetCollectibleClaim2D& claim){bool found=false;
    for(const auto& event:context.events())if(event.source==c.actions.collision.systemId && event.type==c.actions.collision.eventType){
        DetTriggerEvent2D decoded;if(!decodeDetTriggerEvent2D(event.payload,decoded))invalid();
        if(decoded.phase==DetTriggerPhase2D::Enter && decoded.pair.first==std::min(claim.trigger,claim.actor) && decoded.pair.second==std::max(claim.trigger,claim.actor))found=true;}return found;}
} // namespace
std::vector<std::uint8_t> encodeDetProgressConfig2D(const DetProgress2DConfig& c){return plan(c).bytes;}
DetTypedStateSchema detProgressStateSchema2D(const DetProgress2DConfig& c){return plan(c).state;}
std::uint32_t detProgressCounterField2D(const DetProgress2DConfig& c,std::uint32_t id){(void)plan(c);return counterField(index(c.counters,id));}
std::uint32_t detCollectibleField2D(const DetProgress2DConfig& c,std::uint32_t id,bool time){(void)plan(c);return claimField(index(c.claims,id),time);}
bool installDetProgress2D(DeterministicSession& session,DetProgress2DConfig c){Plan p;
    try{p=plan(c);if(session.sealed() || session.faulted() || session.nextTick())return false;}catch(const std::invalid_argument&){return false;}
    if(!(session.registerTypedSchema(p.state) && session.registerGlobalState(c.stateSchema)))return false;
    for(const auto& schema:p.policies)if(!(session.registerTypedSchema(schema) && session.registerGlobalState(schema.id)))return false;
    return session.registerLogicProfile(c.logicProfileId,1,logicHash) && session.registerValidator(c.validatorId,1,[c,p](const DetSessionCheckpoint& cp,std::string& error){
        try{validateCheckpoint(cp,c,p);return true;}catch(const std::exception& e){error=e.what();return false;}});}
DetProgressResult2D executeDetProgress2D(DetTickContext& context,const DetProgress2DConfig& c,DetProgressRequest2D request){
    (void)access(context,c);if(request.operation!=DetProgressOperation2D::Add && request.operation!=DetProgressOperation2D::Subtract)invalid();
    if(context.readGlobal<std::uint64_t>(c.stateSchema,DetProgressTotalCount)==UINT64_MAX)invalid();
    DetProgressResult2D r;r.operation=request.operation;r.id=r.counter=request.counter;r.amount=request.amount;
    const auto* counter=find(c.counters,request.counter);if(!counter)r.code=DetProgressCode2D::UnknownCounter;
    else {const auto f=counterField(index(c.counters,request.counter));const auto value=context.readGlobal<std::uint64_t>(c.stateSchema,f);validValue(value,*counter);
        if(!request.amount)r.code=DetProgressCode2D::Unchanged;
        else if(request.amount>(request.operation==DetProgressOperation2D::Add?counter->maximum-value:value-counter->minimum))r.code=DetProgressCode2D::OutOfRange;
        else {context.writeGlobal(c.stateSchema,f,request.operation==DetProgressOperation2D::Add?value+request.amount:value-request.amount);r.code=DetProgressCode2D::Applied;}}
    return record(context,c,r);}
DetProgressResult2D claimDetCollectible2D(DetTickContext& context,const DetProgress2DConfig& c,std::uint32_t id){
    (void)access(context,c);if(context.readGlobal<std::uint64_t>(c.stateSchema,DetProgressTotalCount)==UINT64_MAX)invalid();
    DetProgressResult2D r;r.operation=DetProgressOperation2D::Claim;r.id=id;const auto* claim=find(c.claims,id);
    if(!claim){r.code=DetProgressCode2D::UnknownClaim;return record(context,c,r);}r.counter=claim->counter;r.amount=claim->amount;
    const auto n=index(c.claims,id);if(context.readGlobal<bool>(c.stateSchema,claimField(n))){r.code=DetProgressCode2D::AlreadyClaimed;return record(context,c,r);}
    const auto actors=context.entities();if(!std::binary_search(actors.begin(),actors.end(),claim->trigger) || !std::binary_search(actors.begin(),actors.end(),claim->actor)){
        r.code=DetProgressCode2D::MissingActor;return record(context,c,r);}
    const auto s=c.actions.collision.bodySchema;
    if(!context.read<bool>(claim->trigger,s,DetBodyTrigger) || context.read<std::uint32_t>(claim->trigger,s,DetBodyMode)==0
        || context.read<std::uint32_t>(claim->actor,s,DetBodyMode)!=2 || context.read<bool>(claim->actor,s,DetBodyTrigger))invalid();
    if((c.actions.collision.oneWayPlatforms && context.read<bool>(claim->trigger,s,DetBodyOneWay))
        || (c.actions.collision.staticSlopes && context.read<std::uint32_t>(claim->trigger,s,DetBodyShape)!=0))invalid();
    const auto mask=context.read<std::uint32_t>(claim->trigger,s,DetBodyMask),authorized=target(c.actions,claim->trigger)->collisionMask;
    if(mask!=0 && mask!=authorized)invalid();
    const auto actorLayer=context.read<std::uint32_t>(claim->actor,s,DetBodyLayer),triggerLayer=context.read<std::uint32_t>(claim->trigger,s,DetBodyLayer);
    if(!actorLayer || !triggerLayer)invalid();
    if(!mask || !(mask&actorLayer) || !(context.read<std::uint32_t>(claim->actor,s,DetBodyMask)&triggerLayer)){
        r.code=DetProgressCode2D::InactiveTrigger;return record(context,c,r);}
    if(!entered(context,c,*claim)){r.code=DetProgressCode2D::NoMatchingEnter;return record(context,c,r);}
    const auto counterIndex=index(c.counters,claim->counter);const auto& counter=c.counters[counterIndex];const auto value=context.readGlobal<std::uint64_t>(c.stateSchema,counterField(counterIndex));validValue(value,counter);
    if(claim->amount>counter.maximum-value){r.code=DetProgressCode2D::OutOfRange;return record(context,c,r);}
    const auto action=executeDetAction2D(context,c.actions,{claim->trigger,DetActionKind2D::CollisionDisable});r.actionCode=action.code;r.actionSequence=action.sequence;
    if(action.code!=DetActionCode2D::Applied)r.code=DetProgressCode2D::ActionDenied;
    else {context.writeGlobal(c.stateSchema,counterField(counterIndex),value+claim->amount);context.writeGlobal(c.stateSchema,claimField(n),true);
        context.writeGlobal(c.stateSchema,claimField(n,true),stamp(context.tick()));r.code=DetProgressCode2D::Applied;}
    return record(context,c,r);}
DetProgressResult2D detProgressResult2D(DetTickContext& context,const DetProgress2DConfig& c){(void)access(context,c);return readResult(context,c);}
DetProgressResult2D detProgressResult2D(const DetSessionCheckpoint& cp,const DetProgress2DConfig& c){const auto p=plan(c);validateCheckpoint(cp,c,p);return readResult(cp,c,p);}
std::uint64_t detProgressCounter2D(DetTickContext& context,const DetProgress2DConfig& c,std::uint32_t id){(void)access(context,c);const auto n=index(c.counters,id);
    const auto value=context.readGlobal<std::uint64_t>(c.stateSchema,counterField(n));validValue(value,c.counters[n]);return value;}
std::uint64_t detProgressCounter2D(const DetSessionCheckpoint& cp,const DetProgress2DConfig& c,std::uint32_t id){const auto p=plan(c);validateCheckpoint(cp,c,p);
    return readDetState<std::uint64_t>(p.state,cp.globals.at(c.stateSchema),counterField(index(c.counters,id)));}
bool detProgressObjective2D(DetTickContext& context,const DetProgress2DConfig& c,std::uint32_t id){(void)access(context,c);const auto* objective=find(c.objectives,id);if(!objective)invalid();
    for(const auto& condition:objective->conditions){const auto n=index(c.counters,condition.counter);const auto value=context.readGlobal<std::uint64_t>(c.stateSchema,counterField(n));validValue(value,c.counters[n]);if(value<condition.atLeast)return false;}return true;}
bool detProgressObjective2D(const DetSessionCheckpoint& cp,const DetProgress2DConfig& c,std::uint32_t id){const auto p=plan(c);validateCheckpoint(cp,c,p);const auto* objective=find(c.objectives,id);if(!objective)invalid();
    for(const auto& condition:objective->conditions)if(readDetState<std::uint64_t>(p.state,cp.globals.at(c.stateSchema),counterField(index(c.counters,condition.counter)))<condition.atLeast)return false;return true;}
DetCollectibleState2D detCollectibleState2D(DetTickContext& context,const DetProgress2DConfig& c,std::uint32_t id){(void)access(context,c);const auto n=index(c.claims,id);
    const bool claimed=context.readGlobal<bool>(c.stateSchema,claimField(n));const auto at=context.readGlobal<std::uint64_t>(c.stateSchema,claimField(n,true));if(claimed?!at:at!=0)invalid();return {claimed,claimed?at-1:0};}
DetCollectibleState2D detCollectibleState2D(const DetSessionCheckpoint& cp,const DetProgress2DConfig& c,std::uint32_t id){const auto p=plan(c);validateCheckpoint(cp,c,p);const auto n=index(c.claims,id);
    const bool claimed=readDetState<bool>(p.state,cp.globals.at(c.stateSchema),claimField(n));const auto at=readDetState<std::uint64_t>(p.state,cp.globals.at(c.stateSchema),claimField(n,true));return {claimed,claimed?at-1:0};}
} // namespace ayt::entity
