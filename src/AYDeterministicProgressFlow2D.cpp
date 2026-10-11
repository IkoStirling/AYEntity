// design reference: AYEntity/design.md Stage41 finite registered progress routing.
#include <AYEntity/DeterministicProgressFlow2D.h>
#include "detail/DetSessionWire.h"
#include "detail/DetPathMotion2DIdentity.h"
#include <algorithm>
#include <set>
#include <stdexcept>

namespace ayt::entity {
namespace {
constexpr std::size_t chunkBytes=1000;
constexpr std::uint64_t logicHash=0x4450463244503031ull;
[[noreturn]] void invalid(){throw std::invalid_argument("deterministic progress flow 2D: invalid configuration, policy, event or registered state");}
std::uint64_t add(std::uint64_t a,std::uint64_t b){if(a>UINT64_MAX-b)invalid();return a+b;}
std::uint64_t stamp(std::uint64_t tick){return add(tick,1);}
std::uint32_t field(std::size_t index,std::uint32_t member){return static_cast<std::uint32_t>(100*(index+1))+member;}
template<class T>const T* find(const std::vector<T>& rows,std::uint32_t id){
    const auto it=std::lower_bound(rows.begin(),rows.end(),id,[](const auto& row,auto value){return row.id<value;});return it!=rows.end() && it->id==id?&*it:nullptr;}
std::size_t claimIndex(const DetProgressFlow2DConfig& c,std::uint32_t id){
    const auto it=std::lower_bound(c.claims.begin(),c.claims.end(),id,[](const auto& row,auto value){return row.claim<value;});
    if(it==c.claims.end() || it->claim!=id)invalid();return static_cast<std::size_t>(it-c.claims.begin());}
std::size_t ruleIndex(const DetProgressFlow2DConfig& c,std::uint32_t id){const auto* row=find(c.rules,id);if(!row)invalid();return static_cast<std::size_t>(row-c.rules.data());}
struct Writer {std::vector<std::uint8_t> bytes;
    void put(std::uint64_t v,unsigned count){for(unsigned n=0;n<count;++n)bytes.push_back(static_cast<std::uint8_t>(v>>(8*n)));}
    void u32(std::uint32_t v){put(v,4);}void u64(std::uint64_t v){put(v,8);}};
struct Plan {std::vector<std::uint8_t> bytes;DetTypedStateSchema collect,objective,actions;
    std::vector<DetTypedStateSchema> policies,pathPolicies;};
Plan plan(const DetProgressFlow2DConfig& c){
    if(c.collectStateSchema<2 || c.collectStateSchema==UINT32_MAX || c.objectiveStateSchema<2 || c.objectiveStateSchema==UINT32_MAX
        || c.policySchemaBase<2 || c.policySchemaBase==UINT32_MAX || !c.systemId || !c.validatorId || !c.logicProfileId
        || (c.claims.empty() && c.rules.empty()) || c.claims.size()>kDetProgressFlow2DMaxClaims || c.rules.size()>kDetProgressFlow2DMaxRules
        || c.systemId>=c.progress.actions.collision.systemId || c.validatorId==c.progress.validatorId
        || c.validatorId==c.progress.actions.validatorId || c.validatorId==c.progress.actions.collision.systemId
        || c.logicProfileId==c.progress.logicProfileId || c.logicProfileId==c.progress.actions.logicProfileId)invalid();
    const auto progressBytes=encodeDetProgressConfig2D(c.progress);const auto progressState=detProgressStateSchema2D(c.progress);
    const auto actionPolicies=detActionsPolicySchemas2D(c.progress.actions);Plan p;p.actions=detActionsStateSchema2D(c.progress.actions);
    p.collect={c.collectStateSchema,1,{{1,std::uint64_t{}}}};p.objective={c.objectiveStateSchema,1,{{1,std::uint64_t{}}}};
    Writer w;w.u32(kDetProgressFlow2DProfileVersion);w.u32(c.collectStateSchema);w.u32(c.objectiveStateSchema);
    w.u32(c.policySchemaBase);w.u32(c.systemId);w.u32(c.validatorId);w.u32(c.logicProfileId);
    w.u32(static_cast<std::uint32_t>(progressBytes.size()));w.bytes.insert(w.bytes.end(),progressBytes.begin(),progressBytes.end());
    w.u32(static_cast<std::uint32_t>(c.claims.size()));std::uint32_t previous=0;
    for(std::size_t n=0;n<c.claims.size();++n){const auto& binding=c.claims[n];
        if(!binding.claim || binding.claim<=previous || !find(c.progress.claims,binding.claim)
            || (binding.requiredObjective && !find(c.progress.objectives,binding.requiredObjective)))invalid();previous=binding.claim;
        w.u32(binding.claim);w.u32(binding.requiredObjective);
        p.collect.fields.insert(p.collect.fields.end(),{{field(n,DetFlowClaimAttemptCount),std::uint64_t{}},
            {field(n,DetFlowClaimLastTick),std::uint64_t{}},{field(n,DetFlowClaimProgressSequence),std::uint64_t{}},
            {field(n,DetFlowClaimLastResult),std::uint32_t{}},{field(n,DetFlowClaimActionAttemptCount),std::uint64_t{}},
            {field(n,DetFlowClaimLastActionCode),std::uint32_t{}},{field(n,DetFlowClaimLastActionSequence),std::uint64_t{}}});}
    w.u32(static_cast<std::uint32_t>(c.rules.size()));previous=0;
    for(std::size_t n=0;n<c.rules.size();++n){const auto& rule=c.rules[n];const auto kind=static_cast<std::uint32_t>(rule.action.kind);
        const auto controlled=std::find_if(c.progress.actions.targets.begin(),c.progress.actions.targets.end(),[&](const auto& row){return row.actor==rule.action.target;});
        if(!rule.id || rule.id<=previous || !find(c.progress.objectives,rule.objective) || controlled==c.progress.actions.targets.end() || kind<1 || kind>7)invalid();previous=rule.id;
        w.u32(rule.id);w.u32(rule.objective);w.u64(rule.action.target);w.u32(kind);w.u32(rule.once?1:0);
        p.objective.fields.insert(p.objective.fields.end(),{{field(n,DetFlowPreviousSatisfied),false},{field(n,DetFlowOnceConsumed),false},
            {field(n,DetFlowObjectiveAttemptCount),std::uint64_t{}},{field(n,DetFlowObjectiveLastTick),std::uint64_t{}},
            {field(n,DetFlowObjectiveLastResult),std::uint32_t{}},{field(n,DetFlowObjectiveActionSequence),std::uint64_t{}}});}
    p.bytes=std::move(w.bytes);if(p.bytes.size()>kDetProgressFlow2DMaxPolicyBytes)invalid();
    const auto chunks=(p.bytes.size()+chunkBytes-1)/chunkBytes;if(!chunks || chunks>13 || c.policySchemaBase>UINT32_MAX-chunks)invalid();
    std::set<std::uint32_t> schemas;const auto reserve=[&](std::uint32_t id){if(!schemas.insert(id).second)invalid();};
    reserve(c.progress.actions.collision.bodySchema);reserve(c.progress.actions.collision.historySchema);reserve(p.actions.id);
    for(const auto& s:actionPolicies)reserve(s.id);reserve(progressState.id);
    for(std::size_t n=0;n<(progressBytes.size()+chunkBytes-1)/chunkBytes;++n)reserve(c.progress.policySchemaBase+static_cast<std::uint32_t>(n));
    std::map<std::uint32_t,std::uint32_t> pathStates;
    for(const auto& controlled:c.progress.actions.targets)if(controlled.path){const auto& path=*controlled.path;
        if(c.systemId>=path.preSystemId || c.systemId==path.postSystemId || c.validatorId==path.validatorId || c.logicProfileId==path.logicProfileId)invalid();
        // Multiple targets can bind the same installed named path.
        const auto existing=std::find_if(p.pathPolicies.begin(),p.pathPolicies.end(),[&](const auto& s){return s.id==path.policySchema;});
        const auto expected=detPathMotionPolicySchema2D(path);
        if(existing==p.pathPolicies.end()){
            reserve(path.stateSchema);reserve(path.policySchema);pathStates.emplace(path.policySchema,path.stateSchema);p.pathPolicies.push_back(expected);}
        else if(pathStates.at(path.policySchema)!=path.stateSchema || detStateDefaults(*existing)!=detStateDefaults(expected))invalid();}
    reserve(c.collectStateSchema);reserve(c.objectiveStateSchema);
    for(std::size_t n=0;n<chunks;++n){const auto id=c.policySchemaBase+static_cast<std::uint32_t>(n);reserve(id);
        DetTypedStateSchema s{id,1,{{1,static_cast<std::uint32_t>(n)},{2,static_cast<std::uint32_t>(chunks)},{3,static_cast<std::uint32_t>(p.bytes.size())}}};
        const auto begin=n*chunkBytes,end=std::min(begin+chunkBytes,p.bytes.size());
        for(std::size_t offset=begin;offset<end;offset+=8){std::uint64_t word=0;for(std::size_t b=0;b<8 && offset+b<end;++b)word|=std::uint64_t{p.bytes[offset+b]}<<(8*b);
            s.fields.push_back({static_cast<std::uint32_t>(4+(offset-begin)/8),word});}p.policies.push_back(std::move(s));}
    (void)detStateDefaults(p.collect);(void)detStateDefaults(p.objective);return p;
}
void policiesMatch(DetTickContext& ctx,const Plan& p){for(const auto& s:p.policies)for(const auto& f:s.fields)std::visit([&]<class T>(const T& expected){
    if(encodeDetStateValue(ctx.readGlobal<T>(s.id,f.id))!=encodeDetStateValue(expected))invalid();},f.initial);}
void descriptor(const detwire::ManifestLayout& m,const DetTypedStateSchema& s,bool requireGlobal=true){
    if(m.schemaVersions.at(s.id)!=s.version || (requireGlobal && !m.globals.contains(s.id)))invalid();const auto& actual=m.schemas.at(s.id);
    if(actual.size()!=s.fields.size())invalid();for(std::size_t n=0;n<actual.size();++n)if(actual[n].id!=s.fields[n].id
        || actual[n].type!=detStateType(s.fields[n].initial) || actual[n].width!=encodeDetStateValue(s.fields[n].initial).size())invalid();}
void policiesMatch(const DetSessionCheckpoint& cp,const detwire::ManifestLayout& m,const std::vector<DetTypedStateSchema>& policies){
    for(const auto& s:policies){descriptor(m,s);const auto expected=detStateDefaults(s);if(cp.globals.at(s.id)!=expected)invalid();
        for(const auto& [id,a]:cp.actors){(void)id;if(a.blocks.at(s.id)!=expected)invalid();}}}
template<class Getter>DetProgressFlowClaimState2D readClaim(Getter&& get){DetProgressFlowClaimState2D r;
    r.attemptCount=get.template operator()<std::uint64_t>(DetFlowClaimAttemptCount);const auto last=get.template operator()<std::uint64_t>(DetFlowClaimLastTick);
    r.progressSequence=get.template operator()<std::uint64_t>(DetFlowClaimProgressSequence);r.lastResult=static_cast<DetProgressCode2D>(get.template operator()<std::uint32_t>(DetFlowClaimLastResult));
    r.actionAttemptCount=get.template operator()<std::uint64_t>(DetFlowClaimActionAttemptCount);
    r.lastActionCode=static_cast<DetActionCode2D>(get.template operator()<std::uint32_t>(DetFlowClaimLastActionCode));
    r.lastActionSequence=get.template operator()<std::uint64_t>(DetFlowClaimLastActionSequence);
    if(r.attemptCount){if(!last)invalid();r.lastTick=last-1;}else if(last)invalid();return r;}
template<class Getter>DetProgressFlowObjectiveState2D readObjective(Getter&& get){DetProgressFlowObjectiveState2D r;
    r.previousSatisfied=get.template operator()<bool>(DetFlowPreviousSatisfied);r.onceConsumed=get.template operator()<bool>(DetFlowOnceConsumed);
    r.attemptCount=get.template operator()<std::uint64_t>(DetFlowObjectiveAttemptCount);const auto last=get.template operator()<std::uint64_t>(DetFlowObjectiveLastTick);
    r.lastResult=static_cast<DetActionCode2D>(get.template operator()<std::uint32_t>(DetFlowObjectiveLastResult));
    r.actionSequence=get.template operator()<std::uint64_t>(DetFlowObjectiveActionSequence);
    if(r.attemptCount){if(!last)invalid();r.lastTick=last-1;}else if(last)invalid();return r;}
DetProgressFlowClaimState2D readClaim(DetTickContext& ctx,const DetProgressFlow2DConfig& c,std::size_t n){
    return readClaim([&]<class T>(auto member){return ctx.readGlobal<T>(c.collectStateSchema,field(n,member));});}
DetProgressFlowObjectiveState2D readObjective(DetTickContext& ctx,const DetProgressFlow2DConfig& c,std::size_t n){
    return readObjective([&]<class T>(auto member){return ctx.readGlobal<T>(c.objectiveStateSchema,field(n,member));});}
DetProgressFlowClaimState2D readClaim(const DetSessionCheckpoint& cp,const Plan& p,std::size_t n){const DetStateLayout l(p.collect);
    return readClaim([&]<class T>(auto member){return std::get<T>(l.read(cp.globals.at(p.collect.id),field(n,member)));});}
DetProgressFlowObjectiveState2D readObjective(const DetSessionCheckpoint& cp,const Plan& p,std::size_t n){const DetStateLayout l(p.objective);
    return readObjective([&]<class T>(auto member){return std::get<T>(l.read(cp.globals.at(p.objective.id),field(n,member)));});}
void valid(const DetProgressFlowClaimState2D& r,std::uint64_t maximumStamp,std::uint64_t progressCount,std::uint64_t actionsCount,std::uint64_t actionsStamp){
    const auto code=static_cast<std::uint32_t>(r.lastResult),action=static_cast<std::uint32_t>(r.lastActionCode);
    if(!r.attemptCount){if(r.lastTick || r.progressSequence || code || r.actionAttemptCount || action || r.lastActionSequence)invalid();return;}
    if(stamp(r.lastTick)>maximumStamp || r.attemptCount>stamp(r.lastTick) || r.progressSequence<r.attemptCount || r.progressSequence>progressCount || code<1 || code>10
        || code==2 || code==3 || code==5 || code==8 || r.actionAttemptCount>r.attemptCount || r.actionAttemptCount>actionsCount || action>8
        || bool(action)!=bool(r.lastActionSequence) || r.lastActionSequence>actionsCount
        || (action && (!r.actionAttemptCount || stamp(r.lastTick)>actionsStamp))
        || (code==1 && action!=1) || (code==10 && action<2) || (code!=1 && code!=10 && action))invalid();}
void valid(const DetProgressFlowObjectiveState2D& r,const DetProgressFlowRule2D& rule,std::uint64_t maximumStamp,std::uint64_t actionsCount,std::uint64_t actionsStamp){
    const auto code=static_cast<std::uint32_t>(r.lastResult);
    if(!r.attemptCount){if(r.previousSatisfied || r.onceConsumed || r.lastTick || code || r.actionSequence)invalid();return;}
    if(stamp(r.lastTick)>maximumStamp || r.attemptCount>stamp(r.lastTick) || r.onceConsumed!=(rule.once && r.attemptCount>0) || (rule.once && r.attemptCount!=1)
        || code<1 || code>8 || !r.actionSequence || r.actionSequence<r.attemptCount || r.actionSequence>actionsCount || stamp(r.lastTick)>actionsStamp)invalid();}
void write(DetTickContext& ctx,const DetProgressFlow2DConfig& c,std::size_t n,const DetProgressFlowClaimState2D& r){const auto s=c.collectStateSchema;
    ctx.writeGlobal(s,field(n,DetFlowClaimAttemptCount),r.attemptCount);ctx.writeGlobal(s,field(n,DetFlowClaimLastTick),r.attemptCount?stamp(r.lastTick):0);
    ctx.writeGlobal(s,field(n,DetFlowClaimProgressSequence),r.progressSequence);ctx.writeGlobal(s,field(n,DetFlowClaimLastResult),static_cast<std::uint32_t>(r.lastResult));
    ctx.writeGlobal(s,field(n,DetFlowClaimActionAttemptCount),r.actionAttemptCount);ctx.writeGlobal(s,field(n,DetFlowClaimLastActionCode),static_cast<std::uint32_t>(r.lastActionCode));
    ctx.writeGlobal(s,field(n,DetFlowClaimLastActionSequence),r.lastActionSequence);}
void write(DetTickContext& ctx,const DetProgressFlow2DConfig& c,std::size_t n,const DetProgressFlowObjectiveState2D& r){const auto s=c.objectiveStateSchema;
    ctx.writeGlobal(s,field(n,DetFlowPreviousSatisfied),r.previousSatisfied);ctx.writeGlobal(s,field(n,DetFlowOnceConsumed),r.onceConsumed);
    ctx.writeGlobal(s,field(n,DetFlowObjectiveAttemptCount),r.attemptCount);ctx.writeGlobal(s,field(n,DetFlowObjectiveLastTick),r.attemptCount?stamp(r.lastTick):0);
    ctx.writeGlobal(s,field(n,DetFlowObjectiveLastResult),static_cast<std::uint32_t>(r.lastResult));ctx.writeGlobal(s,field(n,DetFlowObjectiveActionSequence),r.actionSequence);}
void validateCheckpoint(const DetSessionCheckpoint& cp,const DetProgressFlow2DConfig& c,const Plan& p){
    const auto progress=detProgressResult2D(cp,c.progress);const auto m=detwire::manifestLayout(cp.manifest);
    if(m.systems.at(c.systemId)!=c.progress.actions.collision.priority || m.validatorVersions.at(c.validatorId)!=1
        || m.logicProfiles.at(c.logicProfileId)!=std::pair<std::uint32_t,std::uint64_t>{1,logicHash})invalid();
    descriptor(m,p.collect);descriptor(m,p.objective);policiesMatch(cp,m,p.policies);policiesMatch(cp,m,p.pathPolicies);
    for(const auto& target:c.progress.actions.targets)if(target.path){const auto& path=*target.path;
        const auto identity=detail::pathMotionIdentity2D(path);descriptor(m,detPathMotionStateSchema2D(path),false);
        if(m.systems.at(path.preSystemId)!=path.collision.priority || m.systems.at(path.postSystemId)!=path.collision.priority
            || m.validatorVersions.at(path.validatorId)!=identity.first || m.logicProfiles.at(path.logicProfileId)!=identity)invalid();}
    const DetStateLayout collect(p.collect),objective(p.objective),actions(p.actions);
    const auto& cw=cp.globals.at(c.collectStateSchema);const auto& ow=cp.globals.at(c.objectiveStateSchema);
    if(!collect.valid(cw) || !objective.valid(ow) || std::get<std::uint64_t>(collect.read(cw,1)) || std::get<std::uint64_t>(objective.read(ow,1)))invalid();
    for(const auto& [id,a]:cp.actors){(void)id;if(a.blocks.at(c.collectStateSchema)!=collect.defaults() || a.blocks.at(c.objectiveStateSchema)!=objective.defaults())invalid();}
    const auto& aw=cp.globals.at(c.progress.actions.stateSchema);
    const auto actionsCount=std::get<std::uint64_t>(actions.read(aw,DetActionsTotalCount)),actionsStamp=std::get<std::uint64_t>(actions.read(aw,DetActionsLastTick));
    std::uint64_t claims=0,attempts=0;
    for(std::size_t n=0;n<c.claims.size();++n){const auto r=readClaim(cp,p,n);valid(r,cp.nextTick,progress.sequence,actionsCount,actionsStamp);
        claims=add(claims,r.attemptCount);attempts=add(attempts,r.actionAttemptCount);
        if(r.attemptCount && (r.lastResult==DetProgressCode2D::Applied || r.lastResult==DetProgressCode2D::AlreadyClaimed)){
            const auto claim=detCollectibleState2D(cp,c.progress,c.claims[n].claim);if(!claim.claimed || (r.lastResult==DetProgressCode2D::Applied && claim.tick!=r.lastTick))invalid();}}
    for(std::size_t n=0;n<c.rules.size();++n){const auto r=readObjective(cp,p,n);valid(r,c.rules[n],cp.nextTick,actionsCount,actionsStamp);
        attempts=add(attempts,r.attemptCount);
        if(cp.nextTick && r.previousSatisfied!=detProgressObjective2D(cp,c.progress,c.rules[n].objective))invalid();}
    if(claims>progress.sequence || attempts>actionsCount)invalid();
}
bool run(DetTickContext& ctx,const DetProgressFlow2DConfig& c,const Plan& p){
    policiesMatch(ctx,p);const auto before=detProgressResult2D(ctx,c.progress);const auto actionsBefore=detActionResult2D(ctx,c.progress.actions);const auto maximum=stamp(ctx.tick());
    if(ctx.readGlobal<std::uint64_t>(c.collectStateSchema,1) || ctx.readGlobal<std::uint64_t>(c.objectiveStateSchema,1))invalid();
    for(std::size_t n=0;n<c.claims.size();++n)valid(readClaim(ctx,c,n),maximum,before.sequence,actionsBefore.sequence,actionsBefore.sequence?stamp(actionsBefore.tick):0);
    for(std::size_t n=0;n<c.rules.size();++n)valid(readObjective(ctx,c,n),c.rules[n],maximum,actionsBefore.sequence,actionsBefore.sequence?stamp(actionsBefore.tick):0);
    std::vector<const DetTickCommand*> events;for(const auto& event:ctx.events())if(event.source==c.progress.actions.collision.systemId && event.type==c.progress.actions.collision.eventType)events.push_back(&event);
    std::sort(events.begin(),events.end(),[](const auto* a,const auto* b){return std::pair{a->source,a->sequence}<std::pair{b->source,b->sequence};});
    std::vector<bool> attempted(c.claims.size(),false);
    for(const auto* event:events){DetTriggerEvent2D decoded;if(!decodeDetTriggerEvent2D(event->payload,decoded))invalid();if(decoded.phase!=DetTriggerPhase2D::Enter)continue;
        for(std::size_t n=0;n<c.claims.size();++n){if(attempted[n])continue;const auto& binding=c.claims[n];const auto* claim=find(c.progress.claims,binding.claim);
            if(decoded.pair!=DetCollisionPair2D{std::min(claim->trigger,claim->actor),std::max(claim->trigger,claim->actor)}
                || (binding.requiredObjective && !detProgressObjective2D(ctx,c.progress,binding.requiredObjective)))continue;
            auto r=readClaim(ctx,c,n);if(r.attemptCount==UINT64_MAX || r.actionAttemptCount==UINT64_MAX)invalid();
            const auto result=claimDetCollectible2D(ctx,c.progress,binding.claim);attempted[n]=true;++r.attemptCount;r.lastTick=ctx.tick();
            r.progressSequence=result.sequence;r.lastResult=result.code;r.lastActionCode=result.actionCode;r.lastActionSequence=result.actionSequence;
            if(result.actionSequence)++r.actionAttemptCount;write(ctx,c,n,r);}}
    for(std::size_t n=0;n<c.rules.size();++n){const auto& rule=c.rules[n];auto r=readObjective(ctx,c,n);const bool satisfied=detProgressObjective2D(ctx,c.progress,rule.objective);
        if(satisfied && !r.previousSatisfied && !r.onceConsumed){if(r.attemptCount==UINT64_MAX)invalid();
            const auto result=executeDetAction2D(ctx,c.progress.actions,rule.action);++r.attemptCount;r.lastTick=ctx.tick();r.lastResult=result.code;r.actionSequence=result.sequence;
            r.onceConsumed=rule.once;}r.previousSatisfied=satisfied;write(ctx,c,n,r);}
    return true;
}
} // namespace
bool installDetProgressFlow2D(DeterministicSession& session,DetProgressFlow2DConfig c){Plan p;
    try{p=plan(c);if(session.sealed() || session.faulted() || session.nextTick())return false;}catch(const std::invalid_argument&){return false;}
    if(!(session.registerTypedSchema(p.collect) && session.registerGlobalState(p.collect.id)
        && session.registerTypedSchema(p.objective) && session.registerGlobalState(p.objective.id)))return false;
    for(const auto& s:p.policies)if(!(session.registerTypedSchema(s) && session.registerGlobalState(s.id)))return false;
    return session.registerLogicProfile(c.logicProfileId,1,logicHash) && session.registerValidator(c.validatorId,1,[c,p](const auto& cp,std::string& error){
        try{validateCheckpoint(cp,c,p);return true;}catch(const std::exception& e){error=e.what();return false;}})
        && session.registerSystem(c.systemId,c.progress.actions.collision.priority,[c,p](DetTickContext& ctx){return run(ctx,c,p);});
}
DetProgressFlowClaimState2D detProgressFlowClaim2D(DetTickContext& ctx,const DetProgressFlow2DConfig& c,std::uint32_t id){const auto p=plan(c);policiesMatch(ctx,p);
    const auto progress=detProgressResult2D(ctx,c.progress);const auto actions=detActionResult2D(ctx,c.progress.actions);const auto r=readClaim(ctx,c,claimIndex(c,id));
    valid(r,stamp(ctx.tick()),progress.sequence,actions.sequence,actions.sequence?stamp(actions.tick):0);return r;}
DetProgressFlowClaimState2D detProgressFlowClaim2D(const DetSessionCheckpoint& cp,const DetProgressFlow2DConfig& c,std::uint32_t id){const auto p=plan(c);validateCheckpoint(cp,c,p);return readClaim(cp,p,claimIndex(c,id));}
DetProgressFlowObjectiveState2D detProgressFlowObjective2D(DetTickContext& ctx,const DetProgressFlow2DConfig& c,std::uint32_t id){const auto p=plan(c);policiesMatch(ctx,p);
    (void)detProgressResult2D(ctx,c.progress);const auto actions=detActionResult2D(ctx,c.progress.actions);const auto n=ruleIndex(c,id);const auto r=readObjective(ctx,c,n);
    valid(r,c.rules[n],stamp(ctx.tick()),actions.sequence,actions.sequence?stamp(actions.tick):0);return r;}
DetProgressFlowObjectiveState2D detProgressFlowObjective2D(const DetSessionCheckpoint& cp,const DetProgressFlow2DConfig& c,std::uint32_t id){const auto p=plan(c);validateCheckpoint(cp,c,p);return readObjective(cp,p,ruleIndex(c,id));}
DetTypedStateSchema detProgressFlowCollectSchema2D(const DetProgressFlow2DConfig& c){return plan(c).collect;}
DetTypedStateSchema detProgressFlowObjectiveSchema2D(const DetProgressFlow2DConfig& c){return plan(c).objective;}
std::uint32_t detProgressFlowClaimField2D(const DetProgressFlow2DConfig& c,std::uint32_t id,DetProgressFlowClaimField2D member){(void)plan(c);
    switch(member){case DetFlowClaimAttemptCount:case DetFlowClaimLastTick:case DetFlowClaimProgressSequence:case DetFlowClaimLastResult:
        case DetFlowClaimActionAttemptCount:case DetFlowClaimLastActionCode:case DetFlowClaimLastActionSequence:break;default:invalid();}return field(claimIndex(c,id),member);}
std::uint32_t detProgressFlowObjectiveField2D(const DetProgressFlow2DConfig& c,std::uint32_t id,DetProgressFlowObjectiveField2D member){(void)plan(c);
    switch(member){case DetFlowPreviousSatisfied:case DetFlowOnceConsumed:case DetFlowObjectiveAttemptCount:case DetFlowObjectiveLastTick:
        case DetFlowObjectiveLastResult:case DetFlowObjectiveActionSequence:break;default:invalid();}return field(ruleIndex(c,id),member);}
std::vector<std::uint8_t> encodeDetProgressFlowConfig2D(const DetProgressFlow2DConfig& c){return plan(c).bytes;}
} // namespace ayt::entity
