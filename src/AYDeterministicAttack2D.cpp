// design reference: AYEntity/design.md Stage51 finite registered attack windows.
#include <AYEntity/DeterministicAttack2D.h>
#include "detail/DetSessionWire.h"
#include <algorithm>
#include <bit>
#include <map>
#include <set>
#include <stdexcept>

namespace ayt::entity {
namespace {
constexpr std::uint64_t logicHash=UINT64_C(0x4441543244503031);
constexpr std::size_t chunkBytes=1000;
[[noreturn]] void invalid(){throw std::invalid_argument("deterministic attack 2D: invalid policy, authority, geometry or registered state");}
std::uint64_t add(std::uint64_t a,std::uint64_t b){if(a>UINT64_MAX-b)invalid();return a+b;}
std::uint64_t stamp(std::uint64_t t){return add(t,1);}
std::uint32_t field(std::size_t n,std::uint32_t member){return static_cast<std::uint32_t>(1000+200*n)+member;}
std::size_t ownerIndex(const DetAttack2DConfig& c,SimEntityId id){const auto it=std::lower_bound(c.owners.begin(),c.owners.end(),id,
    [](const auto& row,auto key){return row.actor<key;});if(it==c.owners.end() || it->actor!=id)invalid();return static_cast<std::size_t>(it-c.owners.begin());}
template<class T>const T* actorRow(const std::vector<T>& rows,SimEntityId id){const auto it=std::lower_bound(rows.begin(),rows.end(),id,
    [](const auto& row,auto key){return row.actor<key;});return it!=rows.end() && it->actor==id?&*it:nullptr;}
const DetVitalityPermission2D* permission(const DetVitality2DConfig& c,std::uint32_t id){const auto it=std::lower_bound(c.permissions.begin(),c.permissions.end(),id,
    [](const auto& row,auto key){return row.id<key;});return it!=c.permissions.end() && it->id==id?&*it:nullptr;}
std::uint64_t maximumHP(const DetVitality2DConfig& c,SimEntityId id){const auto* actor=actorRow(c.actors,id);if(!actor)invalid();
    const auto it=std::find_if(c.progress.counters.begin(),c.progress.counters.end(),[&](const auto& row){return row.id==actor->counter;});
    if(it==c.progress.counters.end())invalid();return it->maximum;}
math::DetAabb2 attackBox(const DetAttackOwner2D& owner,math::DetVec2 pose,bool facing){auto offset=owner.offset;if(!facing)offset.x=-offset.x;
    const auto result=math::DetAabb2::fromCenterHalf(pose+offset,owner.halfExtent);if(!result.hasArea())invalid();return result;}
struct Writer {std::vector<std::uint8_t> bytes;void u64(std::uint64_t value,unsigned size=8){for(unsigned n=0;n<size;++n)bytes.push_back(static_cast<std::uint8_t>(value>>(8*n)));}
    void u32(std::uint32_t value){u64(value,4);}void vec(math::DetVec2 value){u32(value.x.bits());u32(value.y.bits());}};
struct Plan {DetTypedStateSchema state,ledger,body;std::vector<DetTypedStateSchema> policies,vitalityStates,vitalityPolicies;
    std::vector<std::uint8_t> bytes;};
Plan plan(const DetAttack2DConfig& c){
    if(c.owners.empty() || c.owners.size()>kDetAttack2DMaxOwners || c.stateSchema<2 || c.ledgerSchema<2 || c.policySchemaBase<2
        || c.stateSchema==UINT32_MAX || c.ledgerSchema==UINT32_MAX || c.policySchemaBase==UINT32_MAX
        || !c.systemId || !c.validatorId || !c.logicProfileId || c.systemId>=c.vitality.systemId
        || c.validatorId==c.vitality.validatorId || c.logicProfileId==c.vitality.logicProfileId)invalid();
    const auto vitalityBytes=encodeDetVitalityConfig2D(c.vitality);Plan p;
    p.vitalityStates=detVitalityStateSchemas2D(c.vitality);p.vitalityPolicies=detVitalityPolicySchemas2D(c.vitality);
    p.body=detCollisionBodySchema2D(c.vitality.progress.actions.collision);
    p.state={c.stateSchema,1,{{DetAttackProcessedTick,std::uint64_t{}}}};
    p.ledger={c.ledgerSchema,1,{{DetAttackTotalAttempts,std::uint64_t{}}}};
    Writer w;w.u32(1);w.u32(c.stateSchema);w.u32(c.ledgerSchema);w.u32(c.policySchemaBase);w.u32(c.systemId);
    w.u32(c.validatorId);w.u32(c.logicProfileId);w.u32(c.cancelOnRecovery?1u:0u);w.u32(static_cast<std::uint32_t>(vitalityBytes.size()));
    w.bytes.insert(w.bytes.end(),vitalityBytes.begin(),vitalityBytes.end());w.u32(static_cast<std::uint32_t>(c.owners.size()));
    SimEntityId prior=0;
    for(std::size_t n=0;n<c.owners.size();++n){const auto& row=c.owners[n];
        if(!row.actor || row.actor<=prior || !actorRow(c.vitality.actors,row.actor) || row.targets.empty() || row.targets.size()>kDetAttack2DMaxTargets
            || row.warmupTicks>65535 || !row.activeTicks || row.activeTicks>65535 || row.recoveryTicks>65535
            || !row.halfExtent.isFinite() || !row.offset.isFinite() || row.halfExtent.x<=math::DetFloat32{} || row.halfExtent.y<=math::DetFloat32{})invalid();
        prior=row.actor;(void)attackBox(row,{},true);(void)attackBox(row,{},false);
        w.u64(row.actor);w.vec(row.halfExtent);w.vec(row.offset);w.u32(row.warmupTicks);w.u32(row.activeTicks);w.u32(row.recoveryTicks);
        w.u32(static_cast<std::uint32_t>(row.targets.size()));SimEntityId previousTarget=0;
        for(const auto& target:row.targets){const auto* allowed=permission(c.vitality,target.permission);
            if(!target.actor || target.actor<=previousTarget || target.actor==row.actor || !allowed || allowed->actor!=target.actor
                || allowed->kind!=DetVitalityKind2D::Hurt || !target.amount || target.amount>allowed->maxAmount)invalid();
            previousTarget=target.actor;w.u64(target.actor);w.u32(target.permission);w.u64(target.amount);}
        for(std::uint32_t member=10;member<=150;member+=10){const auto f=field(n,member);
            if(member==10 || member==20 || member==30 || member==50)p.state.fields.push_back({f,member==20 || member==50});
            else if(member==40 || member==110)p.state.fields.push_back({f,std::uint32_t{}});else p.state.fields.push_back({f,std::uint64_t{}});}
        for(std::uint32_t member=10;member<=120;member+=10){const auto f=field(n,member);
            if(member==40)p.ledger.fields.push_back({f,DetEntityRef{}});
            else if(member==50 || member==60 || member==120)p.ledger.fields.push_back({f,std::uint32_t{}});
            else p.ledger.fields.push_back({f,std::uint64_t{}});}
    }
    p.bytes=std::move(w.bytes);if(p.bytes.size()>kDetAttack2DMaxPolicyBytes)invalid();
    const auto chunks=(p.bytes.size()+chunkBytes-1)/chunkBytes;if(!chunks || chunks>17 || c.policySchemaBase>UINT32_MAX-chunks)invalid();
    // Joint dependency budget and namespaces: nested factories own their closure;
    // deduplication here permits an authority intentionally shared by Flow/Progress.
    std::set<std::uint32_t> schemaIds,validators,logicIds,systems;
    const auto schemas=[&](const auto& rows){for(const auto& s:rows)schemaIds.insert(s.id);};
    schemas(p.vitalityStates);schemas(p.vitalityPolicies);schemaIds.insert(p.body.id);schemaIds.insert(c.vitality.progress.actions.collision.historySchema);
    schemaIds.insert(detProgressStateSchema2D(c.vitality.progress).id);
    const auto progressBytes=encodeDetProgressConfig2D(c.vitality.progress);
    for(std::size_t n=0;n<(progressBytes.size()+chunkBytes-1)/chunkBytes;++n)schemaIds.insert(c.vitality.progress.policySchemaBase+static_cast<std::uint32_t>(n));
    schemaIds.insert(detActionsStateSchema2D(c.vitality.progress.actions).id);schemas(detActionsPolicySchemas2D(c.vitality.progress.actions));
    validators.insert(c.vitality.validatorId);logicIds.insert(c.vitality.logicProfileId);systems.insert(c.vitality.systemId);
    validators.insert(c.vitality.progress.validatorId);logicIds.insert(c.vitality.progress.logicProfileId);
    validators.insert(c.vitality.progress.actions.validatorId);logicIds.insert(c.vitality.progress.actions.logicProfileId);
    validators.insert(c.vitality.progress.actions.collision.systemId);systems.insert(c.vitality.progress.actions.collision.systemId);
    for(const auto& target:c.vitality.progress.actions.targets)if(target.path){const auto& path=*target.path;
        schemaIds.insert(path.stateSchema);schemaIds.insert(path.policySchema);validators.insert(path.validatorId);logicIds.insert(path.logicProfileId);
        systems.insert(path.preSystemId);systems.insert(path.postSystemId);if(c.systemId>=path.preSystemId)invalid();}
    if(c.vitality.respawnRecovery){const auto& flow=*c.vitality.respawnFlow;const auto& bottom=flow.respawn;const auto& character=bottom.character;
        schemas(detCharacterRespawnFlowStateSchemas2D(flow));schemas(detCharacterRespawnFlowPolicySchemas2D(flow));
        schemaIds.insert(bottom.stateSchema);schemaIds.insert(bottom.policySchema);schemaIds.insert(character.stateSchema);schemaIds.insert(character.policySchema);
        validators.insert(flow.validatorId);validators.insert(bottom.validatorId);validators.insert(character.validatorId);
        logicIds.insert(flow.logicProfileId);logicIds.insert(bottom.logicProfileId);logicIds.insert(character.logicProfileId);
        systems.insert(c.vitality.postSystemId);systems.insert(flow.systemId);systems.insert(character.preSystemId);systems.insert(character.postSystemId);
        if(c.systemId>=flow.systemId || c.systemId>=character.preSystemId)invalid();}
    if(validators.contains(c.validatorId) || logicIds.contains(c.logicProfileId) || systems.contains(c.systemId))invalid();
    if(!schemaIds.insert(c.stateSchema).second || !schemaIds.insert(c.ledgerSchema).second)invalid();
    for(std::size_t n=0;n<chunks;++n){const auto id=c.policySchemaBase+static_cast<std::uint32_t>(n);if(!schemaIds.insert(id).second)invalid();
        DetTypedStateSchema s{id,1,{{1,static_cast<std::uint32_t>(n)},{2,static_cast<std::uint32_t>(chunks)},{3,static_cast<std::uint32_t>(p.bytes.size())}}};
        const auto begin=n*chunkBytes,end=std::min(begin+chunkBytes,p.bytes.size());for(std::size_t offset=begin;offset<end;offset+=8){std::uint64_t word=0;
            for(std::size_t byte=0;byte<8 && offset+byte<end;++byte)word|=std::uint64_t{p.bytes[offset+byte]}<<(8*byte);
            s.fields.push_back({static_cast<std::uint32_t>(4+(offset-begin)/8),word});}p.policies.push_back(std::move(s));}
    if(schemaIds.size()>detwire::maxSchemas)invalid();(void)detStateDefaults(p.state);(void)detStateDefaults(p.ledger);return p;
}
void descriptor(const detwire::ManifestLayout& m,const DetTypedStateSchema& schema){
    if(m.schemaVersions.at(schema.id)!=schema.version || !m.globals.contains(schema.id))invalid();const auto& actual=m.schemas.at(schema.id);
    if(actual.size()!=schema.fields.size())invalid();for(std::size_t n=0;n<actual.size();++n){const auto& f=schema.fields[n];
        if(actual[n].id!=f.id || actual[n].type!=detStateType(f.initial) || actual[n].width!=encodeDetStateValue(f.initial).size())invalid();}}
void policiesMatch(DetTickContext& ctx,const Plan& p){for(const auto& s:p.policies)for(const auto& f:s.fields)
    std::visit([&]<class T>(const T& expected){if(encodeDetStateValue(ctx.readGlobal<T>(s.id,f.id))!=encodeDetStateValue(expected))invalid();},f.initial);}
struct OwnerState {bool held=false,facing=true,latched=false,locked=true;std::uint32_t phase=0,mask=0;
    std::uint64_t count=0,start=0,begin=0,end=0,finish=0,cancels=0,cancelAt=0,generation=0,base=0;};
template<class Get>OwnerState readState(Get&& get,std::size_t n){OwnerState r;
    r.held=get.template operator()<bool>(field(n,10));r.facing=get.template operator()<bool>(field(n,20));r.latched=get.template operator()<bool>(field(n,30));
    r.phase=get.template operator()<std::uint32_t>(field(n,40));r.locked=get.template operator()<bool>(field(n,50));r.count=get.template operator()<std::uint64_t>(field(n,60));
    r.start=get.template operator()<std::uint64_t>(field(n,70));r.begin=get.template operator()<std::uint64_t>(field(n,80));r.end=get.template operator()<std::uint64_t>(field(n,90));
    r.finish=get.template operator()<std::uint64_t>(field(n,100));r.mask=get.template operator()<std::uint32_t>(field(n,110));r.cancels=get.template operator()<std::uint64_t>(field(n,120));
    r.cancelAt=get.template operator()<std::uint64_t>(field(n,130));r.generation=get.template operator()<std::uint64_t>(field(n,140));
    r.base=get.template operator()<std::uint64_t>(field(n,150));return r;}
struct Ledger {std::uint64_t attempts=0,at=0,cycle=0,requested=0,actual=0,before=0,after=0,sequence=0;
    SimEntityId target=0;std::uint32_t permission=0,code=0,cancel=0;};
template<class Get>Ledger readLedger(Get&& get,std::size_t n){Ledger r;r.attempts=get.template operator()<std::uint64_t>(field(n,10));
    r.at=get.template operator()<std::uint64_t>(field(n,20));r.cycle=get.template operator()<std::uint64_t>(field(n,30));r.target=get.template operator()<DetEntityRef>(field(n,40)).value;
    r.permission=get.template operator()<std::uint32_t>(field(n,50));r.code=get.template operator()<std::uint32_t>(field(n,60));r.requested=get.template operator()<std::uint64_t>(field(n,70));
    r.actual=get.template operator()<std::uint64_t>(field(n,80));r.before=get.template operator()<std::uint64_t>(field(n,90));r.after=get.template operator()<std::uint64_t>(field(n,100));
    r.sequence=get.template operator()<std::uint64_t>(field(n,110));r.cancel=get.template operator()<std::uint32_t>(field(n,120));return r;}
std::uint32_t phaseAt(const OwnerState& r,std::uint64_t tick){if(tick<r.begin)return 1;if(tick<r.end)return 2;if(tick<r.finish)return 3;return 0;}
template<class StateGet,class LedgerGet,class RecoveryGet>void validOwned(StateGet&& state,LedgerGet&& ledger,RecoveryGet&& recovery,
    const DetAttack2DConfig& c,const DetVitalityResult2D& vitality,std::uint64_t maximumStamp,bool boundary){
    const auto processed=state.template operator()<std::uint64_t>(DetAttackProcessedTick),total=ledger.template operator()<std::uint64_t>(DetAttackTotalAttempts);
    if(processed>maximumStamp || (boundary && processed!=maximumStamp) || total>vitality.sequence)invalid();
    std::uint64_t sum=0;std::set<std::uint64_t> sequences;
    for(std::size_t n=0;n<c.owners.size();++n){const auto& row=c.owners[n];const auto r=readState(state,n);const auto result=readLedger(ledger,n);
        if(r.phase>3 || r.mask>=(1u<<row.targets.size()) || r.count>processed || r.cancels>r.count || (boundary && r.held!=r.latched))invalid();
        if(!processed){if((boundary && (r.held || !r.facing)) || r.latched || !r.locked || r.count || r.phase || r.generation)invalid();}
        if(r.base>result.attempts || result.attempts-r.base!=static_cast<std::uint64_t>(std::popcount(r.mask)))invalid();
        if(!r.count){if(r.start || r.begin || r.end || r.finish || r.mask || r.phase || !r.locked || r.cancels || r.base)invalid();}
        else {if(!r.start || r.count>r.start || r.start>processed || r.begin!=add(r.start-1,row.warmupTicks) || r.end!=add(r.begin,row.activeTicks)
                || r.finish!=add(r.end,row.recoveryTicks))invalid();
            const auto expected=phaseAt(r,processed-1);const bool cancelled=r.cancelAt>=r.start;
            if(r.phase!=(cancelled?0u:expected))invalid();
            if(cancelled && (!r.cancels || r.cancelAt>processed || r.cancelAt>r.finish))invalid();}
        if(!r.cancels){if(r.cancelAt || result.cancel)invalid();}
        else if(!r.cancelAt || r.cancelAt>processed || result.cancel<1 || result.cancel>3)invalid();
        const auto restored=recovery(row.actor);
        if(r.generation>restored.restoreCount || (r.generation!=restored.restoreCount
            && (!c.vitality.respawnRecovery || !processed || stamp(restored.lastRestoreTick)!=processed || add(r.generation,1)!=restored.restoreCount)))invalid();
        if(!c.vitality.respawnRecovery && r.generation)invalid();
        sum=add(sum,result.attempts);
        if(!result.attempts){if(result.at || result.cycle || result.target || result.permission || result.code || result.requested || result.actual
            || result.before || result.after || result.sequence || r.mask)invalid();continue;}
        const auto cap=static_cast<std::uint64_t>(row.targets.size());
        if(!r.count || result.attempts/cap+(result.attempts%cap?1u:0u)>r.count || !result.at || result.at>processed
            || !result.cycle || result.cycle>r.count || !result.sequence || result.sequence>vitality.sequence || stamp(vitality.tick)<result.at
            || !sequences.insert(result.sequence).second || result.attempts<std::popcount(r.mask))invalid();
        const auto* target=actorRow(row.targets,result.target);if(!target || result.permission!=target->permission || result.requested!=target->amount
            || result.before>maximumHP(c.vitality,result.target) || result.after>maximumHP(c.vitality,result.target))invalid();
        const auto targetIndex=static_cast<std::size_t>(target-row.targets.data());
        if(result.cycle==r.count){if(result.at<r.start || result.at-1<r.begin || result.at-1>=r.end || !(r.mask&(1u<<targetIndex)))invalid();}
        else if(r.mask || result.at>=r.start)invalid();
        if(result.code==static_cast<std::uint32_t>(DetVitalityCode2D::Applied)){
            if(!result.before || !result.actual || result.actual!=std::min(result.before,result.requested) || result.after!=result.before-result.actual)invalid();}
        else {if(result.actual || result.after!=result.before || (result.code!=5 && result.code!=7 && result.code!=8 && result.code!=9)
                || (result.code==7 && result.before) || (result.code==8 && !result.before))invalid();}
        if(result.sequence==vitality.sequence && (vitality.actor!=result.target || vitality.permission!=result.permission || vitality.region
            || vitality.kind!=DetVitalityKind2D::Hurt || static_cast<std::uint32_t>(vitality.code)!=result.code || stamp(vitality.tick)!=result.at
            || vitality.requested!=result.requested || vitality.actual!=result.actual || vitality.hpBefore!=result.before || vitality.hpAfter!=result.after))invalid();
    }
    if(sum!=total)invalid();
}
void validateCheckpoint(const DetSessionCheckpoint& cp,const DetAttack2DConfig& c,const Plan& p){
    const auto vitality=detVitalityResult2D(cp,c.vitality);const auto m=detwire::manifestLayout(cp.manifest);
    if(m.systems.at(c.systemId)!=c.vitality.progress.actions.collision.priority || m.validatorVersions.at(c.validatorId)!=1
        || m.logicProfiles.at(c.logicProfileId)!=std::pair<std::uint32_t,std::uint64_t>{1,logicHash})invalid();
    descriptor(m,p.state);descriptor(m,p.ledger);
    const auto owned=[&](const auto& schema,bool immutable){descriptor(m,schema);const DetStateLayout layout(schema);const auto& words=cp.globals.at(schema.id);
        if(!layout.valid(words) || (immutable && words!=layout.defaults()))invalid();
        for(const auto& [id,actor]:cp.actors){(void)id;if(actor.blocks.at(schema.id)!=layout.defaults())invalid();}};
    owned(p.state,false);owned(p.ledger,false);for(const auto& s:p.policies)owned(s,true);
    // A real query validates all registered represented bodies, not only targets.
    const DetCollisionQuery2D snapshot(cp,c.vitality.progress.actions.collision);(void)snapshot;
    for(const auto& row:c.owners){const auto it=cp.actors.find(row.actor);
        if(it==cp.actors.end()){if(!cp.nextTick || !std::binary_search(cp.retiredIds.begin(),cp.retiredIds.end(),row.actor))invalid();continue;}
        const auto& body=it->second.blocks.at(p.body.id);if(readDetState<bool>(p.body,body,DetBodyTrigger) || readDetState<std::uint32_t>(p.body,body,DetBodyMode)!=2)invalid();
        const math::DetVec2 xy{math::DetFloat32::fromBits(it->second.pose.position[0]),math::DetFloat32::fromBits(it->second.pose.position[1])};
        (void)attackBox(row,xy,true);(void)attackBox(row,xy,false);}
    validOwned([&]<class T>(auto f){return readDetState<T>(p.state,cp.globals.at(p.state.id),f);},
        [&]<class T>(auto f){return readDetState<T>(p.ledger,cp.globals.at(p.ledger.id),f);},
        [&](auto id){return detVitalityRecoveryState2D(cp,c.vitality,id);},c,vitality,cp.nextTick,true);
}
Plan access(DetTickContext& ctx,const DetAttack2DConfig& c,bool writing){auto p=plan(c);policiesMatch(ctx,p);
    const auto vitality=detVitalityResult2D(ctx,c.vitality);
    validOwned([&]<class T>(auto f){return ctx.readGlobal<T>(c.stateSchema,f);},[&]<class T>(auto f){return ctx.readGlobal<T>(c.ledgerSchema,f);},
        [&](auto id){return detVitalityRecoveryState2D(ctx,c.vitality,id);},c,vitality,stamp(ctx.tick()),false);
    if(writing && (ctx.systemId()>=c.systemId || ctx.readGlobal<std::uint64_t>(c.stateSchema,DetAttackProcessedTick)>=stamp(ctx.tick())))invalid();return p;}
void writeState(DetTickContext& ctx,const DetAttack2DConfig& c,std::size_t n,const OwnerState& r){const auto s=c.stateSchema;
    ctx.writeGlobal(s,field(n,10),r.held);ctx.writeGlobal(s,field(n,20),r.facing);ctx.writeGlobal(s,field(n,30),r.latched);
    ctx.writeGlobal(s,field(n,40),r.phase);ctx.writeGlobal(s,field(n,50),r.locked);ctx.writeGlobal(s,field(n,60),r.count);ctx.writeGlobal(s,field(n,70),r.start);
    ctx.writeGlobal(s,field(n,80),r.begin);ctx.writeGlobal(s,field(n,90),r.end);ctx.writeGlobal(s,field(n,100),r.finish);ctx.writeGlobal(s,field(n,110),r.mask);
    ctx.writeGlobal(s,field(n,120),r.cancels);ctx.writeGlobal(s,field(n,130),r.cancelAt);ctx.writeGlobal(s,field(n,140),r.generation);ctx.writeGlobal(s,field(n,150),r.base);}
void writeLedger(DetTickContext& ctx,const DetAttack2DConfig& c,std::size_t n,const Ledger& r){const auto s=c.ledgerSchema;
    ctx.writeGlobal(s,field(n,10),r.attempts);ctx.writeGlobal(s,field(n,20),r.at);ctx.writeGlobal(s,field(n,30),r.cycle);ctx.writeGlobal(s,field(n,40),DetEntityRef{r.target});
    ctx.writeGlobal(s,field(n,50),r.permission);ctx.writeGlobal(s,field(n,60),r.code);ctx.writeGlobal(s,field(n,70),r.requested);ctx.writeGlobal(s,field(n,80),r.actual);
    ctx.writeGlobal(s,field(n,90),r.before);ctx.writeGlobal(s,field(n,100),r.after);ctx.writeGlobal(s,field(n,110),r.sequence);ctx.writeGlobal(s,field(n,120),r.cancel);}
bool run(DetTickContext& ctx,const DetAttack2DConfig& c){const auto p=access(ctx,c,false);
    if(ctx.systemId()!=c.systemId || ctx.readGlobal<std::uint64_t>(c.stateSchema,DetAttackProcessedTick)!=ctx.tick())invalid();
    const auto at=stamp(ctx.tick()),tick=ctx.tick();const auto ids=ctx.entities();
    const DetCollisionQuery2D snapshot(ctx,c.vitality.progress.actions.collision);
    // Both geometry and HP are captured before any owner can change another HP.
    std::map<SimEntityId,bool> alive;for(const auto& row:c.vitality.actors)alive.emplace(row.actor,!detVitalityState2D(ctx,c.vitality,row.actor).dead);
    std::vector<OwnerState> states;std::vector<Ledger> ledgers;std::vector<std::vector<std::size_t>> hits(c.owners.size());
    auto total=ctx.readGlobal<std::uint64_t>(c.ledgerSchema,DetAttackTotalAttempts);
    std::uint64_t prospectiveTotal=total;
    for(std::size_t n=0;n<c.owners.size();++n){const auto& row=c.owners[n];auto r=readState([&]<class T>(auto f){return ctx.readGlobal<T>(c.stateSchema,f);},n);
        auto result=readLedger([&]<class T>(auto f){return ctx.readGlobal<T>(c.ledgerSchema,f);},n);
        const bool present=std::binary_search(ids.begin(),ids.end(),row.actor),edge=r.held && !r.latched;
        const auto generation=detVitalityRecoveryState2D(ctx,c.vitality,row.actor).restoreCount;
        const bool open=r.phase && tick<r.finish;bool cancelled=false;
        if(open && (!present || !alive.at(row.actor) || (c.cancelOnRecovery && generation!=r.generation))){
            r.cancels=add(r.cancels,1);r.cancelAt=at;r.phase=0;cancelled=true;
            result.cancel=!present?3u:(!alive.at(row.actor)?1u:2u);}
        else if(open)r.phase=phaseAt(r,tick);else r.phase=0;
        // Cancellation consumes the current edge. Busy edges are never queued.
        if(edge && !cancelled && !r.phase && present && alive.at(row.actor)){
            r.count=add(r.count,1);r.start=at;r.begin=add(tick,row.warmupTicks);r.end=add(r.begin,row.activeTicks);
            r.finish=add(r.end,row.recoveryTicks);r.phase=phaseAt(r,tick);r.locked=r.facing;r.mask=0;r.base=result.attempts;}
        r.latched=r.held;r.generation=generation;
        if(r.phase==2 && present && alive.at(row.actor)){
            if(ctx.read<bool>(row.actor,p.body.id,DetBodyTrigger) || ctx.read<std::uint32_t>(row.actor,p.body.id,DetBodyMode)!=2)invalid();
            const auto position=ctx.pose(row.actor).position;const auto box=attackBox(row,{position.x,position.y},r.locked);
            const auto candidates=snapshot.overlap(box,{ctx.read<std::uint32_t>(row.actor,p.body.id,DetBodyLayer),
                ctx.read<std::uint32_t>(row.actor,p.body.id,DetBodyMask),row.actor,false},true);
            for(std::size_t target=0;target<row.targets.size();++target)if(!(r.mask&(1u<<target)) && alive.at(row.targets[target].actor)
                && std::binary_search(candidates.begin(),candidates.end(),row.targets[target].actor))hits[n].push_back(target);
            (void)add(result.attempts,static_cast<std::uint64_t>(hits[n].size()));prospectiveTotal=add(prospectiveTotal,static_cast<std::uint64_t>(hits[n].size()));
        }
        states.push_back(r);ledgers.push_back(result);
    }
    // All attack-local arithmetic and all boxes/queries precede numerical HP writes.
    (void)prospectiveTotal;for(std::size_t n=0;n<states.size();++n){writeState(ctx,c,n,states[n]);writeLedger(ctx,c,n,ledgers[n]);}
    for(std::size_t n=0;n<c.owners.size();++n)for(const auto targetIndex:hits[n]){auto& state=states[n];auto& result=ledgers[n];const auto& target=c.owners[n].targets[targetIndex];
        const auto attempts=add(result.attempts,1),nextTotal=add(total,1);
        const auto actual=executeDetVitality2D(ctx,c.vitality,target.permission,target.amount);
        result.attempts=attempts;result.at=at;result.cycle=state.count;result.target=actual.actor;result.permission=actual.permission;
        result.code=static_cast<std::uint32_t>(actual.code);result.requested=actual.requested;result.actual=actual.actual;result.before=actual.hpBefore;result.after=actual.hpAfter;result.sequence=actual.sequence;
        state.mask|=1u<<targetIndex;total=nextTotal;writeState(ctx,c,n,state);writeLedger(ctx,c,n,result);ctx.writeGlobal(c.ledgerSchema,DetAttackTotalAttempts,total);
    }
    ctx.writeGlobal(c.stateSchema,DetAttackProcessedTick,at);return true;
}
DetAttackState2D copy(const OwnerState& r){DetAttackState2D value;value.input={r.held,r.facing};value.latchedHeld=r.latched;value.lockedFacingRight=r.locked;
    value.phase=static_cast<DetAttackPhase2D>(r.phase);value.attackCount=r.count;value.startTick=r.count?r.start-1:0;value.activeBegin=r.begin;value.activeEnd=r.end;value.recoveryEnd=r.finish;
    value.cancelCount=r.cancels;value.lastCancelTick=r.cancels?r.cancelAt-1:0;value.recoveryGeneration=r.generation;value.hitMask=r.mask;value.cycleAttemptBase=r.base;return value;}
DetAttackResult2D copy(const Ledger& r){DetAttackResult2D value;value.attemptCount=r.attempts;value.tick=r.attempts?r.at-1:0;value.attackCount=r.cycle;value.vitalitySequence=r.sequence;
    value.target=r.target;value.permission=r.permission;value.code=static_cast<DetVitalityCode2D>(r.code);value.requested=r.requested;value.actual=r.actual;value.hpBefore=r.before;value.hpAfter=r.after;
    value.lastCancelReason=static_cast<DetAttackCancel2D>(r.cancel);return value;}
} // namespace
std::vector<std::uint8_t> encodeDetAttackConfig2D(const DetAttack2DConfig& c){return plan(c).bytes;}
std::vector<DetTypedStateSchema> detAttackPolicySchemas2D(const DetAttack2DConfig& c){return plan(c).policies;}
std::vector<DetTypedStateSchema> detAttackStateSchemas2D(const DetAttack2DConfig& c){const auto p=plan(c);return {p.state,p.ledger};}
std::uint32_t detAttackField2D(const DetAttack2DConfig& c,SimEntityId id,DetAttackField2D member){(void)plan(c);
    const auto value=static_cast<std::uint32_t>(member);if(value<10 || value>150 || value%10)invalid();return field(ownerIndex(c,id),value);}
std::uint32_t detAttackLedgerField2D(const DetAttack2DConfig& c,SimEntityId id,DetAttackLedgerField2D member){(void)plan(c);
    const auto value=static_cast<std::uint32_t>(member);if(value<10 || value>120 || value%10)invalid();return field(ownerIndex(c,id),value);}
bool installDetAttack2D(DeterministicSession& session,DetAttack2DConfig c){Plan p;
    try{p=plan(c);if(session.sealed() || session.faulted() || session.nextTick())return false;}catch(const std::invalid_argument&){return false;}
    if(!(session.registerTypedSchema(p.state) && session.registerGlobalState(p.state.id)
        && session.registerTypedSchema(p.ledger) && session.registerGlobalState(p.ledger.id)))return false;
    for(const auto& s:p.policies)if(!(session.registerTypedSchema(s) && session.registerGlobalState(s.id)))return false;
    return session.registerLogicProfile(c.logicProfileId,1,logicHash)
        && session.registerSystem(c.systemId,c.vitality.progress.actions.collision.priority,[c](DetTickContext& ctx){return run(ctx,c);})
        && session.registerValidator(c.validatorId,1,[c,p](const DetSessionCheckpoint& cp,std::string& error){
            try{validateCheckpoint(cp,c,p);return true;}catch(const std::exception& e){error=e.what();return false;}});}
void detSetAttackInput2D(DetTickContext& ctx,const DetAttack2DConfig& c,SimEntityId id,DetAttackInput2D input){(void)access(ctx,c,true);const auto n=ownerIndex(c,id);
    ctx.writeGlobal(c.stateSchema,field(n,10),input.held);ctx.writeGlobal(c.stateSchema,field(n,20),input.facingRight);}
DetAttackState2D detAttackState2D(DetTickContext& ctx,const DetAttack2DConfig& c,SimEntityId id){(void)access(ctx,c,false);
    return copy(readState([&]<class T>(auto f){return ctx.readGlobal<T>(c.stateSchema,f);},ownerIndex(c,id)));}
DetAttackState2D detAttackState2D(const DetSessionCheckpoint& cp,const DetAttack2DConfig& c,SimEntityId id){const auto p=plan(c);validateCheckpoint(cp,c,p);
    return copy(readState([&]<class T>(auto f){return readDetState<T>(p.state,cp.globals.at(p.state.id),f);},ownerIndex(c,id)));}
DetAttackResult2D detAttackResult2D(DetTickContext& ctx,const DetAttack2DConfig& c,SimEntityId id){(void)access(ctx,c,false);
    return copy(readLedger([&]<class T>(auto f){return ctx.readGlobal<T>(c.ledgerSchema,f);},ownerIndex(c,id)));}
DetAttackResult2D detAttackResult2D(const DetSessionCheckpoint& cp,const DetAttack2DConfig& c,SimEntityId id){const auto p=plan(c);validateCheckpoint(cp,c,p);
    return copy(readLedger([&]<class T>(auto f){return readDetState<T>(p.ledger,cp.globals.at(p.ledger.id),f);},ownerIndex(c,id)));}
} // namespace ayt::entity
