// design reference: AYEntity/design.md Stage34 registered trigger interactions.
#include <AYEntity/DeterministicInteraction2D.h>
#include "DetDebugPolicies2D.h"
#include <algorithm>
#include <set>
#include <stdexcept>

namespace ayt::entity {
namespace {
constexpr std::size_t chunkBytes=1000;
[[noreturn]] void invalid(){throw std::invalid_argument("deterministic interaction 2D: invalid configuration, policy, event or registered state");}
std::uint64_t add(std::uint64_t a,std::uint64_t b){if(a>UINT64_MAX-b)invalid();return a+b;}
std::uint32_t field(std::size_t index,DetInteractionField2D member){return static_cast<std::uint32_t>((index+1)*100)+member;}
struct Writer {
    std::vector<std::uint8_t> bytes;
    void put(std::uint64_t value,unsigned count){for(unsigned n=0;n<count;++n)bytes.push_back(static_cast<std::uint8_t>(value>>(n*8)));}
    void u32(std::uint32_t value){put(value,4);}void u64(std::uint64_t value){put(value,8);}
};
struct Plan {
    std::vector<std::uint8_t> bytes;
    DetTypedStateSchema state,body,actionState;
    std::vector<DetTypedStateSchema> policies,actionPolicies;
};
std::size_t find(const DetInteraction2DConfig& c,std::uint32_t id){
    const auto it=std::lower_bound(c.rules.begin(),c.rules.end(),id,[](const auto& rule,auto value){return rule.id<value;});
    if(it==c.rules.end() || it->id!=id)invalid();return static_cast<std::size_t>(it-c.rules.begin());
}
Plan plan(const DetInteraction2DConfig& c){
    if(c.stateSchema<2 || c.stateSchema==UINT32_MAX || c.policySchemaBase<2 || c.policySchemaBase==UINT32_MAX
        || !c.systemId || !c.validatorId || !c.logicProfileId || c.rules.empty() || c.rules.size()>kDetInteraction2DMaxRules
        || c.systemId>=c.actions.collision.systemId || c.validatorId==c.actions.validatorId
        || c.validatorId==c.actions.collision.systemId || c.logicProfileId==c.actions.logicProfileId)invalid();
    Plan p;p.body=detCollisionBodySchema2D(c.actions.collision);
    p.actionState=detActionsStateSchema2D(c.actions);p.actionPolicies=detActionsPolicySchemas2D(c.actions);
    const auto actionBytes=encodeDetActionsConfig2D(c.actions);
    Writer w;w.u32(kDetInteraction2DProfileVersion);w.u32(c.stateSchema);w.u32(c.policySchemaBase);
    w.u32(c.systemId);w.u32(c.validatorId);w.u32(c.logicProfileId);
    w.u32(static_cast<std::uint32_t>(actionBytes.size()));w.bytes.insert(w.bytes.end(),actionBytes.begin(),actionBytes.end());
    w.u32(static_cast<std::uint32_t>(c.rules.size()));p.state={c.stateSchema,1,{}};
    std::uint32_t previous=0;
    for(std::size_t i=0;i<c.rules.size();++i){const auto& rule=c.rules[i];const auto phase=static_cast<std::uint32_t>(rule.phase),kind=static_cast<std::uint32_t>(rule.action.kind);
        if(!rule.id || rule.id<=previous || !rule.trigger || !rule.actor || rule.trigger==rule.actor
            || (phase!=1 && phase!=3) || !rule.action.target || kind<1 || kind>7
            || rule.cooldownTicks>65535 || rule.delayTicks>65535)invalid();previous=rule.id;
        w.u32(rule.id);w.u64(rule.trigger);w.u64(rule.actor);w.u32(phase);w.u64(rule.action.target);w.u32(kind);
        w.u32(rule.once?1:0);w.u32(rule.cooldownTicks);w.u32(rule.delayTicks);
        p.state.fields.insert(p.state.fields.end(),{
            {field(i,DetInteractionFired),false},{field(i,DetInteractionPending),false},
            {field(i,DetInteractionScheduleCount),std::uint64_t{}},{field(i,DetInteractionAttemptCount),std::uint64_t{}},
            {field(i,DetInteractionLastScheduleTick),std::uint64_t{}},{field(i,DetInteractionDueTick),std::uint64_t{}},
            {field(i,DetInteractionLastExecutionTick),std::uint64_t{}},{field(i,DetInteractionLastResult),std::uint32_t{}}});
    }
    p.bytes=std::move(w.bytes);if(p.bytes.size()>kDetInteraction2DMaxPolicyBytes)invalid();
    const auto chunks=(p.bytes.size()+chunkBytes-1)/chunkBytes;
    if(!chunks || chunks>10 || c.policySchemaBase>UINT32_MAX-chunks)invalid();
    std::set<std::uint32_t> schemas{c.actions.collision.bodySchema,c.actions.collision.historySchema};
    if(!schemas.insert(c.actions.stateSchema).second)invalid();
    for(const auto& schema:p.actionPolicies)if(!schemas.insert(schema.id).second)invalid();
    if(!schemas.insert(c.stateSchema).second)invalid();
    for(std::size_t i=0;i<chunks;++i){const auto id=c.policySchemaBase+static_cast<std::uint32_t>(i);
        if(!schemas.insert(id).second)invalid();
        DetTypedStateSchema policy{id,1,{{1,static_cast<std::uint32_t>(i)},{2,static_cast<std::uint32_t>(chunks)},
            {3,static_cast<std::uint32_t>(p.bytes.size())}}};
        const auto begin=i*chunkBytes,end=std::min(begin+chunkBytes,p.bytes.size());
        for(std::size_t offset=begin;offset<end;offset+=8){std::uint64_t word=0;
            for(std::size_t b=0;b<8 && offset+b<end;++b)word|=std::uint64_t{p.bytes[offset+b]}<<(8*b);
            policy.fields.push_back({static_cast<std::uint32_t>(4+(offset-begin)/8),word});}
        p.policies.push_back(std::move(policy));
    }
    for(const auto& target:c.actions.targets)if(target.path){const auto& path=*target.path;
        if(c.systemId>=path.preSystemId || c.systemId==path.postSystemId
            || c.validatorId==path.validatorId || c.logicProfileId==path.logicProfileId
            || schemas.contains(path.stateSchema) || schemas.contains(path.policySchema))invalid();
    }
    (void)detStateDefaults(p.state);return p;
}
void matches(DetTickContext& context,const std::vector<DetTypedStateSchema>& policies){
    for(const auto& schema:policies)for(const auto& f:schema.fields)std::visit([&]<class T>(const T& expected){
        if(encodeDetStateValue(context.readGlobal<T>(schema.id,f.id))!=encodeDetStateValue(expected))invalid();},f.initial);
}
void matches(const DetSessionCheckpoint& cp,const std::vector<DetTypedStateSchema>& policies){
    for(const auto& schema:policies){const auto expected=detStateDefaults(schema);
        if(cp.globals.at(schema.id)!=expected)invalid();
        for(const auto& [id,actor]:cp.actors){(void)id;if(actor.blocks.at(schema.id)!=expected)invalid();}}
}
DetInteractionState2D read(DetTickContext& context,const DetInteraction2DConfig& c,std::size_t i){const auto s=c.stateSchema;
    DetInteractionState2D r;r.fired=context.readGlobal<bool>(s,field(i,DetInteractionFired));r.pending=context.readGlobal<bool>(s,field(i,DetInteractionPending));
    r.scheduleCount=context.readGlobal<std::uint64_t>(s,field(i,DetInteractionScheduleCount));
    r.attemptCount=context.readGlobal<std::uint64_t>(s,field(i,DetInteractionAttemptCount));
    r.lastScheduleTick=context.readGlobal<std::uint64_t>(s,field(i,DetInteractionLastScheduleTick));
    r.dueTick=context.readGlobal<std::uint64_t>(s,field(i,DetInteractionDueTick));
    r.lastExecutionTick=context.readGlobal<std::uint64_t>(s,field(i,DetInteractionLastExecutionTick));
    r.lastResult=static_cast<DetActionCode2D>(context.readGlobal<std::uint32_t>(s,field(i,DetInteractionLastResult)));
    if(r.scheduleCount)r.cooldownUntil=add(r.lastScheduleTick,c.rules[i].cooldownTicks);return r;
}
DetInteractionState2D read(const DetSessionCheckpoint& cp,const DetInteraction2DConfig& c,const Plan& p,std::size_t i){
    const DetStateLayout layout(p.state);const auto& words=cp.globals.at(c.stateSchema);
    const auto get=[&]<class T>(DetInteractionField2D f){return std::get<T>(layout.read(words,field(i,f)));};
    DetInteractionState2D r;r.fired=get.operator()<bool>(DetInteractionFired);r.pending=get.operator()<bool>(DetInteractionPending);
    r.scheduleCount=get.operator()<std::uint64_t>(DetInteractionScheduleCount);r.attemptCount=get.operator()<std::uint64_t>(DetInteractionAttemptCount);
    r.lastScheduleTick=get.operator()<std::uint64_t>(DetInteractionLastScheduleTick);r.dueTick=get.operator()<std::uint64_t>(DetInteractionDueTick);
    r.lastExecutionTick=get.operator()<std::uint64_t>(DetInteractionLastExecutionTick);r.lastResult=static_cast<DetActionCode2D>(get.operator()<std::uint32_t>(DetInteractionLastResult));
    if(r.scheduleCount)r.cooldownUntil=add(r.lastScheduleTick,c.rules[i].cooldownTicks);return r;
}
void valid(const DetInteractionState2D& r,const DetInteractionRule2D& rule,std::uint64_t nextTick,std::uint64_t pendingFloor){
    const auto code=static_cast<std::uint32_t>(r.lastResult);
    if(!r.scheduleCount){if(r.fired || r.pending || r.attemptCount || r.lastScheduleTick || r.dueTick || r.lastExecutionTick || code || r.cooldownUntil)invalid();return;}
    if(!r.fired || r.lastScheduleTick>=nextTick || (rule.once && r.scheduleCount!=1) || r.attemptCount>r.scheduleCount
        || r.cooldownUntil!=add(r.lastScheduleTick,rule.cooldownTicks))invalid();
    const auto due=add(r.lastScheduleTick,rule.delayTicks);
    if(r.pending){if(!rule.delayTicks || r.attemptCount==UINT64_MAX || r.scheduleCount!=r.attemptCount+1
            || r.dueTick!=due || r.dueTick<pendingFloor || (r.attemptCount && r.lastExecutionTick>r.lastScheduleTick))invalid();}
    else if(r.scheduleCount!=r.attemptCount || r.dueTick || r.lastExecutionTick!=due)invalid();
    if(r.attemptCount){if(code<1 || code>8 || r.lastExecutionTick>=nextTick)invalid();}
    else if(code || r.lastExecutionTick)invalid();
}
void write(DetTickContext& context,const DetInteraction2DConfig& c,std::size_t i,const DetInteractionState2D& r){const auto s=c.stateSchema;
    context.writeGlobal(s,field(i,DetInteractionFired),r.fired);context.writeGlobal(s,field(i,DetInteractionPending),r.pending);
    context.writeGlobal(s,field(i,DetInteractionScheduleCount),r.scheduleCount);context.writeGlobal(s,field(i,DetInteractionAttemptCount),r.attemptCount);
    context.writeGlobal(s,field(i,DetInteractionLastScheduleTick),r.lastScheduleTick);context.writeGlobal(s,field(i,DetInteractionDueTick),r.dueTick);
    context.writeGlobal(s,field(i,DetInteractionLastExecutionTick),r.lastExecutionTick);context.writeGlobal(s,field(i,DetInteractionLastResult),static_cast<std::uint32_t>(r.lastResult));
}
void execute(DetTickContext& context,const DetInteraction2DConfig& c,std::size_t i,DetInteractionState2D& r){
    if(r.attemptCount==UINT64_MAX)invalid();
    const auto result=executeDetAction2D(context,c.actions,c.rules[i].action);
    ++r.attemptCount;r.pending=false;r.dueTick=0;r.lastExecutionTick=context.tick();r.lastResult=result.code;
}
void validateCheckpoint(const DetSessionCheckpoint& cp,const DetInteraction2DConfig& c,const Plan& p){
    matches(cp,p.policies);matches(cp,p.actionPolicies);
    for(const auto& event:cp.pendingEvents)if(event.source==c.actions.collision.systemId && event.type==c.actions.collision.eventType){
        DetTriggerEvent2D decoded;if(!decodeDetTriggerEvent2D(event.payload,decoded))invalid();}
    const DetStateLayout state(p.state),action(p.actionState),body(p.body);
    if(!state.valid(cp.globals.at(c.stateSchema)) || !action.valid(cp.globals.at(c.actions.stateSchema)))invalid();
    for(const auto& [id,actor]:cp.actors){(void)id;
        if(actor.blocks.at(c.stateSchema)!=state.defaults() || actor.blocks.at(c.actions.stateSchema)!=action.defaults())invalid();}
    std::uint64_t attempts=0;const auto lastActionStamp=std::get<std::uint64_t>(action.read(cp.globals.at(c.actions.stateSchema),DetActionsLastTick));
    for(std::size_t i=0;i<c.rules.size();++i){const auto record=read(cp,c,p,i);valid(record,c.rules[i],cp.nextTick,cp.nextTick);
        attempts=add(attempts,record.attemptCount);
        if(record.attemptCount && (!lastActionStamp || record.lastExecutionTick>=lastActionStamp))invalid();
        const auto& rule=c.rules[i];
        for(const auto id:{rule.trigger,rule.actor}){const auto it=cp.actors.find(id);
            if(it==cp.actors.end()){
                if(!cp.nextTick || !std::binary_search(cp.retiredIds.begin(),cp.retiredIds.end(),id))invalid();continue;}
            const auto& block=it->second.blocks.at(c.actions.collision.bodySchema);
            const auto mode=std::get<std::uint32_t>(body.read(block,DetBodyMode));const bool trigger=std::get<bool>(body.read(block,DetBodyTrigger));
            if(id==rule.trigger){if(mode==static_cast<std::uint32_t>(DetBodyMode2D::Disabled) || !trigger)invalid();}
            else if(mode!=static_cast<std::uint32_t>(DetBodyMode2D::Kinematic) || trigger)invalid();
        }
    }
    if(attempts>std::get<std::uint64_t>(action.read(cp.globals.at(c.actions.stateSchema),DetActionsTotalCount)))invalid();
}
bool run(DetTickContext& context,const DetInteraction2DConfig& c,const Plan& p){
    matches(context,p.policies);matches(context,p.actionPolicies);
    const auto next=add(context.tick(),1);
    // Due jobs have a total order independent of event production this tick.
    for(std::size_t i=0;i<c.rules.size();++i){auto r=read(context,c,i);valid(r,c.rules[i],next,context.tick());
        if(r.pending && r.dueTick==context.tick()){execute(context,c,i,r);write(context,c,i,r);}}
    for(const auto& event:context.events()){
        if(event.source!=c.actions.collision.systemId || event.type!=c.actions.collision.eventType)continue;
        DetTriggerEvent2D decoded;if(!decodeDetTriggerEvent2D(event.payload,decoded))invalid();
        if(decoded.phase==DetTriggerPhase2D::Stay)continue;
        for(std::size_t i=0;i<c.rules.size();++i){const auto& rule=c.rules[i];
            const auto first=std::min(rule.trigger,rule.actor),second=std::max(rule.trigger,rule.actor);
            if(decoded.phase!=rule.phase || decoded.pair.first!=first || decoded.pair.second!=second)continue;
            auto r=read(context,c,i);if(r.pending || (rule.once && r.fired) || (r.scheduleCount && context.tick()<r.cooldownUntil))continue;
            if(r.scheduleCount==UINT64_MAX)invalid();const auto due=add(context.tick(),rule.delayTicks),cooldown=add(context.tick(),rule.cooldownTicks);
            ++r.scheduleCount;r.fired=true;r.lastScheduleTick=context.tick();r.cooldownUntil=cooldown;r.pending=true;r.dueTick=due;
            if(!rule.delayTicks)execute(context,c,i,r);write(context,c,i,r);
        }
    }
    return true;
}
} // namespace
std::vector<std::uint8_t> encodeDetInteractionConfig2D(const DetInteraction2DConfig& c){return plan(c).bytes;}
DetTypedStateSchema detInteractionStateSchema2D(const DetInteraction2DConfig& c){return plan(c).state;}
std::uint32_t detInteractionField2D(const DetInteraction2DConfig& c,std::uint32_t id,DetInteractionField2D member){
    (void)plan(c);switch(member){case DetInteractionFired:case DetInteractionPending:case DetInteractionScheduleCount:
        case DetInteractionAttemptCount:case DetInteractionLastScheduleTick:case DetInteractionDueTick:
        case DetInteractionLastExecutionTick:case DetInteractionLastResult:break;default:invalid();}
    return field(find(c,id),member);
}
bool installDetInteraction2D(DeterministicSession& session,DetInteraction2DConfig c){
    Plan p;try{p=plan(c);if(session.sealed() || session.faulted() || session.nextTick())return false;}
    catch(const std::invalid_argument&){return false;}
    if(!(session.registerTypedSchema(p.state) && session.registerGlobalState(c.stateSchema)))return false;
    for(const auto& schema:p.policies)if(!(session.registerTypedSchema(schema) && session.registerGlobalState(schema.id)))return false;
    const bool installed=session.registerLogicProfile(c.logicProfileId,1,0x4449523244503031ull)
        && session.registerValidator(c.validatorId,1,[c,p](const DetSessionCheckpoint& cp,std::string& error){
            try{validateCheckpoint(cp,c,p);return true;}catch(const std::exception& e){error=e.what();return false;}})
        && session.registerSystem(c.systemId,c.actions.collision.priority,[c,p](DetTickContext& context){return run(context,c,p);});
    if(installed)DetDebugAccess::retain(session,c);return installed;
}
DetInteractionState2D detInteractionState2D(DetTickContext& context,const DetInteraction2DConfig& c,std::uint32_t id){
    const auto p=plan(c);matches(context,p.policies);matches(context,p.actionPolicies);const auto i=find(c,id);auto r=read(context,c,i);
    valid(r,c.rules[i],add(context.tick(),1),context.tick());return r;
}
DetInteractionState2D detInteractionState2D(const DetSessionCheckpoint& cp,const DetInteraction2DConfig& c,std::uint32_t id){
    const auto p=plan(c);validateCheckpoint(cp,c,p);return read(cp,c,p,find(c,id));
}
} // namespace ayt::entity
