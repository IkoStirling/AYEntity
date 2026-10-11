// Stage47 dedicated HP authority, real Enter effects and pure restore closure.
#include <AYEntity/DeterministicVitality2D.h>
#include "detail/DetSessionWire.h"
#include <algorithm>
#include <set>
#include <stdexcept>

namespace ayt::entity {
namespace {
constexpr std::uint64_t logicHash=UINT64_C(0x4456493244503031);
constexpr std::size_t chunkBytes=1000;
[[noreturn]] void invalid(){throw std::invalid_argument("deterministic vitality 2D: invalid policy, event, authority or registered state");}
std::uint64_t add(std::uint64_t a,std::uint64_t b){if(a>UINT64_MAX-b)invalid();return a+b;}
std::uint64_t stamp(std::uint64_t t){return add(t,1);}
template<class T>const T* find(const std::vector<T>& v,std::uint32_t id){const auto it=std::lower_bound(v.begin(),v.end(),id,[](const auto& r,auto x){return r.id<x;});return it!=v.end() && it->id==id?&*it:nullptr;}
std::size_t actorIndex(const DetVitality2DConfig& c,SimEntityId id){const auto it=std::lower_bound(c.actors.begin(),c.actors.end(),id,[](const auto& r,auto x){return r.actor<x;});if(it==c.actors.end() || it->actor!=id)invalid();return static_cast<std::size_t>(it-c.actors.begin());}
std::uint32_t af(std::size_t n,std::uint32_t m){return static_cast<std::uint32_t>(1000+n*100)+m;}
bool kindValid(DetVitalityKind2D k){return k==DetVitalityKind2D::Hurt || k==DetVitalityKind2D::Heal;}
struct Writer {std::vector<std::uint8_t> bytes;void u64(std::uint64_t v,unsigned w=8){for(unsigned n=0;n<w;++n)bytes.push_back(static_cast<std::uint8_t>(v>>(8*n)));}void u32(std::uint32_t v){u64(v,4);}};
struct Plan {std::vector<std::uint8_t> bytes;DetTypedStateSchema state,body;std::vector<DetTypedStateSchema> policies,pathPolicies;};
// Path1 exposes its mutable descriptor but no immutable-policy factory. This
// read-only dependency descriptor matches its public fixed policy fields.
DetTypedStateSchema pathPolicy(const DetPathMotion2DConfig& c){DetTypedStateSchema s{c.policySchema,1,{{1,std::uint32_t{1}},
    {2,c.collision.bodySchema},{3,c.collision.historySchema},{4,c.collision.systemId},{5,c.preSystemId},{6,c.postSystemId},
    {7,c.validatorId},{8,c.logicProfileId},{9,c.tickPeriod},{10,static_cast<std::uint32_t>(c.playback)},{11,static_cast<std::uint32_t>(c.waypoints.size())}}};
    for(std::size_t n=0;n<c.waypoints.size();++n){const auto id=static_cast<std::uint32_t>(20+3*n);const auto& point=c.waypoints[n];
        s.fields.insert(s.fields.end(),{{id,point.offset},{id+1,point.travelTicks},{id+2,point.dwellTicks}});}return s;}
Plan plan(const DetVitality2DConfig& c){
    if(c.actors.empty() || c.actors.size()>kDetVitality2DMaxActors || c.permissions.size()>kDetVitality2DMaxPermissions
        || c.regions.size()>kDetVitality2DMaxRegions || (c.permissions.empty() && c.regions.empty())
        || !c.maxOperationsPerTick || c.maxOperationsPerTick>64 || c.stateSchema<2 || c.stateSchema==UINT32_MAX
        || c.policySchemaBase<2 || c.policySchemaBase==UINT32_MAX || !c.systemId || !c.validatorId || !c.logicProfileId
        || c.systemId>=c.progress.actions.collision.systemId || !c.progress.objectives.empty() || !c.progress.claims.empty()
        || c.progress.counters.size()!=c.actors.size() || c.validatorId==c.progress.validatorId || c.validatorId==c.progress.actions.validatorId
        || c.validatorId==c.progress.actions.collision.systemId || c.logicProfileId==c.progress.logicProfileId
        || c.logicProfileId==c.progress.actions.logicProfileId)invalid();
    const auto progressBytes=encodeDetProgressConfig2D(c.progress);const auto progressState=detProgressStateSchema2D(c.progress);
    const auto actionPolicies=detActionsPolicySchemas2D(c.progress.actions);const auto actionState=detActionsStateSchema2D(c.progress.actions);
    Plan p;p.body=detCollisionBodySchema2D(c.progress.actions.collision);
    p.state={c.stateSchema,1,{{DetVitalityTotalCount,std::uint64_t{}},{DetVitalityBudgetTick,std::uint64_t{}},
        {DetVitalityTickCount,std::uint64_t{}},{DetVitalityProcessedTick,std::uint64_t{}},{DetVitalityOwnedProgressCount,std::uint64_t{}},
        {DetVitalityWriteCounter,std::uint32_t{}},{DetVitalityWriteKind,std::uint32_t{}},{DetVitalityWriteAmount,std::uint64_t{}},{DetVitalityWriteTick,std::uint64_t{}},
        {DetVitalityLastCode,std::uint32_t{}},{DetVitalityLastKind,std::uint32_t{}},{DetVitalityLastActor,DetEntityRef{}},
        {DetVitalityLastPermission,std::uint32_t{}},{DetVitalityLastRegion,std::uint32_t{}},{DetVitalityLastRequested,std::uint64_t{}},
        {DetVitalityLastActual,std::uint64_t{}},{DetVitalityLastBefore,std::uint64_t{}},{DetVitalityLastAfter,std::uint64_t{}},{DetVitalityLastTick,std::uint64_t{}},{DetVitalityUnknownCount,std::uint64_t{}}}};
    Writer w;w.u32(1);w.u32(c.stateSchema);w.u32(c.policySchemaBase);w.u32(c.systemId);w.u32(c.validatorId);w.u32(c.logicProfileId);w.u32(c.maxOperationsPerTick);
    w.u32(static_cast<std::uint32_t>(progressBytes.size()));w.bytes.insert(w.bytes.end(),progressBytes.begin(),progressBytes.end());
    w.u32(static_cast<std::uint32_t>(c.actors.size()));SimEntityId previous=0;std::set<std::uint32_t> counters;
    for(std::size_t n=0;n<c.actors.size();++n){const auto& a=c.actors[n];const auto* counter=find(c.progress.counters,a.counter);
        if(!a.actor || a.actor<=previous || !counter || counter->minimum || !counter->maximum || a.invulnerabilityTicks>65535 || !counters.insert(a.counter).second)invalid();previous=a.actor;
        w.u64(a.actor);w.u32(a.counter);w.u32(a.invulnerabilityTicks);
        p.state.fields.insert(p.state.fields.end(),{{af(n,DetVitalityHP),counter->initial},{af(n,DetVitalityUntil),std::uint64_t{}},
            {af(n,DetVitalityAttempts),std::uint64_t{}},{af(n,DetVitalityHurts),std::uint64_t{}},{af(n,DetVitalityHeals),std::uint64_t{}},
            {af(n,DetVitalityDeaths),std::uint64_t{}},{af(n,DetVitalityActorLastTick),std::uint64_t{}},{af(n,DetVitalityLastHurtTick),std::uint64_t{}}});}
    w.u32(static_cast<std::uint32_t>(c.permissions.size()));std::uint32_t last=0;
    for(const auto& r:c.permissions){if(!r.id || r.id<=last || !kindValid(r.kind) || !r.maxAmount)invalid();last=r.id;(void)actorIndex(c,r.actor);
        w.u32(r.id);w.u64(r.actor);w.u32(static_cast<std::uint32_t>(r.kind));w.u64(r.maxAmount);}
    w.u32(static_cast<std::uint32_t>(c.regions.size()));last=0;
    for(const auto& r:c.regions){if(!r.id || r.id<=last || !r.trigger || r.trigger==r.actor || !kindValid(r.kind) || !r.amount)invalid();last=r.id;(void)actorIndex(c,r.actor);
        w.u32(r.id);w.u64(r.trigger);w.u64(r.actor);w.u32(static_cast<std::uint32_t>(r.kind));w.u64(r.amount);}
    p.bytes=std::move(w.bytes);if(p.bytes.size()>kDetVitality2DMaxPolicyBytes)invalid();const auto chunks=(p.bytes.size()+chunkBytes-1)/chunkBytes;
    if(!chunks || chunks>5 || c.policySchemaBase>UINT32_MAX-chunks)invalid();
    std::set<std::uint32_t> schemas;const auto reserve=[&](auto id){if(!schemas.insert(id).second)invalid();};
    reserve(p.body.id);reserve(c.progress.actions.collision.historySchema);reserve(actionState.id);for(const auto& s:actionPolicies)reserve(s.id);reserve(progressState.id);
    for(std::size_t n=0;n<(progressBytes.size()+chunkBytes-1)/chunkBytes;++n)reserve(c.progress.policySchemaBase+static_cast<std::uint32_t>(n));
    std::map<std::uint32_t,std::uint32_t> paths;
    for(const auto& target:c.progress.actions.targets)if(target.path){const auto& path=*target.path;
        if(c.systemId>=path.preSystemId || c.systemId==path.postSystemId || c.validatorId==path.validatorId || c.logicProfileId==path.logicProfileId)invalid();
        if(!paths.contains(path.policySchema)){reserve(path.stateSchema);reserve(path.policySchema);paths.emplace(path.policySchema,path.stateSchema);p.pathPolicies.push_back(pathPolicy(path));}
        else if(paths.at(path.policySchema)!=path.stateSchema)invalid();}
    reserve(c.stateSchema);
    for(std::size_t n=0;n<chunks;++n){const auto id=c.policySchemaBase+static_cast<std::uint32_t>(n);reserve(id);
        DetTypedStateSchema s{id,1,{{1,static_cast<std::uint32_t>(n)},{2,static_cast<std::uint32_t>(chunks)},{3,static_cast<std::uint32_t>(p.bytes.size())}}};
        const auto begin=n*chunkBytes,end=std::min(begin+chunkBytes,p.bytes.size());for(std::size_t off=begin;off<end;off+=8){std::uint64_t word=0;
            for(std::size_t b=0;b<8 && off+b<end;++b)word|=std::uint64_t{p.bytes[off+b]}<<(8*b);s.fields.push_back({static_cast<std::uint32_t>(4+(off-begin)/8),word});}p.policies.push_back(std::move(s));}
    (void)detStateDefaults(p.state);return p;
}
void policiesMatch(DetTickContext& ctx,const Plan& p){for(const auto& s:p.policies)for(const auto& f:s.fields)std::visit([&]<class T>(const T& expected){
    if(encodeDetStateValue(ctx.readGlobal<T>(s.id,f.id))!=encodeDetStateValue(expected))invalid();},f.initial);}
void descriptor(const detwire::ManifestLayout& m,const DetTypedStateSchema& s){if(m.schemaVersions.at(s.id)!=s.version || !m.globals.contains(s.id))invalid();
    const auto& actual=m.schemas.at(s.id);if(actual.size()!=s.fields.size())invalid();for(std::size_t n=0;n<actual.size();++n)if(actual[n].id!=s.fields[n].id
        || actual[n].type!=detStateType(s.fields[n].initial) || actual[n].width!=encodeDetStateValue(s.fields[n].initial).size())invalid();}
template<class Get>DetVitalityState2D readActor(Get&& get,std::size_t n){DetVitalityState2D r;
    r.hp=get.template operator()<std::uint64_t>(af(n,DetVitalityHP));r.invulnerableUntil=get.template operator()<std::uint64_t>(af(n,DetVitalityUntil));
    r.attemptCount=get.template operator()<std::uint64_t>(af(n,DetVitalityAttempts));r.hurtCount=get.template operator()<std::uint64_t>(af(n,DetVitalityHurts));
    r.healCount=get.template operator()<std::uint64_t>(af(n,DetVitalityHeals));r.deathCount=get.template operator()<std::uint64_t>(af(n,DetVitalityDeaths));
    const auto at=get.template operator()<std::uint64_t>(af(n,DetVitalityActorLastTick));if(r.attemptCount){if(!at)invalid();r.lastTick=at-1;}else if(at)invalid();
    const auto hurt=get.template operator()<std::uint64_t>(af(n,DetVitalityLastHurtTick));if(r.hurtCount){if(!hurt)invalid();r.lastHurtTick=hurt-1;}else if(hurt)invalid();r.dead=!r.hp;return r;}
template<class Get>DetVitalityResult2D readResult(Get&& get){DetVitalityResult2D r;
    r.sequence=get.template operator()<std::uint64_t>(DetVitalityTotalCount);r.code=static_cast<DetVitalityCode2D>(get.template operator()<std::uint32_t>(DetVitalityLastCode));
    r.kind=static_cast<DetVitalityKind2D>(get.template operator()<std::uint32_t>(DetVitalityLastKind));r.actor=get.template operator()<DetEntityRef>(DetVitalityLastActor).value;
    r.permission=get.template operator()<std::uint32_t>(DetVitalityLastPermission);r.region=get.template operator()<std::uint32_t>(DetVitalityLastRegion);
    r.requested=get.template operator()<std::uint64_t>(DetVitalityLastRequested);r.actual=get.template operator()<std::uint64_t>(DetVitalityLastActual);
    r.hpBefore=get.template operator()<std::uint64_t>(DetVitalityLastBefore);r.hpAfter=get.template operator()<std::uint64_t>(DetVitalityLastAfter);
    const auto at=get.template operator()<std::uint64_t>(DetVitalityLastTick);if(r.sequence){if(!at)invalid();r.tick=at-1;}else if(at)invalid();return r;}
template<class Get,class HP>void validState(Get&& get,HP&& hp,const DetVitality2DConfig& c,const DetProgressResult2D& progress,std::uint64_t maximumStamp,bool boundary){
    const auto total=get.template operator()<std::uint64_t>(DetVitalityTotalCount),budget=get.template operator()<std::uint64_t>(DetVitalityBudgetTick),
        count=get.template operator()<std::uint64_t>(DetVitalityTickCount),processed=get.template operator()<std::uint64_t>(DetVitalityProcessedTick),owned=get.template operator()<std::uint64_t>(DetVitalityOwnedProgressCount);
    if(owned!=progress.sequence || owned>total || processed>maximumStamp || (boundary && processed!=maximumStamp)
        || bool(total)!=bool(budget) || bool(total)!=bool(count) || budget>maximumStamp || count>total)invalid();
    const auto wc=get.template operator()<std::uint32_t>(DetVitalityWriteCounter),wk=get.template operator()<std::uint32_t>(DetVitalityWriteKind);
    const auto wa=get.template operator()<std::uint64_t>(DetVitalityWriteAmount),wt=get.template operator()<std::uint64_t>(DetVitalityWriteTick);
    if(!owned){if(wc || wk || wa || wt)invalid();}
    else if(!find(c.progress.counters,wc) || (wk!=1 && wk!=2) || !wa || !wt || wt>maximumStamp
        || progress.code!=DetProgressCode2D::Applied || progress.id!=wc || progress.counter!=wc || progress.amount!=wa || stamp(progress.tick)!=wt
        || progress.operation!=(wk==1?DetProgressOperation2D::Subtract:DetProgressOperation2D::Add))invalid();
    std::uint64_t attempts=0,writes=0;
    for(std::size_t n=0;n<c.actors.size();++n){const auto r=readActor(get,n);const auto* counter=find(c.progress.counters,c.actors[n].counter);
        if(r.hp!=hp(c.actors[n].counter) || r.hp>counter->maximum || (!counter->initial && r.hp) || add(r.hurtCount,r.healCount)>r.attemptCount
            || r.deathCount!=(counter->initial && !r.hp?1u:0u) || r.deathCount>r.hurtCount
            || (r.attemptCount && stamp(r.lastTick)>maximumStamp) || (r.dead && r.invulnerableUntil)
            || (r.hurtCount && (r.lastHurtTick>r.lastTick || stamp(r.lastHurtTick)>maximumStamp))
            || r.invulnerableUntil!=(r.hurtCount && r.hp && c.actors[n].invulnerabilityTicks?add(r.lastHurtTick,c.actors[n].invulnerabilityTicks):0)
            || (!r.attemptCount && (r.hp!=counter->initial || r.invulnerableUntil || r.hurtCount || r.healCount || r.deathCount)))invalid();
        attempts=add(attempts,r.attemptCount);writes=add(writes,add(r.hurtCount,r.healCount));}
    const auto unknown=get.template operator()<std::uint64_t>(DetVitalityUnknownCount);
    if(add(attempts,unknown)!=total || writes!=owned)invalid();const auto r=readResult(get);const auto code=static_cast<std::uint32_t>(r.code);
    if(!total){if(code || r.kind!=DetVitalityKind2D::None || r.actor || r.permission || r.region || r.requested || r.actual || r.hpBefore || r.hpAfter || r.tick)invalid();return;}
    if(code<1 || code>9 || stamp(r.tick)>maximumStamp || stamp(r.tick)!=budget || (r.permission && r.region))invalid();
    if(!r.region){const auto* permission=find(c.permissions,r.permission);if(!permission){if(!unknown || r.actor || r.kind!=DetVitalityKind2D::None || r.actual || r.hpBefore || r.hpAfter || r.code!=DetVitalityCode2D::UnknownPermission)invalid();return;}
        if(r.actor!=permission->actor || r.kind!=permission->kind || (r.code==DetVitalityCode2D::AmountExceeded)!=(r.requested>permission->maxAmount))invalid();}
    else {const auto* region=find(c.regions,r.region);if(!region || r.actor!=region->actor || r.kind!=region->kind || r.requested!=region->amount
            || r.code==DetVitalityCode2D::UnknownPermission || r.code==DetVitalityCode2D::AmountExceeded)invalid();}
    const auto n=actorIndex(c,r.actor);const auto actor=readActor(get,n);const auto maximum=find(c.progress.counters,c.actors[n].counter)->maximum;
    if(actor.hp!=r.hpAfter || !actor.attemptCount || actor.lastTick!=r.tick || r.hpBefore>maximum || r.hpAfter>maximum || r.actual>r.requested)invalid();
    if(r.code==DetVitalityCode2D::Applied){if(!r.actual || !r.hpBefore || r.actual!=(r.kind==DetVitalityKind2D::Hurt?std::min(r.requested,r.hpBefore):std::min(r.requested,maximum-r.hpBefore))
            || r.hpAfter!=(r.kind==DetVitalityKind2D::Hurt?r.hpBefore-r.actual:r.hpBefore+r.actual) || wt!=stamp(r.tick) || wc!=c.actors[n].counter || wa!=r.actual || wk!=static_cast<std::uint32_t>(r.kind))invalid();}
    else if(r.actual || r.hpAfter!=r.hpBefore || (r.code==DetVitalityCode2D::Unchanged && (!r.hpBefore || (r.requested && !(r.kind==DetVitalityKind2D::Heal && r.hpBefore==maximum))))
        || (r.code==DetVitalityCode2D::Dead && r.hpBefore) || (r.code==DetVitalityCode2D::Invulnerable && (r.kind!=DetVitalityKind2D::Hurt || !r.hpBefore || actor.invulnerableUntil<=r.tick))
        || (r.code==DetVitalityCode2D::BudgetExceeded && count<=c.maxOperationsPerTick) || (r.code==DetVitalityCode2D::InactiveRegion && !r.region))invalid();
}
void roles(const DetSessionCheckpoint& cp,const DetVitality2DConfig& c,const Plan& p){
    const auto check=[&](SimEntityId id,bool trigger){const auto it=cp.actors.find(id);if(it==cp.actors.end()){if(!cp.nextTick || !std::binary_search(cp.retiredIds.begin(),cp.retiredIds.end(),id))invalid();return;}
        const auto& w=it->second.blocks.at(p.body.id);const auto mode=readDetState<std::uint32_t>(p.body,w,DetBodyMode);if(readDetState<bool>(p.body,w,DetBodyTrigger)!=trigger || (trigger?!mode:mode!=2))invalid();
        if(trigger && ((c.progress.actions.collision.oneWayPlatforms && readDetState<bool>(p.body,w,DetBodyOneWay))
            || (c.progress.actions.collision.staticSlopes && readDetState<std::uint32_t>(p.body,w,DetBodyShape))))invalid();};
    for(const auto& a:c.actors)check(a.actor,false);for(const auto& r:c.regions){check(r.trigger,true);if(!cp.nextTick){const auto& t=cp.actors.at(r.trigger).blocks.at(p.body.id);const auto& a=cp.actors.at(r.actor).blocks.at(p.body.id);
            if(!(readDetState<std::uint32_t>(p.body,t,DetBodyMask)&readDetState<std::uint32_t>(p.body,a,DetBodyLayer))
                || !(readDetState<std::uint32_t>(p.body,a,DetBodyMask)&readDetState<std::uint32_t>(p.body,t,DetBodyLayer)))invalid();}}
}
void validateCheckpoint(const DetSessionCheckpoint& cp,const DetVitality2DConfig& c,const Plan& p){const auto progress=detProgressResult2D(cp,c.progress);const auto m=detwire::manifestLayout(cp.manifest);
    if(m.systems.at(c.systemId)!=c.progress.actions.collision.priority || m.validatorVersions.at(c.validatorId)!=1
        || m.logicProfiles.at(c.logicProfileId)!=std::pair<std::uint32_t,std::uint64_t>{1,logicHash})invalid();descriptor(m,p.state);
    const auto matchPolicies=[&](const std::vector<DetTypedStateSchema>& schemas){for(const auto& s:schemas){descriptor(m,s);const auto defaults=detStateDefaults(s);
        if(cp.globals.at(s.id)!=defaults)invalid();for(const auto& [id,a]:cp.actors){(void)id;if(a.blocks.at(s.id)!=defaults)invalid();}}};
    matchPolicies(p.policies);matchPolicies(p.pathPolicies);
    const DetStateLayout l(p.state);const auto& words=cp.globals.at(c.stateSchema);if(!l.valid(words))invalid();for(const auto& [id,a]:cp.actors){(void)id;if(a.blocks.at(c.stateSchema)!=l.defaults())invalid();}
    for(const auto& t:c.progress.actions.targets)if(t.path){const auto& path=*t.path;if(m.systems.at(path.preSystemId)!=path.collision.priority
        || m.systems.at(path.postSystemId)!=path.collision.priority || m.validatorVersions.at(path.validatorId)!=1
        || m.logicProfiles.at(path.logicProfileId)!=std::pair<std::uint32_t,std::uint64_t>{1,UINT64_C(0x44504d3244503031)})invalid();}
    validState([&]<class T>(auto f){return std::get<T>(l.read(words,f));},[&](auto id){return detProgressCounter2D(cp,c.progress,id);},c,progress,cp.nextTick,true);roles(cp,c,p);
    for(const auto& e:cp.pendingEvents)if(e.source==c.progress.actions.collision.systemId && e.type==c.progress.actions.collision.eventType){DetTriggerEvent2D d;if(!decodeDetTriggerEvent2D(e.payload,d))invalid();}
}
Plan access(DetTickContext& ctx,const DetVitality2DConfig& c,bool writing){auto p=plan(c);policiesMatch(ctx,p);const auto progress=detProgressResult2D(ctx,c.progress);
    validState([&]<class T>(auto f){return ctx.readGlobal<T>(c.stateSchema,f);},[&](auto id){return detProgressCounter2D(ctx,c.progress,id);},c,progress,stamp(ctx.tick()),false);
    if(writing && ctx.readGlobal<std::uint64_t>(c.stateSchema,DetVitalityProcessedTick)>=stamp(ctx.tick()))invalid();return p;}
void writeActor(DetTickContext& ctx,const DetVitality2DConfig& c,std::size_t n,const DetVitalityState2D& r){const auto s=c.stateSchema;
    ctx.writeGlobal(s,af(n,DetVitalityHP),r.hp);ctx.writeGlobal(s,af(n,DetVitalityUntil),r.invulnerableUntil);ctx.writeGlobal(s,af(n,DetVitalityAttempts),r.attemptCount);
    ctx.writeGlobal(s,af(n,DetVitalityHurts),r.hurtCount);ctx.writeGlobal(s,af(n,DetVitalityHeals),r.healCount);ctx.writeGlobal(s,af(n,DetVitalityDeaths),r.deathCount);
    ctx.writeGlobal(s,af(n,DetVitalityActorLastTick),stamp(r.lastTick));ctx.writeGlobal(s,af(n,DetVitalityLastHurtTick),r.hurtCount?stamp(r.lastHurtTick):0);}
DetVitalityResult2D attempt(DetTickContext& ctx,const DetVitality2DConfig& c,DetVitalityResult2D r,bool amountExceeded=false){
    const auto s=c.stateSchema;const auto tick=ctx.tick(),at=stamp(tick);const auto total=add(ctx.readGlobal<std::uint64_t>(s,DetVitalityTotalCount),1);
    const auto count=add(ctx.readGlobal<std::uint64_t>(s,DetVitalityBudgetTick)==at?ctx.readGlobal<std::uint64_t>(s,DetVitalityTickCount):0,1);
    r.tick=tick;r.sequence=total;std::optional<std::size_t> n;DetVitalityState2D actor;
    if(r.actor){n=actorIndex(c,r.actor);actor=readActor([&]<class T>(auto f){return ctx.readGlobal<T>(s,f);},*n);r.hpBefore=r.hpAfter=actor.hp;actor.attemptCount=add(actor.attemptCount,1);actor.lastTick=tick;
        if(amountExceeded)r.code=DetVitalityCode2D::AmountExceeded;
        else if(count>c.maxOperationsPerTick)r.code=DetVitalityCode2D::BudgetExceeded;
        else {const auto ids=ctx.entities();const auto bs=c.progress.actions.collision.bodySchema;if(!std::binary_search(ids.begin(),ids.end(),r.actor))r.code=DetVitalityCode2D::MissingActor;
            else if(ctx.read<bool>(r.actor,bs,DetBodyTrigger) || ctx.read<std::uint32_t>(r.actor,bs,DetBodyMode)!=2)invalid();
            else if(r.region){const auto* binding=find(c.regions,r.region);
                if(!std::binary_search(ids.begin(),ids.end(),binding->trigger))r.code=DetVitalityCode2D::MissingActor;
                else {if(!ctx.read<bool>(binding->trigger,bs,DetBodyTrigger) || !ctx.read<std::uint32_t>(binding->trigger,bs,DetBodyMode)
                        || ctx.read<bool>(r.actor,bs,DetBodyTrigger) || ctx.read<std::uint32_t>(r.actor,bs,DetBodyMode)!=2)invalid();
                    if((c.progress.actions.collision.oneWayPlatforms && ctx.read<bool>(binding->trigger,bs,DetBodyOneWay))
                        || (c.progress.actions.collision.staticSlopes && ctx.read<std::uint32_t>(binding->trigger,bs,DetBodyShape)))invalid();
                    if(!(ctx.read<std::uint32_t>(binding->trigger,bs,DetBodyMask)&ctx.read<std::uint32_t>(r.actor,bs,DetBodyLayer))
                        || !(ctx.read<std::uint32_t>(r.actor,bs,DetBodyMask)&ctx.read<std::uint32_t>(binding->trigger,bs,DetBodyLayer)))r.code=DetVitalityCode2D::InactiveRegion;}}
            if(r.code==DetVitalityCode2D::None){if(!actor.hp)r.code=DetVitalityCode2D::Dead;
                else if(!r.requested)r.code=DetVitalityCode2D::Unchanged;
                else if(r.kind==DetVitalityKind2D::Hurt && tick<actor.invulnerableUntil)r.code=DetVitalityCode2D::Invulnerable;
                else {const auto maximum=find(c.progress.counters,c.actors[*n].counter)->maximum;
                    r.actual=std::min(r.requested,r.kind==DetVitalityKind2D::Hurt?actor.hp:maximum-actor.hp);r.code=r.actual?DetVitalityCode2D::Applied:DetVitalityCode2D::Unchanged;}}}
        if(r.code==DetVitalityCode2D::Applied){const auto owned=add(ctx.readGlobal<std::uint64_t>(s,DetVitalityOwnedProgressCount),1);
            if(r.kind==DetVitalityKind2D::Hurt){actor.hurtCount=add(actor.hurtCount,1);actor.lastHurtTick=tick;actor.hp-=r.actual;
                if(!actor.hp){actor.deathCount=add(actor.deathCount,1);actor.invulnerableUntil=0;}
                else actor.invulnerableUntil=c.actors[*n].invulnerabilityTicks?add(tick,c.actors[*n].invulnerabilityTicks):0;
            }else {actor.healCount=add(actor.healCount,1);actor.hp+=r.actual;}
            // All overflow and result/shape planning precedes the only numerical write.
            if(detProgressResult2D(ctx,c.progress).sequence==UINT64_MAX)invalid();
            const auto written=executeDetProgress2D(ctx,c.progress,{r.kind==DetVitalityKind2D::Hurt?DetProgressOperation2D::Subtract:DetProgressOperation2D::Add,c.actors[*n].counter,r.actual});
            if(written.code!=DetProgressCode2D::Applied || written.sequence!=owned)invalid();r.hpAfter=actor.hp;
            ctx.writeGlobal(s,DetVitalityOwnedProgressCount,owned);ctx.writeGlobal(s,DetVitalityWriteCounter,c.actors[*n].counter);
            ctx.writeGlobal(s,DetVitalityWriteKind,static_cast<std::uint32_t>(r.kind));ctx.writeGlobal(s,DetVitalityWriteAmount,r.actual);ctx.writeGlobal(s,DetVitalityWriteTick,at);}
        writeActor(ctx,c,*n,actor);
    }else {const auto unknown=add(ctx.readGlobal<std::uint64_t>(s,DetVitalityUnknownCount),1);r.code=DetVitalityCode2D::UnknownPermission;
        ctx.writeGlobal(s,DetVitalityUnknownCount,unknown);}
    ctx.writeGlobal(s,DetVitalityTotalCount,total);ctx.writeGlobal(s,DetVitalityBudgetTick,at);ctx.writeGlobal(s,DetVitalityTickCount,count);
    ctx.writeGlobal(s,DetVitalityLastCode,static_cast<std::uint32_t>(r.code));ctx.writeGlobal(s,DetVitalityLastKind,static_cast<std::uint32_t>(r.kind));
    ctx.writeGlobal(s,DetVitalityLastActor,DetEntityRef{r.actor});ctx.writeGlobal(s,DetVitalityLastPermission,r.permission);ctx.writeGlobal(s,DetVitalityLastRegion,r.region);
    ctx.writeGlobal(s,DetVitalityLastRequested,r.requested);ctx.writeGlobal(s,DetVitalityLastActual,r.actual);ctx.writeGlobal(s,DetVitalityLastBefore,r.hpBefore);
    ctx.writeGlobal(s,DetVitalityLastAfter,r.hpAfter);ctx.writeGlobal(s,DetVitalityLastTick,at);return r;
}
bool run(DetTickContext& ctx,const DetVitality2DConfig& c){(void)access(ctx,c,true);std::vector<const DetTickCommand*> events;
    for(const auto& e:ctx.events())if(e.source==c.progress.actions.collision.systemId && e.type==c.progress.actions.collision.eventType)events.push_back(&e);
    std::sort(events.begin(),events.end(),[](const auto* a,const auto* b){return std::pair{a->source,a->sequence}<std::pair{b->source,b->sequence};});std::vector<bool> used(c.regions.size(),false);
    for(const auto* e:events){DetTriggerEvent2D d;if(!decodeDetTriggerEvent2D(e->payload,d))invalid();if(d.phase!=DetTriggerPhase2D::Enter)continue;
        for(std::size_t n=0;n<c.regions.size();++n){const auto& b=c.regions[n];if(used[n] || d.pair!=DetCollisionPair2D{std::min(b.actor,b.trigger),std::max(b.actor,b.trigger)})continue;
            used[n]=true;DetVitalityResult2D r;r.actor=b.actor;r.region=b.id;r.kind=b.kind;r.requested=b.amount;(void)attempt(ctx,c,r);}}
    ctx.writeGlobal(c.stateSchema,DetVitalityProcessedTick,stamp(ctx.tick()));return true;}
} // namespace
std::vector<std::uint8_t> encodeDetVitalityConfig2D(const DetVitality2DConfig& c){return plan(c).bytes;}
std::vector<DetTypedStateSchema> detVitalityPolicySchemas2D(const DetVitality2DConfig& c){return plan(c).policies;}
DetTypedStateSchema detVitalityStateSchema2D(const DetVitality2DConfig& c){return plan(c).state;}
std::uint32_t detVitalityActorField2D(const DetVitality2DConfig& c,SimEntityId id,DetVitalityActorField2D member){(void)plan(c);if(member<1 || member>8)invalid();return af(actorIndex(c,id),member);}
bool installDetVitality2D(DeterministicSession& session,DetVitality2DConfig c){Plan p;try{p=plan(c);if(session.sealed() || session.faulted() || session.nextTick())return false;}catch(const std::invalid_argument&){return false;}
    if(!(session.registerTypedSchema(p.state) && session.registerGlobalState(p.state.id)))return false;
    for(const auto& s:p.policies)if(!(session.registerTypedSchema(s) && session.registerGlobalState(s.id)))return false;
    return session.registerLogicProfile(c.logicProfileId,1,logicHash)
        && session.registerSystem(c.systemId,c.progress.actions.collision.priority,[c](DetTickContext& ctx){return run(ctx,c);})
        && session.registerValidator(c.validatorId,1,[c,p](const DetSessionCheckpoint& cp,std::string& error){try{validateCheckpoint(cp,c,p);return true;}catch(const std::exception& e){error=e.what();return false;}});}
DetVitalityResult2D executeDetVitality2D(DetTickContext& ctx,const DetVitality2DConfig& c,std::uint32_t id,std::uint64_t amount){(void)access(ctx,c,true);DetVitalityResult2D r;r.permission=id;r.requested=amount;
    // A zero/unknown permission is an ordinary recorded denial; regions are nonzero.
    const auto* p=find(c.permissions,id);if(p){r.actor=p->actor;r.kind=p->kind;}return attempt(ctx,c,r,p && amount>p->maxAmount);}
DetVitalityState2D detVitalityState2D(DetTickContext& ctx,const DetVitality2DConfig& c,SimEntityId id){(void)access(ctx,c,false);return readActor([&]<class T>(auto f){return ctx.readGlobal<T>(c.stateSchema,f);},actorIndex(c,id));}
DetVitalityState2D detVitalityState2D(const DetSessionCheckpoint& cp,const DetVitality2DConfig& c,SimEntityId id){const auto p=plan(c);validateCheckpoint(cp,c,p);const DetStateLayout l(p.state);
    return readActor([&]<class T>(auto f){return std::get<T>(l.read(cp.globals.at(c.stateSchema),f));},actorIndex(c,id));}
DetVitalityResult2D detVitalityResult2D(DetTickContext& ctx,const DetVitality2DConfig& c){(void)access(ctx,c,false);return readResult([&]<class T>(auto f){return ctx.readGlobal<T>(c.stateSchema,f);});}
DetVitalityResult2D detVitalityResult2D(const DetSessionCheckpoint& cp,const DetVitality2DConfig& c){const auto p=plan(c);validateCheckpoint(cp,c,p);const DetStateLayout l(p.state);
    return readResult([&]<class T>(auto f){return std::get<T>(l.read(cp.globals.at(c.stateSchema),f));});}
} // namespace ayt::entity
