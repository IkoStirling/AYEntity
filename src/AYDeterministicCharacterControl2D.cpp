// design reference: AYEntity/design.md Stage55 finite registered character input ownership.
#include <AYEntity/DeterministicCharacterControl2D.h>
#include <AYEntity/DeterministicCollisionQuery2D.h>
#include "detail/DetCharacterController2DIdentity.h"
#include "detail/DetCharacterRespawn2D.h"
#include "detail/DetSessionWire.h"
#include <algorithm>
#include <set>
#include <stdexcept>

namespace ayt::entity {
namespace {
using D=math::DetFloat32;using Bytes=std::vector<std::uint8_t>;
constexpr std::uint64_t logicHash=UINT64_C(0x4443543244503031);
constexpr std::size_t chunkBytes=1000;
[[noreturn]] void invalid(){throw std::invalid_argument("deterministic character control 2D: invalid policy, input owner, timeline or dependency");}
std::uint64_t add(std::uint64_t a,std::uint64_t b){if(a>UINT64_MAX-b)invalid();return a+b;}
std::uint64_t stamp(std::uint64_t tick){return add(tick,1);}
D abs(D v){return v<D{}?-v:v;}
std::uint32_t field(std::size_t n,std::uint32_t f){return static_cast<std::uint32_t>(1000+200*n)+f;}
std::size_t ownerIndex(const DetCharacterControl2DConfig& c,SimEntityId id){const auto i=std::lower_bound(c.owners.begin(),c.owners.end(),id,
    [](const auto& owner,auto key){return owner.actor<key;});if(i==c.owners.end() || i->actor!=id)invalid();return static_cast<std::size_t>(i-c.owners.begin());}
struct Writer {
    Bytes bytes;
    void u(std::uint64_t value,unsigned width=4){for(unsigned n=0;n<width;++n)bytes.push_back(static_cast<std::uint8_t>(value>>(8*n)));}
    void schema(const DetTypedStateSchema& s){u(s.id);u(s.version);u(s.fields.size());for(const auto& f:s.fields){u(f.id);u(static_cast<std::uint32_t>(detStateType(f.initial)));
        const auto lanes=encodeDetStateValue(f.initial);u(lanes.size());for(auto lane:lanes)u(lane,8);}}
};
struct Plan {
    DetTypedStateSchema input,state,characterState,characterPolicy,body;
    std::optional<DetTypedStateSchema> respawnState,respawnPolicy;
    std::vector<DetTypedStateSchema> policies;
    std::pair<std::uint32_t,std::uint64_t> characterIdentity;
    Bytes bytes;
};
bool equalDescriptor(const DetTypedStateSchema& a,const DetTypedStateSchema& b){Writer x,y;x.schema(a);y.schema(b);return x.bytes==y.bytes;}
Plan plan(const DetCharacterControl2DConfig& c){
    if(c.owners.empty() || c.owners.size()>kDetCharacterControl2DMaxOwners || !c.character.advanced
        || c.inputSchema<2 || c.stateSchema<2 || c.policySchemaBase<2 || c.inputSchema==UINT32_MAX
        || c.stateSchema==UINT32_MAX || c.policySchemaBase==UINT32_MAX || !c.systemId || !c.validatorId || !c.logicProfileId
        || c.systemId==UINT32_MAX || c.validatorId==UINT32_MAX || c.logicProfileId==UINT32_MAX || c.systemId>=c.character.preSystemId
        || c.character.collision.characterRespawn!=c.respawn.has_value())invalid();
    Plan p;p.characterState=detCharacterStateSchema2D(c.character);p.characterPolicy=detCharacterPolicySchema2D(c.character);
    p.characterIdentity=detail::characterControllerIdentity2D(c.character);p.body=detCollisionBodySchema2D(c.character.collision);
    if(c.respawn){if(!detail::validRespawnConfig(*c.respawn)
            || !equalDescriptor(p.characterState,detCharacterStateSchema2D(c.respawn->character))
            || !equalDescriptor(p.characterPolicy,detCharacterPolicySchema2D(c.respawn->character)))invalid();
        p.respawnState=detail::respawnStateSchema(*c.respawn);p.respawnPolicy=detail::respawnPolicySchema(*c.respawn);}
    p.input={c.inputSchema,1,{}};p.state={c.stateSchema,1,{{DetControlProcessedTick,std::uint64_t{}}}};
    Writer w;w.u(1);w.u(c.inputSchema);w.u(c.stateSchema);w.u(c.policySchemaBase);w.u(c.systemId);w.u(c.validatorId);w.u(c.logicProfileId);
    w.u(p.characterIdentity.first);w.u(p.characterIdentity.second,8);w.schema(p.characterState);w.schema(p.characterPolicy);w.schema(p.body);
    w.u(c.respawn?1u:0u);if(c.respawn){w.schema(*p.respawnState);w.schema(*p.respawnPolicy);}w.u(c.owners.size());
    SimEntityId previous=0;
    for(std::size_t n=0;n<c.owners.size();++n){const auto& row=c.owners[n];if(!row.actor || row.actor<=previous || !row.dashSpeed.isFinite()
            || !(row.dashSpeed>D{}) || row.dashSpeed>c.character.maxHorizontalSpeed || !row.durationTicks || row.durationTicks>65535 || row.cooldownTicks>65535)invalid();
        previous=row.actor;w.u(row.actor,8);w.u(row.dashSpeed.bits());w.u(row.durationTicks);w.u(row.cooldownTicks);w.u(row.groundOnly?1u:0u);w.u(row.initialFacingRight?1u:0u);
        for(std::uint32_t member=10;member<=150;member+=10){
            if(member==10 || member==90)p.input.fields.push_back({field(n,member),D{}});
            else p.input.fields.push_back({field(n,member),member==60?row.initialFacingRight:false});
            if(member==10 || member==110)p.state.fields.push_back({field(n,member),std::uint32_t{}});
            else if(member==20)p.state.fields.push_back({field(n,member),row.initialFacingRight});
            else if(member==100 || member==120)p.state.fields.push_back({field(n,member),false});
            else p.state.fields.push_back({field(n,member),std::uint64_t{}});
        }
    }
    p.bytes=std::move(w.bytes);if(p.bytes.empty() || p.bytes.size()>kDetCharacterControl2DMaxPolicyBytes)invalid();
    const auto chunks=(p.bytes.size()+chunkBytes-1)/chunkBytes;if(chunks>9 || c.policySchemaBase>UINT32_MAX-chunks)invalid();
    std::set<std::uint32_t> schemas{p.characterState.id,p.characterPolicy.id,p.body.id,c.character.collision.historySchema};
    std::set<std::uint32_t> systems{c.character.preSystemId,c.character.collision.systemId,c.character.postSystemId};
    std::set<std::uint32_t> validators{c.character.validatorId,c.character.collision.systemId},logic{c.character.logicProfileId};
    if(c.respawn){if(!schemas.insert(p.respawnState->id).second || !schemas.insert(p.respawnPolicy->id).second
            || !validators.insert(c.respawn->validatorId).second || !logic.insert(c.respawn->logicProfileId).second)invalid();}
    if(systems.contains(c.systemId) || validators.contains(c.validatorId) || logic.contains(c.logicProfileId)
        || !schemas.insert(p.input.id).second || !schemas.insert(p.state.id).second)invalid();
    for(std::size_t n=0;n<chunks;++n){const auto id=c.policySchemaBase+static_cast<std::uint32_t>(n);if(!schemas.insert(id).second)invalid();
        DetTypedStateSchema s{id,1,{{1,static_cast<std::uint32_t>(n)},{2,static_cast<std::uint32_t>(chunks)},{3,static_cast<std::uint32_t>(p.bytes.size())}}};
        const auto begin=n*chunkBytes,end=std::min(begin+chunkBytes,p.bytes.size());
        for(std::size_t offset=begin;offset<end;offset+=8){std::uint64_t word=0;for(std::size_t byte=0;byte<8 && offset+byte<end;++byte)word|=std::uint64_t{p.bytes[offset+byte]}<<(8*byte);
            s.fields.push_back({static_cast<std::uint32_t>(4+(offset-begin)/8),word});}p.policies.push_back(std::move(s));}
    if(schemas.size()>detwire::maxSchemas)invalid();(void)detStateDefaults(p.input);(void)detStateDefaults(p.state);return p;
}
struct State {
    DetCharacterControlInput2D input;
    DetCharacterInput2D emitted;
    bool latched=false,present=false,accepted=false,edge=false,facing=true,canceled=false,grounded=false;
    std::uint32_t phase=0,outcome=0;
    std::uint64_t start=0,end=0,ready=0,requests=0,starts=0,denied=0,cancels=0,respawnAt=0,startAt=0,cancelAt=0;
};
template<class InputRead,class StateRead>State readState(InputRead&& i,StateRead&& s,std::size_t n){State r;
    r.input.character.targetSpeed=i.template operator()<D>(field(n,10));r.input.character.jumpPressed=i.template operator()<bool>(field(n,20));
    r.input.character.crouchHeld=i.template operator()<bool>(field(n,30));r.input.character.dropPressed=i.template operator()<bool>(field(n,40));
    r.input.dashHeld=i.template operator()<bool>(field(n,50));r.input.facingRight=i.template operator()<bool>(field(n,60));r.input.cancel=i.template operator()<bool>(field(n,70));
    r.latched=i.template operator()<bool>(field(n,80));r.emitted.targetSpeed=i.template operator()<D>(field(n,90));r.emitted.jumpPressed=i.template operator()<bool>(field(n,100));
    r.emitted.crouchHeld=i.template operator()<bool>(field(n,110));r.emitted.dropPressed=i.template operator()<bool>(field(n,120));r.present=i.template operator()<bool>(field(n,130));
    r.accepted=i.template operator()<bool>(field(n,140));r.edge=i.template operator()<bool>(field(n,150));
    r.phase=s.template operator()<std::uint32_t>(field(n,10));r.facing=s.template operator()<bool>(field(n,20));r.start=s.template operator()<std::uint64_t>(field(n,30));
    r.end=s.template operator()<std::uint64_t>(field(n,40));r.ready=s.template operator()<std::uint64_t>(field(n,50));r.requests=s.template operator()<std::uint64_t>(field(n,60));
    r.starts=s.template operator()<std::uint64_t>(field(n,70));r.denied=s.template operator()<std::uint64_t>(field(n,80));r.cancels=s.template operator()<std::uint64_t>(field(n,90));
    r.canceled=s.template operator()<bool>(field(n,100));r.outcome=s.template operator()<std::uint32_t>(field(n,110));r.grounded=s.template operator()<bool>(field(n,120));
    r.respawnAt=s.template operator()<std::uint64_t>(field(n,130));r.startAt=s.template operator()<std::uint64_t>(field(n,140));r.cancelAt=s.template operator()<std::uint64_t>(field(n,150));return r;
}
bool validInput(const DetCharacterControlInput2D& input,const DetCharacterController2DConfig& c){return input.character.targetSpeed.isFinite() && abs(input.character.targetSpeed)<=c.maxHorizontalSpeed;}
std::uint32_t phase(const State& r,std::uint64_t tick){if(!r.present)return 3;if(r.starts && !r.canceled && tick<r.end)return 1;return r.starts && tick<r.ready?2u:0u;}
bool empty(const DetCharacterInput2D& input){return input.targetSpeed.bits()==0 && !input.jumpPressed && !input.crouchHeld && !input.dropPressed;}
template<class InputRead,class StateRead>void validOwned(InputRead&& input,StateRead&& state,const DetCharacterControl2DConfig& c,std::uint64_t processed,bool boundary){
    if(state.template operator()<std::uint64_t>(DetControlProcessedTick)!=processed)invalid();
    for(std::size_t n=0;n<c.owners.size();++n){const auto& row=c.owners[n];const auto r=readState(input,state,n);
        if(!validInput(r.input,c.character) || !r.emitted.targetSpeed.isFinite() || abs(r.emitted.targetSpeed)>c.character.maxHorizontalSpeed
            || r.phase>3 || r.outcome>11 || r.starts>r.requests || r.denied>r.requests || add(r.starts,r.denied)!=r.requests
            || r.requests>processed || r.cancels>r.starts || r.startAt>processed || r.cancelAt>processed)invalid();
        if(!processed){if(r.phase || r.outcome || r.start || r.end || r.ready || r.requests || r.starts || r.denied || r.cancels || r.canceled
                || r.startAt || r.cancelAt || r.latched || r.present || r.accepted || r.edge || r.grounded || r.respawnAt || !empty(r.emitted)
                || r.facing!=row.initialFacingRight)invalid();
            if(boundary && (!empty(r.input.character) || r.input.dashHeld || r.input.cancel || r.input.facingRight!=row.initialFacingRight))invalid();continue;}
        const auto tick=processed-1;
        if(r.starts){if(r.end!=add(r.start,row.durationTicks) || r.ready!=add(r.end,row.cooldownTicks) || r.startAt!=stamp(r.start)
                || r.start>tick || r.starts>r.startAt || (r.canceled?(!r.cancels || r.cancelAt<=r.startAt || r.cancelAt>r.end):(r.cancelAt>=r.startAt || r.cancels>=r.starts)))invalid();}
        else if(r.start || r.end || r.ready || r.startAt || r.cancelAt || r.cancels || r.canceled || r.facing!=row.initialFacingRight)invalid();
        if(r.cancels?!r.cancelAt:r.cancelAt!=0)invalid();
        if(r.phase!=phase(r,tick) || !r.outcome || r.respawnAt>tick || (!c.respawn && (r.respawnAt || r.accepted))
            || (r.accepted && (!tick || r.respawnAt!=tick || !r.present)) || (!r.present && (r.grounded || r.accepted || !empty(r.emitted))))invalid();
        if(r.phase==1){const auto speed=r.facing?row.dashSpeed:-row.dashSpeed;if(r.emitted.targetSpeed.bits()!=speed.bits())invalid();}
        if(!boundary)continue;
        if(r.latched!=r.input.dashHeld || (r.edge && !r.latched))invalid();
        if(r.present){if(r.emitted.jumpPressed!=r.input.character.jumpPressed || r.emitted.crouchHeld!=r.input.character.crouchHeld
                || r.emitted.dropPressed!=r.input.character.dropPressed || (r.phase!=1 && r.emitted.targetSpeed.bits()!=r.input.character.targetSpeed.bits()))invalid();}
        switch(static_cast<DetCharacterControlOutcome2D>(r.outcome)){
        case DetCharacterControlOutcome2D::Normal:if(!r.present || r.edge || r.phase==1)invalid();break;
        case DetCharacterControlOutcome2D::Active:if(!r.present || r.edge || r.phase!=1)invalid();break;
        case DetCharacterControlOutcome2D::Started:if(!r.present || !r.edge || r.phase!=1 || r.start!=tick || r.input.cancel || r.accepted
                || r.facing!=r.input.facingRight || (row.groundOnly && !r.grounded))invalid();break;
        case DetCharacterControlOutcome2D::DeniedActive:if(!r.present || !r.edge || r.phase!=1 || r.input.cancel || r.accepted || !r.denied)invalid();break;
        case DetCharacterControlOutcome2D::DeniedCooldown:if(!r.present || !r.edge || r.phase!=2 || r.input.cancel || r.accepted || !r.denied)invalid();break;
        case DetCharacterControlOutcome2D::DeniedGround:if(!r.present || !r.edge || r.phase!=0 || !row.groundOnly || r.grounded || r.input.cancel || r.accepted || !r.denied)invalid();break;
        case DetCharacterControlOutcome2D::DeniedCancel:if(!r.present || !r.edge || !r.input.cancel || r.phase==1 || !r.denied)invalid();break;
        case DetCharacterControlOutcome2D::DeniedRelocation:if(!r.present || !r.edge || r.input.cancel || !r.accepted || r.phase==1 || !r.denied)invalid();break;
        case DetCharacterControlOutcome2D::Canceled:if(!r.present || r.edge || !r.input.cancel || !r.canceled || r.cancelAt!=processed || r.phase==1)invalid();break;
        case DetCharacterControlOutcome2D::RelocationCanceled:if(!r.present || r.edge || r.input.cancel || !r.accepted || !r.canceled || r.cancelAt!=processed || r.phase==1)invalid();break;
        case DetCharacterControlOutcome2D::Retired:if(r.present || r.phase!=3 || (r.edge && !r.denied))invalid();break;
        default:invalid();}
    }
}
void descriptor(const detwire::ManifestLayout& m,const DetTypedStateSchema& s,bool global){if(m.schemaVersions.at(s.id)!=s.version || (global && !m.globals.contains(s.id)))invalid();
    const auto& actual=m.schemas.at(s.id);if(actual.size()!=s.fields.size())invalid();for(std::size_t n=0;n<actual.size();++n){const auto& f=s.fields[n];
        if(actual[n].id!=f.id || actual[n].type!=detStateType(f.initial) || actual[n].width!=encodeDetStateValue(f.initial).size())invalid();}}
void policyMatches(DetTickContext& ctx,const DetCharacterControl2DConfig& c,const Plan& p){const auto check=[&](const DetTypedStateSchema& schema){for(const auto& f:schema.fields)
        std::visit([&]<class T>(const T& expected){if(encodeDetStateValue(ctx.readGlobal<T>(schema.id,f.id))!=encodeDetStateValue(expected))invalid();},f.initial);};
    for(const auto& s:p.policies)check(s);check(p.characterPolicy);if(c.respawn)detail::respawnPolicyMatches(ctx,*c.respawn);}
void pure(const DetSessionCheckpoint& cp,const DetCharacterControl2DConfig& c,const Plan& p){
    (void)DetCollisionQuery2D(cp,c.character.collision);detail::validateCharacterCheckpoint2D(cp,c.character);if(c.respawn)detail::validateRespawnCheckpoint(cp,*c.respawn);
    const auto m=detwire::manifestLayout(cp.manifest);if(m.systems.at(c.systemId)!=c.character.collision.priority || m.validatorVersions.at(c.validatorId)!=1
        || m.logicProfiles.at(c.logicProfileId)!=std::pair<std::uint32_t,std::uint64_t>{1,logicHash}
        || m.systems.at(c.character.preSystemId)!=c.character.collision.priority || m.systems.at(c.character.postSystemId)!=c.character.collision.priority
        || m.systems.at(c.character.collision.systemId)!=c.character.collision.priority
        || m.validatorVersions.at(c.character.validatorId)!=p.characterIdentity.first || m.logicProfiles.at(c.character.logicProfileId)!=p.characterIdentity)invalid();
    descriptor(m,p.characterState,false);descriptor(m,p.characterPolicy,true);
    const auto owned=[&](const DetTypedStateSchema& schema,bool immutable){descriptor(m,schema,true);const DetStateLayout layout(schema);const auto& words=cp.globals.at(schema.id);
        if(!layout.valid(words) || (immutable && words!=layout.defaults()))invalid();for(const auto& [id,actor]:cp.actors){(void)id;if(actor.blocks.at(schema.id)!=layout.defaults())invalid();}};
    owned(p.input,false);owned(p.state,false);for(const auto& s:p.policies)owned(s,true);
    const auto input=[&]<class T>(auto f){return readDetState<T>(p.input,cp.globals.at(c.inputSchema),f);};
    const auto state=[&]<class T>(auto f){return readDetState<T>(p.state,cp.globals.at(c.stateSchema),f);};validOwned(input,state,c,cp.nextTick,true);
    for(std::size_t n=0;n<c.owners.size();++n){const auto id=c.owners[n].actor;const auto r=readState(input,state,n);const auto a=cp.actors.find(id);
        if(a==cp.actors.end()){if(!cp.nextTick || !std::binary_search(cp.retiredIds.begin(),cp.retiredIds.end(),id))invalid();continue;}
        if(cp.nextTick && !r.present)invalid();const auto& character=a->second.blocks.at(p.characterState.id);const auto& body=a->second.blocks.at(p.body.id);
        if(!readDetState<bool>(p.characterState,character,DetCharacterEnabled) || readDetState<std::uint32_t>(p.body,body,DetBodyMode)!=2
            || readDetState<bool>(p.body,body,DetBodyTrigger))invalid();
        bool reset=false;if(c.respawn){const auto result=detCharacterRespawnState2D(cp,id,*c.respawn);
            reset=cp.nextTick && result.result==DetRespawnResult2D::Accepted && result.resultTick==cp.nextTick;}
        if(readDetState<D>(p.characterState,character,DetCharacterTargetSpeed).bits()!=(reset?D{}.bits():r.emitted.targetSpeed.bits())
            || readDetState<bool>(p.characterState,character,DetCharacterJumpHeld)!=r.emitted.jumpPressed
            || readDetState<bool>(p.characterState,character,DetCharacterDropHeld)!=r.emitted.dropPressed
            || readDetState<bool>(p.characterState,character,DetCharacterCrouchHeld)!=(reset?false:r.emitted.crouchHeld))invalid();
    }
}
void writeRaw(DetTickContext& ctx,const DetCharacterControl2DConfig& c,std::size_t n,const DetCharacterControlInput2D& i){const auto s=c.inputSchema;
    ctx.writeGlobal(s,field(n,10),i.character.targetSpeed);ctx.writeGlobal(s,field(n,20),i.character.jumpPressed);ctx.writeGlobal(s,field(n,30),i.character.crouchHeld);
    ctx.writeGlobal(s,field(n,40),i.character.dropPressed);ctx.writeGlobal(s,field(n,50),i.dashHeld);ctx.writeGlobal(s,field(n,60),i.facingRight);ctx.writeGlobal(s,field(n,70),i.cancel);}
void writeState(DetTickContext& ctx,const DetCharacterControl2DConfig& c,std::size_t n,const State& r){const auto i=c.inputSchema,s=c.stateSchema;
    ctx.writeGlobal(i,field(n,80),r.latched);ctx.writeGlobal(i,field(n,90),r.emitted.targetSpeed);ctx.writeGlobal(i,field(n,100),r.emitted.jumpPressed);
    ctx.writeGlobal(i,field(n,110),r.emitted.crouchHeld);ctx.writeGlobal(i,field(n,120),r.emitted.dropPressed);ctx.writeGlobal(i,field(n,130),r.present);
    ctx.writeGlobal(i,field(n,140),r.accepted);ctx.writeGlobal(i,field(n,150),r.edge);
    ctx.writeGlobal(s,field(n,10),r.phase);ctx.writeGlobal(s,field(n,20),r.facing);ctx.writeGlobal(s,field(n,30),r.start);ctx.writeGlobal(s,field(n,40),r.end);
    ctx.writeGlobal(s,field(n,50),r.ready);ctx.writeGlobal(s,field(n,60),r.requests);ctx.writeGlobal(s,field(n,70),r.starts);ctx.writeGlobal(s,field(n,80),r.denied);
    ctx.writeGlobal(s,field(n,90),r.cancels);ctx.writeGlobal(s,field(n,100),r.canceled);ctx.writeGlobal(s,field(n,110),r.outcome);ctx.writeGlobal(s,field(n,120),r.grounded);
    ctx.writeGlobal(s,field(n,130),r.respawnAt);ctx.writeGlobal(s,field(n,140),r.startAt);ctx.writeGlobal(s,field(n,150),r.cancelAt);}
void decide(State& r,const DetCharacterControlOwner2D& row,std::uint64_t tick){
    r.edge=r.input.dashHeld && !r.latched;r.latched=r.input.dashHeld;r.outcome=static_cast<std::uint32_t>(DetCharacterControlOutcome2D::Normal);
    const bool wasActive=r.starts && !r.canceled && tick<r.end;
    if(wasActive && (r.input.cancel || r.accepted || !r.present)){r.canceled=true;r.cancels=add(r.cancels,1);r.cancelAt=stamp(tick);
        r.outcome=static_cast<std::uint32_t>(r.input.cancel?DetCharacterControlOutcome2D::Canceled:DetCharacterControlOutcome2D::RelocationCanceled);}
    if(r.edge){r.requests=add(r.requests,1);auto result=DetCharacterControlOutcome2D::Started;
        if(!r.present)result=DetCharacterControlOutcome2D::Retired;
        else if(r.input.cancel)result=DetCharacterControlOutcome2D::DeniedCancel;
        else if(r.accepted)result=DetCharacterControlOutcome2D::DeniedRelocation;
        else if(r.starts && !r.canceled && tick<r.end)result=DetCharacterControlOutcome2D::DeniedActive;
        else if(r.starts && tick<r.ready)result=DetCharacterControlOutcome2D::DeniedCooldown;
        else if(row.groundOnly && !r.grounded)result=DetCharacterControlOutcome2D::DeniedGround;
        if(result==DetCharacterControlOutcome2D::Started){r.starts=add(r.starts,1);r.start=tick;r.end=add(tick,row.durationTicks);r.ready=add(r.end,row.cooldownTicks);
            r.startAt=stamp(tick);r.canceled=false;r.facing=r.input.facingRight;}else r.denied=add(r.denied,1);r.outcome=static_cast<std::uint32_t>(result);}
    r.phase=phase(r,tick);r.emitted=r.present?r.input.character:DetCharacterInput2D{};
    if(!r.present)r.outcome=static_cast<std::uint32_t>(DetCharacterControlOutcome2D::Retired);
    else if(r.phase==1){r.emitted.targetSpeed=r.facing?row.dashSpeed:-row.dashSpeed;
        if(!r.edge)r.outcome=static_cast<std::uint32_t>(DetCharacterControlOutcome2D::Active);}
}
bool run(DetTickContext& ctx,const DetCharacterControl2DConfig& c,const Plan& p){policyMatches(ctx,c,p);
    if(ctx.systemId()!=c.systemId)invalid();const auto input=[&]<class T>(auto f){return ctx.readGlobal<T>(c.inputSchema,f);};
    const auto state=[&]<class T>(auto f){return ctx.readGlobal<T>(c.stateSchema,f);};validOwned(input,state,c,ctx.tick(),false);
    const auto ids=ctx.entities();const auto processed=stamp(ctx.tick());std::vector<State> rows;rows.reserve(c.owners.size());
    for(std::size_t n=0;n<c.owners.size();++n){const auto id=c.owners[n].actor;auto r=readState(input,state,n);
        r.present=std::binary_search(ids.begin(),ids.end(),id);r.grounded=false;r.accepted=false;r.respawnAt=0;
        if(r.present){const auto character=detCharacterState2D(ctx,id,c.character);if(!character.enabled || ctx.read<std::uint32_t>(id,p.body.id,DetBodyMode)!=2
                || ctx.read<bool>(id,p.body.id,DetBodyTrigger))invalid();r.grounded=character.grounded;
            if(c.respawn){const auto result=detCharacterRespawnState2D(ctx,id,*c.respawn);const auto* spawn=detail::respawnSpawn(*c.respawn,result.spawn);
                if(static_cast<std::uint32_t>(result.result)>4 || result.resultTick>ctx.tick()
                    || (result.result==DetRespawnResult2D::None?(result.spawn!=0 || result.resultTick!=0):(!spawn || spawn->actor!=id || !result.resultTick))
                    || ctx.read<bool>(id,c.respawn->stateSchema,DetRespawnPreCompleted))invalid();r.respawnAt=result.resultTick;
                r.accepted=ctx.tick()!=0 && result.result==DetRespawnResult2D::Accepted && result.resultTick==ctx.tick();}}
        decide(r,c.owners[n],ctx.tick());rows.push_back(std::move(r));}
    // All decisions/counts/window arithmetic are complete before the first write.
    for(std::size_t n=0;n<rows.size();++n){if(rows[n].present)detCharacterInput2D(ctx,c.owners[n].actor,c.character,rows[n].emitted);writeState(ctx,c,n,rows[n]);}
    ctx.writeGlobal(c.stateSchema,DetControlProcessedTick,processed);return true;
}
DetCharacterControlState2D copy(const State& r,std::uint64_t processed){DetCharacterControlState2D v;v.input=r.input;v.emitted=r.emitted;
    v.phase=static_cast<DetCharacterControlPhase2D>(r.phase);v.outcome=static_cast<DetCharacterControlOutcome2D>(r.outcome);v.dashLatched=r.latched;v.risingEdge=r.edge;
    v.lockedFacingRight=r.facing;v.canceled=r.canceled;v.ownerPresent=r.present;v.observedGrounded=r.grounded;v.observedAccepted=r.accepted;
    v.startTick=r.start;v.endTick=r.end;v.readyTick=r.ready;v.decisionCount=processed;v.lastDecisionTick=processed?processed-1:0;
    v.requestCount=r.requests;v.startCount=r.starts;v.deniedCount=r.denied;v.cancelCount=r.cancels;
    v.lastStartTick=r.startAt?r.startAt-1:0;v.lastCancelTick=r.cancelAt?r.cancelAt-1:0;v.observedRespawnTick=r.respawnAt;return v;
}
}
Bytes encodeDetCharacterControlConfig2D(const DetCharacterControl2DConfig& c){return plan(c).bytes;}
std::vector<DetTypedStateSchema> detCharacterControlStateSchemas2D(const DetCharacterControl2DConfig& c){const auto p=plan(c);return {p.input,p.state};}
std::vector<DetTypedStateSchema> detCharacterControlPolicySchemas2D(const DetCharacterControl2DConfig& c){return plan(c).policies;}
std::uint32_t detCharacterControlInputField2D(const DetCharacterControl2DConfig& c,SimEntityId owner,DetCharacterControlInputField2D f){(void)plan(c);const auto value=static_cast<std::uint32_t>(f);
    if(value<10 || value>150 || value%10)invalid();return field(ownerIndex(c,owner),value);}
std::uint32_t detCharacterControlField2D(const DetCharacterControl2DConfig& c,SimEntityId owner,DetCharacterControlField2D f){(void)plan(c);const auto value=static_cast<std::uint32_t>(f);
    if(value<10 || value>150 || value%10)invalid();return field(ownerIndex(c,owner),value);}
bool installDetCharacterControl2D(DeterministicSession& session,DetCharacterControl2DConfig c){Plan p;try{p=plan(c);if(session.sealed() || session.faulted() || session.nextTick())return false;}
    catch(const std::invalid_argument&){return false;}
    for(const auto& s:{p.input,p.state})if(!(session.registerTypedSchema(s) && session.registerGlobalState(s.id)))return false;
    for(const auto& s:p.policies)if(!(session.registerTypedSchema(s) && session.registerGlobalState(s.id)))return false;
    return session.registerLogicProfile(c.logicProfileId,1,logicHash)
        && session.registerSystem(c.systemId,c.character.collision.priority,[c,p](DetTickContext& ctx){return run(ctx,c,p);})
        && session.registerValidator(c.validatorId,1,[c,p](const DetSessionCheckpoint& cp,std::string& error){try{pure(cp,c,p);return true;}catch(const std::exception& e){error=e.what();return false;}});
}
void detCharacterControlInput2D(DetTickContext& ctx,const DetCharacterControl2DConfig& c,SimEntityId owner,DetCharacterControlInput2D input){const auto p=plan(c);const auto n=ownerIndex(c,owner);
    policyMatches(ctx,c,p);if(!ctx.systemId() || ctx.systemId()>=c.systemId || ctx.readGlobal<std::uint64_t>(c.stateSchema,DetControlProcessedTick)!=ctx.tick()
        || !validInput(input,c.character))invalid();writeRaw(ctx,c,n,input);
}
DetCharacterControlState2D detCharacterControlState2D(DetTickContext& ctx,const DetCharacterControl2DConfig& c,SimEntityId owner){const auto p=plan(c);policyMatches(ctx,c,p);
    const auto processed=ctx.readGlobal<std::uint64_t>(c.stateSchema,DetControlProcessedTick);if(processed!=ctx.tick() && processed!=stamp(ctx.tick()))invalid();
    const auto input=[&]<class T>(auto f){return ctx.readGlobal<T>(c.inputSchema,f);};const auto state=[&]<class T>(auto f){return ctx.readGlobal<T>(c.stateSchema,f);};
    validOwned(input,state,c,processed,false);return copy(readState(input,state,ownerIndex(c,owner)),processed);
}
DetCharacterControlState2D detCharacterControlState2D(const DetSessionCheckpoint& cp,const DetCharacterControl2DConfig& c,SimEntityId owner){const auto p=plan(c);pure(cp,c,p);
    return copy(readState([&]<class T>(auto f){return readDetState<T>(p.input,cp.globals.at(c.inputSchema),f);},
        [&]<class T>(auto f){return readDetState<T>(p.state,cp.globals.at(c.stateSchema),f);},ownerIndex(c,owner)),cp.nextTick);
}
} // namespace ayt::entity
