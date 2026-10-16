// design reference: AYEntity/design.md Stage53 bounded registered enemy decisions.
#include <AYEntity/DeterministicBrain2D.h>
#include <AYEntity/DeterministicCollisionQuery2D.h>
#include "detail/DetCharacterController2DIdentity.h"
#include "detail/DetCharacterRespawn2D.h"
#include "detail/DetSessionWire.h"
#include <algorithm>
#include <optional>
#include <set>
#include <stdexcept>

namespace ayt::entity {
namespace {
using D=math::DetFloat32;using V=math::DetVec2;using Bytes=std::vector<std::uint8_t>;
constexpr std::uint64_t logicHash=UINT64_C(0x4442523244503031);
std::pair<std::uint32_t,std::uint64_t> brainIdentity(const DetBrain2DConfig& c){
    return c.lineOfSight?std::pair<std::uint32_t,std::uint64_t>{kDetBrain2DSightProfileVersion,UINT64_C(0x4442523244503032)}
        :std::pair<std::uint32_t,std::uint64_t>{kDetBrain2DProfileVersion,logicHash};}
constexpr std::size_t chunkBytes=1000;
[[noreturn]] void invalid(){throw std::invalid_argument("deterministic brain 2D: invalid policy, authority, observation or input owner");}
std::uint64_t add(std::uint64_t a,std::uint64_t b){if(a>UINT64_MAX-b)invalid();return a+b;}
std::uint64_t stamp(std::uint64_t tick){return add(tick,1);}
D abs(D value){return value<D{}?-value:value;}
bool zero(V value){return value.x.bits()==0 && value.y.bits()==0;}
std::uint32_t field(std::size_t n,std::uint32_t member){return static_cast<std::uint32_t>(1000+200*n)+member;}
std::size_t ownerIndex(const DetBrain2DConfig& c,SimEntityId id){const auto found=std::lower_bound(c.owners.begin(),c.owners.end(),id,
    [](const auto& owner,auto key){return owner.actor<key;});if(found==c.owners.end() || found->actor!=id)invalid();return static_cast<std::size_t>(found-c.owners.begin());}
struct Writer {Bytes bytes;void u(std::uint64_t value,unsigned width=4){for(unsigned n=0;n<width;++n)bytes.push_back(static_cast<std::uint8_t>(value>>(8*n)));}
    void scalar(D value){u(value.bits());}void vec(V value){scalar(value.x);scalar(value.y);}
    void schema(const DetTypedStateSchema& s){u(s.id);u(s.version);u(s.fields.size());for(const auto& f:s.fields){u(f.id);u(static_cast<std::uint32_t>(detStateType(f.initial)));
        const auto lanes=encodeDetStateValue(f.initial);u(lanes.size());for(auto lane:lanes)u(lane,8);}}};
struct Plan {DetTypedStateSchema state,characterState,characterPolicy,body;std::vector<DetTypedStateSchema> policies;
    std::pair<std::uint32_t,std::uint64_t> characterIdentity,identity;Bytes bytes;};
Plan plan(const DetBrain2DConfig& c){
    if(c.owners.empty() || c.owners.size()>kDetBrain2DMaxOwners || c.stateSchema<2 || c.policySchemaBase<2
        || c.stateSchema==UINT32_MAX || c.policySchemaBase==UINT32_MAX || !c.systemId || !c.validatorId || !c.logicProfileId
        || c.systemId==UINT32_MAX || c.validatorId==UINT32_MAX || c.logicProfileId==UINT32_MAX
        || c.systemId>=c.attack.systemId || c.systemId>=c.character.preSystemId)invalid();
    Plan p;p.identity=brainIdentity(c);p.characterState=detCharacterStateSchema2D(c.character);p.characterPolicy=detCharacterPolicySchema2D(c.character);
    p.characterIdentity=detail::characterControllerIdentity2D(c.character);const auto attackBytes=encodeDetAttackConfig2D(c.attack);
    auto authority=c.attack.vitality.progress.actions;const auto actualCollision=encodeDetActionsConfig2D(authority);
    authority.collision=c.character.collision;if(encodeDetActionsConfig2D(authority)!=actualCollision)invalid();
    if(c.character.collision.characterRespawn){if(!c.attack.vitality.respawnRecovery || !c.attack.vitality.respawnFlow)invalid();
        const auto& character=c.attack.vitality.respawnFlow->respawn.character;
        Writer a,b;a.schema(p.characterState);a.schema(p.characterPolicy);b.schema(detCharacterStateSchema2D(character));b.schema(detCharacterPolicySchema2D(character));
        if(a.bytes!=b.bytes)invalid();}
    p.body=detCollisionBodySchema2D(c.character.collision);p.state={c.stateSchema,p.identity.first,{{DetBrainProcessedTick,std::uint64_t{}}}};
    Writer w;w.u(p.identity.first);if(c.lineOfSight){w.u(1);w.u(kDetCollisionQuery2DProfileVersion);}w.u(c.stateSchema);w.u(c.policySchemaBase);w.u(c.systemId);w.u(c.validatorId);w.u(c.logicProfileId);
    w.schema(p.characterState);w.schema(p.characterPolicy);w.u(attackBytes.size());w.bytes.insert(w.bytes.end(),attackBytes.begin(),attackBytes.end());w.u(c.owners.size());
    SimEntityId previous=0;const auto length=D::fromInt(1000000);
    for(std::size_t n=0;n<c.owners.size();++n){const auto& row=c.owners[n];const auto attackOwner=std::lower_bound(c.attack.owners.begin(),c.attack.owners.end(),row.actor,
            [](const auto& owner,auto id){return owner.actor<id;});
        if(!row.actor || row.actor<=previous || !row.target || row.target==row.actor || attackOwner==c.attack.owners.end() || attackOwner->actor!=row.actor
            || std::none_of(attackOwner->targets.begin(),attackOwner->targets.end(),[&](const auto& target){return target.actor==row.target;})
            || !row.patrolLeft.isFinite() || !row.patrolRight.isFinite() || !(row.patrolLeft<row.patrolRight)
            || abs(row.patrolLeft)>length || abs(row.patrolRight)>length || !row.patrolSpeed.isFinite() || !row.chaseSpeed.isFinite()
            || row.patrolSpeed<D{} || row.chaseSpeed<D{} || row.patrolSpeed>c.character.maxHorizontalSpeed || row.chaseSpeed>c.character.maxHorizontalSpeed
            || !row.senseHalfExtent.isFinite() || row.senseHalfExtent.x<D{} || row.senseHalfExtent.y<D{}
            || row.senseHalfExtent.x>length || row.senseHalfExtent.y>length || !row.attackDistance.isFinite()
            || row.attackDistance<D{} || row.attackDistance>row.senseHalfExtent.x)invalid();previous=row.actor;
        w.u(row.actor,8);w.u(row.target,8);w.scalar(row.patrolLeft);w.scalar(row.patrolRight);w.scalar(row.patrolSpeed);w.scalar(row.chaseSpeed);
        w.vec(row.senseHalfExtent);w.scalar(row.attackDistance);w.u(row.initialRight?1u:0u);
        for(std::uint32_t member=10;member<=150;member+=10){const auto f=field(n,member);
            if(member==20 || member==30)p.state.fields.push_back({f,row.initialRight});
            else if(member==70 || member==100 || member==110 || member==120)p.state.fields.push_back({f,false});
            else if(member==10 || member==80)p.state.fields.push_back({f,std::uint32_t{}});
            else if(member==60)p.state.fields.push_back({f,D{}});else if(member==130 || member==140)p.state.fields.push_back({f,V{}});
            else p.state.fields.push_back({f,std::uint64_t{}});}
    }
    p.bytes=std::move(w.bytes);if(p.bytes.size()>kDetBrain2DMaxPolicyBytes)invalid();const auto chunks=(p.bytes.size()+chunkBytes-1)/chunkBytes;
    if(!chunks || chunks>19 || c.policySchemaBase>UINT32_MAX-chunks)invalid();
    std::set<std::uint32_t> schemas,systems,validators,logic;
    const auto include=[&](const auto& rows){for(const auto& s:rows)schemas.insert(s.id);};
    const auto& vitality=c.attack.vitality;const auto& actions=vitality.progress.actions;const auto& collision=c.character.collision;
    schemas.insert(p.characterState.id);schemas.insert(p.characterPolicy.id);schemas.insert(p.body.id);schemas.insert(collision.historySchema);
    include(detAttackStateSchemas2D(c.attack));include(detAttackPolicySchemas2D(c.attack));include(detVitalityStateSchemas2D(vitality));include(detVitalityPolicySchemas2D(vitality));
    schemas.insert(detProgressStateSchema2D(vitality.progress).id);const auto progressBytes=encodeDetProgressConfig2D(vitality.progress);
    // Progress1 has no public policy factory. Reserve its existing packing from
    // actual encoded length only, without copying descriptors or policy defaults.
    for(std::size_t n=0;n<(progressBytes.size()+chunkBytes-1)/chunkBytes;++n)schemas.insert(vitality.progress.policySchemaBase+static_cast<std::uint32_t>(n));
    schemas.insert(detActionsStateSchema2D(actions).id);include(detActionsPolicySchemas2D(actions));
    systems.insert(collision.systemId);systems.insert(c.character.preSystemId);systems.insert(c.character.postSystemId);systems.insert(c.attack.systemId);systems.insert(vitality.systemId);
    validators.insert(collision.systemId);validators.insert(c.character.validatorId);validators.insert(c.attack.validatorId);validators.insert(vitality.validatorId);
    validators.insert(vitality.progress.validatorId);validators.insert(actions.validatorId);
    logic.insert(c.character.logicProfileId);logic.insert(c.attack.logicProfileId);logic.insert(vitality.logicProfileId);logic.insert(vitality.progress.logicProfileId);logic.insert(actions.logicProfileId);
    for(const auto& target:actions.targets)if(target.path){const auto& path=*target.path;schemas.insert(detPathMotionStateSchema2D(path).id);schemas.insert(detPathMotionPolicySchema2D(path).id);
        systems.insert(path.preSystemId);systems.insert(path.postSystemId);validators.insert(path.validatorId);logic.insert(path.logicProfileId);if(c.systemId>=path.preSystemId)invalid();}
    if(vitality.respawnRecovery){const auto& flow=*vitality.respawnFlow;const auto& respawn=flow.respawn;
        include(detCharacterRespawnFlowStateSchemas2D(flow));include(detCharacterRespawnFlowPolicySchemas2D(flow));
        schemas.insert(detail::respawnStateSchema(respawn).id);schemas.insert(detail::respawnPolicySchema(respawn).id);
        systems.insert(flow.systemId);systems.insert(vitality.postSystemId);validators.insert(flow.validatorId);validators.insert(respawn.validatorId);
        logic.insert(flow.logicProfileId);logic.insert(respawn.logicProfileId);if(c.systemId>=flow.systemId)invalid();}
    if(systems.contains(c.systemId) || validators.contains(c.validatorId) || logic.contains(c.logicProfileId) || !schemas.insert(c.stateSchema).second)invalid();
    for(std::size_t n=0;n<chunks;++n){const auto id=c.policySchemaBase+static_cast<std::uint32_t>(n);if(!schemas.insert(id).second)invalid();
        DetTypedStateSchema s{id,p.identity.first,{{1,static_cast<std::uint32_t>(n)},{2,static_cast<std::uint32_t>(chunks)},{3,static_cast<std::uint32_t>(p.bytes.size())}}};
        const auto begin=n*chunkBytes,end=std::min(begin+chunkBytes,p.bytes.size());for(std::size_t offset=begin;offset<end;offset+=8){std::uint64_t word=0;
            for(std::size_t byte=0;byte<8 && offset+byte<end;++byte)word|=std::uint64_t{p.bytes[offset+byte]}<<(8*byte);
            s.fields.push_back({static_cast<std::uint32_t>(4+(offset-begin)/8),word});}p.policies.push_back(std::move(s));}
    systems.insert(c.systemId);validators.insert(c.validatorId);logic.insert(c.logicProfileId);
    if(schemas.size()>detwire::maxSchemas || systems.size()>64 || validators.size()>64 || logic.size()>64)invalid();(void)detStateDefaults(p.state);return p;
}
struct State {std::uint32_t mode=0,phase=0;bool right=true,facing=true,held=false,alive=false,targetPresent=false,targetAlive=false;
    D speed{};V position{},targetPosition{};std::uint64_t count=0,at=0,observed=0,pulses=0;};
template<class Read>State readState(Read&& get,std::size_t n){State r;r.mode=get.template operator()<std::uint32_t>(field(n,10));
    r.right=get.template operator()<bool>(field(n,20));r.facing=get.template operator()<bool>(field(n,30));r.count=get.template operator()<std::uint64_t>(field(n,40));
    r.at=get.template operator()<std::uint64_t>(field(n,50));r.speed=get.template operator()<D>(field(n,60));r.held=get.template operator()<bool>(field(n,70));
    r.phase=get.template operator()<std::uint32_t>(field(n,80));r.observed=get.template operator()<std::uint64_t>(field(n,90));r.alive=get.template operator()<bool>(field(n,100));
    r.targetPresent=get.template operator()<bool>(field(n,110));r.targetAlive=get.template operator()<bool>(field(n,120));
    r.position=get.template operator()<V>(field(n,130));r.targetPosition=get.template operator()<V>(field(n,140));r.pulses=get.template operator()<std::uint64_t>(field(n,150));return r;}
V delta(const State& r){const auto difference=r.targetPosition-r.position;if(!difference.isFinite())invalid();return difference;}
bool sensed(const State& r,const DetBrainOwner2D& row){if(!r.targetAlive)return false;const auto d=delta(r);return abs(d.x)<=row.senseHalfExtent.x && abs(d.y)<=row.senseHalfExtent.y;}
void decision(State& r,const DetBrainOwner2D& row,bool present,bool sightBlocked=false){r.speed={};r.held=false;
    if(!present){r.mode=6;r.alive=false;r.position={};return;}if(!r.alive){r.mode=5;return;}
    if(r.position.x<=row.patrolLeft)r.right=true;else if(r.position.x>=row.patrolRight)r.right=false;
    if(r.phase){r.mode=4;return;}
    if(sensed(r,row) && !sightBlocked){const auto d=delta(r);if(d.x<D{})r.facing=false;else if(d.x>D{})r.facing=true;
        if(abs(d.x)<=row.attackDistance){r.mode=3;r.held=true;return;}r.mode=2;r.speed=r.facing?row.chaseSpeed:-row.chaseSpeed;return;}
    r.mode=sightBlocked?7u:1u;r.facing=r.right;r.speed=r.right?row.patrolSpeed:-row.patrolSpeed;}
template<class Read>void validOwned(Read&& get,const DetBrain2DConfig& c,std::uint64_t maximum,bool boundary){
    const auto processed=get.template operator()<std::uint64_t>(DetBrainProcessedTick);if(processed>maximum || (boundary && processed!=maximum))invalid();
    for(std::size_t n=0;n<c.owners.size();++n){const auto& row=c.owners[n];const auto r=readState(get,n);
        if(r.mode>(c.lineOfSight?7u:6u) || r.phase>3 || r.count!=processed || r.at!=processed || r.pulses>r.count || !r.position.isFinite() || !r.targetPosition.isFinite())invalid();
        if(!processed){if(r.mode || r.phase || r.speed.bits() || r.held || r.observed || r.alive || r.targetPresent || r.targetAlive || r.pulses
                || !zero(r.position) || !zero(r.targetPosition) || r.right!=row.initialRight || r.facing!=row.initialRight)invalid();continue;}
        if(r.observed!=processed-1 || (r.targetAlive && !r.targetPresent) || (!r.targetPresent && !zero(r.targetPosition)))invalid();
        if(r.mode==6){if(r.alive || !zero(r.position))invalid();}else if(!r.alive && r.mode!=5)invalid();
        if(r.mode!=6 && r.mode!=5 && !r.alive)invalid();
        const bool blocked=r.mode==7;if(blocked && (!c.lineOfSight || r.phase || !sensed(r,row)))invalid();
        // Mode is the registered old observation. A later CP cannot reproduce the
        // earlier closed segment after movement, masks or relocation have changed.
        auto expected=r;decision(expected,row,r.mode!=6,blocked);
        if(expected.mode!=r.mode || expected.right!=r.right || expected.facing!=r.facing || expected.speed.bits()!=r.speed.bits() || expected.held!=r.held)invalid();
        if(r.held && (!r.pulses || r.phase || !r.targetAlive))invalid();}
}
void descriptor(const detwire::ManifestLayout& m,const DetTypedStateSchema& schema,bool global){
    if(m.schemaVersions.at(schema.id)!=schema.version || (global && !m.globals.contains(schema.id)))invalid();const auto& actual=m.schemas.at(schema.id);
    if(actual.size()!=schema.fields.size())invalid();for(std::size_t n=0;n<actual.size();++n){const auto& f=schema.fields[n];
        if(actual[n].id!=f.id || actual[n].type!=detStateType(f.initial) || actual[n].width!=encodeDetStateValue(f.initial).size())invalid();}}
void policyMatches(DetTickContext& ctx,const Plan& p){for(const auto& s:p.policies)for(const auto& f:s.fields)
    std::visit([&]<class T>(const T& expected){if(encodeDetStateValue(ctx.readGlobal<T>(s.id,f.id))!=encodeDetStateValue(expected))invalid();},f.initial);
    for(const auto& f:p.characterPolicy.fields)std::visit([&]<class T>(const T& expected){if(encodeDetStateValue(ctx.readGlobal<T>(p.characterPolicy.id,f.id))!=encodeDetStateValue(expected))invalid();},f.initial);}
void pure(const DetSessionCheckpoint& cp,const DetBrain2DConfig& c,const Plan& p){
    for(const auto& row:c.owners)(void)detAttackState2D(cp,c.attack,row.actor);detail::validateCharacterCheckpoint2D(cp,c.character);
    const auto m=detwire::manifestLayout(cp.manifest);if(m.systems.at(c.systemId)!=c.character.collision.priority || m.validatorVersions.at(c.validatorId)!=p.identity.first
        || m.logicProfiles.at(c.logicProfileId)!=p.identity
        || m.systems.at(c.character.preSystemId)!=c.character.collision.priority || m.systems.at(c.character.postSystemId)!=c.character.collision.priority
        || m.validatorVersions.at(c.character.validatorId)!=p.characterIdentity.first || m.logicProfiles.at(c.character.logicProfileId)!=p.characterIdentity)invalid();
    descriptor(m,p.characterState,false);descriptor(m,p.characterPolicy,true);descriptor(m,p.state,true);
    const auto owned=[&](const auto& schema,bool immutable){descriptor(m,schema,true);const DetStateLayout layout(schema);const auto& words=cp.globals.at(schema.id);
        if(!layout.valid(words) || (immutable && words!=layout.defaults()))invalid();for(const auto& [id,actor]:cp.actors){(void)id;if(actor.blocks.at(schema.id)!=layout.defaults())invalid();}};
    owned(p.state,false);for(const auto& policy:p.policies)owned(policy,true);
    validOwned([&]<class T>(auto f){return readDetState<T>(p.state,cp.globals.at(c.stateSchema),f);},c,cp.nextTick,true);
    for(std::size_t n=0;n<c.owners.size();++n){const auto& row=c.owners[n];const auto r=readState([&]<class T>(auto f){return readDetState<T>(p.state,cp.globals.at(c.stateSchema),f);},n);
        const auto actor=cp.actors.find(row.actor);if(actor==cp.actors.end()){if(!cp.nextTick || !std::binary_search(cp.retiredIds.begin(),cp.retiredIds.end(),row.actor))invalid();}
        else {if(r.mode==6 || !readDetState<bool>(p.characterState,actor->second.blocks.at(p.characterState.id),DetCharacterEnabled))invalid();
            const auto& body=actor->second.blocks.at(p.body.id);if(readDetState<std::uint32_t>(p.body,body,DetBodyMode)!=2 || readDetState<bool>(p.body,body,DetBodyTrigger))invalid();
            bool reset=false;if(c.character.collision.characterRespawn){const auto result=detCharacterRespawnState2D(cp,row.actor,c.attack.vitality.respawnFlow->respawn);
                reset=result.result==DetRespawnResult2D::Accepted && result.resultTick==cp.nextTick && cp.nextTick!=0;}
            const auto& character=actor->second.blocks.at(p.characterState.id);
            if(readDetState<D>(p.characterState,character,DetCharacterTargetSpeed).bits()!=(reset?D{}.bits():r.speed.bits()))invalid();
            if(c.character.advanced && (readDetState<bool>(p.characterState,character,DetCharacterJumpHeld)
                || readDetState<bool>(p.characterState,character,DetCharacterDropHeld) || readDetState<bool>(p.characterState,character,DetCharacterCrouchHeld)))invalid();}
        const auto attack=detAttackState2D(cp,c.attack,row.actor);if(attack.input.held!=r.held || (cp.nextTick && attack.input.facingRight!=r.facing))invalid();
        if(!cp.actors.contains(row.target) && !std::binary_search(cp.retiredIds.begin(),cp.retiredIds.end(),row.target))invalid();}
}
void write(DetTickContext& ctx,const DetBrain2DConfig& c,std::size_t n,const State& r){const auto s=c.stateSchema;
    ctx.writeGlobal(s,field(n,10),r.mode);ctx.writeGlobal(s,field(n,20),r.right);ctx.writeGlobal(s,field(n,30),r.facing);ctx.writeGlobal(s,field(n,40),r.count);
    ctx.writeGlobal(s,field(n,50),r.at);ctx.writeGlobal(s,field(n,60),r.speed);ctx.writeGlobal(s,field(n,70),r.held);ctx.writeGlobal(s,field(n,80),r.phase);
    ctx.writeGlobal(s,field(n,90),r.observed);ctx.writeGlobal(s,field(n,100),r.alive);ctx.writeGlobal(s,field(n,110),r.targetPresent);ctx.writeGlobal(s,field(n,120),r.targetAlive);
    ctx.writeGlobal(s,field(n,130),r.position);ctx.writeGlobal(s,field(n,140),r.targetPosition);ctx.writeGlobal(s,field(n,150),r.pulses);}
bool run(DetTickContext& ctx,const DetBrain2DConfig& c,const Plan& p){policyMatches(ctx,p);
    if(ctx.systemId()!=c.systemId || ctx.readGlobal<std::uint64_t>(c.stateSchema,DetBrainProcessedTick)!=ctx.tick())invalid();
    validOwned([&]<class T>(auto f){return ctx.readGlobal<T>(c.stateSchema,f);},c,ctx.tick(),true);
    const auto ids=ctx.entities();std::vector<State> planned;const auto at=stamp(ctx.tick());
    std::optional<DetCollisionQuery2D> snapshot;if(c.lineOfSight)snapshot.emplace(ctx,c.character.collision);
    for(std::size_t n=0;n<c.owners.size();++n){const auto& row=c.owners[n];auto r=readState([&]<class T>(auto f){return ctx.readGlobal<T>(c.stateSchema,f);},n);
        const bool present=std::binary_search(ids.begin(),ids.end(),row.actor);const auto attack=detAttackState2D(ctx,c.attack,row.actor);
        r.phase=static_cast<std::uint32_t>(attack.phase);r.observed=ctx.readGlobal<std::uint64_t>(c.attack.stateSchema,DetAttackProcessedTick);if(r.observed!=ctx.tick())invalid();
        r.alive=present && !detVitalityState2D(ctx,c.attack.vitality,row.actor).dead;r.position={};
        if(present){const auto character=detCharacterState2D(ctx,row.actor,c.character);if(!character.enabled)invalid();const auto pose=ctx.pose(row.actor).position;r.position={pose.x,pose.y};}
        r.targetPresent=std::binary_search(ids.begin(),ids.end(),row.target);r.targetAlive=r.targetPresent && !detVitalityState2D(ctx,c.attack.vitality,row.target).dead;r.targetPosition={};
        if(r.targetPresent){const auto pose=ctx.pose(row.target).position;r.targetPosition={pose.x,pose.y};}
        bool blocked=false;if(snapshot && present && r.alive && !r.phase && sensed(r,row)){
            const auto hits=snapshot->raycast(r.position,delta(r),{ctx.read<std::uint32_t>(row.actor,p.body.id,DetBodyLayer),
                ctx.read<std::uint32_t>(row.actor,p.body.id,DetBodyMask),row.actor,false});
            blocked=std::any_of(hits.begin(),hits.end(),[&](const auto& hit){return hit.entity!=row.target;});}
        decision(r,row,present,blocked);r.count=add(r.count,1);r.at=at;if(r.held)r.pulses=add(r.pulses,1);planned.push_back(r);}
    // Every decision and finite subtraction is planned before any input is issued.
    for(std::size_t n=0;n<c.owners.size();++n){const auto& row=c.owners[n];const auto& r=planned[n];write(ctx,c,n,r);
        if(std::binary_search(ids.begin(),ids.end(),row.actor))detCharacterInput2D(ctx,row.actor,c.character,{r.speed,false,false,false});
        detSetAttackInput2D(ctx,c.attack,row.actor,{r.held,r.facing});}
    ctx.writeGlobal(c.stateSchema,DetBrainProcessedTick,at);return true;}
DetBrainState2D copy(const State& r){DetBrainState2D value;value.mode=static_cast<DetBrainMode2D>(r.mode);value.patrolRight=r.right;value.facingRight=r.facing;
    value.desiredAttackHeld=r.held;value.desiredSpeed=r.speed;value.decisionCount=r.count;value.lastDecisionTick=r.count?r.at-1:0;value.observedAttackProcessedTick=r.observed;
    value.observedAttackPhase=static_cast<DetAttackPhase2D>(r.phase);value.observedOwnerAlive=r.alive;value.observedTargetPresent=r.targetPresent;value.observedTargetAlive=r.targetAlive;
    value.observedOwnerPosition=r.position;value.observedTargetPosition=r.targetPosition;value.attackPulseCount=r.pulses;return value;}
}
Bytes encodeDetBrainConfig2D(const DetBrain2DConfig& c){return plan(c).bytes;}
DetTypedStateSchema detBrainStateSchema2D(const DetBrain2DConfig& c){return plan(c).state;}
std::vector<DetTypedStateSchema> detBrainPolicySchemas2D(const DetBrain2DConfig& c){return plan(c).policies;}
std::uint32_t detBrainField2D(const DetBrain2DConfig& c,SimEntityId owner,DetBrainField2D member){(void)plan(c);const auto value=static_cast<std::uint32_t>(member);
    if(value<10 || value>150 || value%10)invalid();return field(ownerIndex(c,owner),value);}
bool installDetBrain2D(DeterministicSession& session,DetBrain2DConfig c){Plan p;try{p=plan(c);if(session.sealed() || session.faulted() || session.nextTick())return false;}catch(const std::invalid_argument&){return false;}
    if(!(session.registerTypedSchema(p.state) && session.registerGlobalState(p.state.id)))return false;
    for(const auto& policy:p.policies)if(!(session.registerTypedSchema(policy) && session.registerGlobalState(policy.id)))return false;
    const bool installed=session.registerLogicProfile(c.logicProfileId,p.identity.first,p.identity.second)
        && session.registerSystem(c.systemId,c.character.collision.priority,[c,p](DetTickContext& ctx){return run(ctx,c,p);})
        && session.registerValidator(c.validatorId,p.identity.first,[c,p](const DetSessionCheckpoint& cp,std::string& error){try{pure(cp,c,p);return true;}catch(const std::exception& e){error=e.what();return false;}});
    if(installed) session._debugBrainConfigs2D.push_back(std::make_shared<const DetBrain2DConfig>(std::move(c)));
    return installed;}
DetBrainState2D detBrainState2D(DetTickContext& ctx,const DetBrain2DConfig& c,SimEntityId owner){const auto p=plan(c);policyMatches(ctx,p);
    (void)detAttackState2D(ctx,c.attack,owner);validOwned([&]<class T>(auto f){return ctx.readGlobal<T>(c.stateSchema,f);},c,stamp(ctx.tick()),false);
    return copy(readState([&]<class T>(auto f){return ctx.readGlobal<T>(c.stateSchema,f);},ownerIndex(c,owner)));}
DetBrainState2D detBrainState2D(const DetSessionCheckpoint& cp,const DetBrain2DConfig& c,SimEntityId owner){const auto p=plan(c);pure(cp,c,p);
    return copy(readState([&]<class T>(auto f){return readDetState<T>(p.state,cp.globals.at(c.stateSchema),f);},ownerIndex(c,owner)));}
}
