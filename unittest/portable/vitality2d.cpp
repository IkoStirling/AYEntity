// Stage47 hand integer behavior and actual Session/rollback/replay closure.
#include <AYEntity/DeterministicVitality2D.h>
#include <AYEntity/DeterministicCharacterRespawn2D.h>
#include <AYEntity/DeterministicReplay.h>
#include <AYEntity/DeterministicRollback.h>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace ayt::entity;
namespace fs=std::filesystem;
namespace {
using D=ayt::math::DetFloat32;using V=ayt::math::DetVec2;using Bytes=std::vector<std::uint8_t>;
void require(bool ok,const std::string& why){if(!ok)throw std::runtime_error(why);}
D q(int v){return D::fromInt(v)/D::fromInt(4);}
DetVitality2DConfig policy(){DetVitality2DConfig c;c.progress.stateSchema=176;c.progress.policySchemaBase=177;
    c.progress.validatorId=c.progress.logicProfileId=76;c.progress.actions.collision.extended=true;
    c.progress.actions.targets={{20,1,true,{}},{30,1,true,{}}};c.progress.counters={{1,0,20,20},{2,0,10,8}};
    c.actors={{10,1,2},{11,2,0}};
    c.permissions={{1,10,DetVitalityKind2D::Hurt,UINT64_MAX},{2,10,DetVitalityKind2D::Heal,UINT64_MAX},
        {3,11,DetVitalityKind2D::Hurt,2},{4,11,DetVitalityKind2D::Heal,3}};
    c.regions={{1,20,10,DetVitalityKind2D::Hurt,3}};return c;}
DetCharacterRespawn2DConfig respawnPolicy(const DetVitality2DConfig& c){DetCharacterRespawn2DConfig r;r.character.advanced=true;
    r.character.collision=c.progress.actions.collision;r.character.gravity={};r.spawns={{1,10,{D::fromInt(-1),D{}}}};return r;}
DetActorState actor(const DetVitality2DConfig& c,SimEntityId id){DetActorState a;DetSimTransformComponent pose;
    require(pose.setPosition({id==10?D::fromInt(-1):(id==11?D::fromInt(4):D{}),D{},D{}}),"Fixture pose");a.pose=pose.snapshot();
    const bool trigger=id>=20;a.blocks[c.progress.actions.collision.bodySchema]=detCollisionBodyState2D(c.progress.actions.collision,
        {trigger?DetBodyMode2D::Static:DetBodyMode2D::Kinematic,trigger?V{q(2),q(2)}:V{q(1),q(1)},V{},V{},trigger?2u:1u,trigger?1u:2u,trigger});
    if(c.progress.actions.collision.characterRespawn && !trigger){const auto r=respawnPolicy(c);DetCharacterState2D state;state.enabled=true;
        a.blocks[r.character.stateSchema]=detCharacterState2D(r.character,state);}return a;}
enum class Mode {Normal,Spoof,Late,ExternalProgress,Retire};
bool configure(DeterministicSession& s,const DetVitality2DConfig& c,Mode mode=Mode::Normal,bool reverse=false){
    if(!installDetActions2D(s,c.progress.actions) || !installDetProgress2D(s,c.progress) || !installDetVitality2D(s,c))return false;
    if(!s.registerSystem(4,0,[c,mode](DetTickContext& ctx){for(const auto& cmd:ctx.input().commands){if(cmd.source!=1)return false;
        if(cmd.type==1 && cmd.payload.size()==1 && cmd.payload[0]<=2){const auto ids=ctx.entities();if(std::binary_search(ids.begin(),ids.end(),SimEntityId{10}))
            ctx.write(10,c.progress.actions.collision.bodySchema,DetBodyVelocity,V{D::fromInt(int(cmd.payload[0])-1),D{}});}
        else if(cmd.type==2 && cmd.payload.size()==9){std::uint64_t amount=0;for(unsigned n=0;n<8;++n)amount|=std::uint64_t{cmd.payload[n+1]}<<(8*n);
            (void)executeDetVitality2D(ctx,c,cmd.payload[0],amount);}
        else if(cmd.type==3 && cmd.payload.empty())(void)executeDetAction2D(ctx,c.progress.actions,{20,DetActionKind2D::CollisionDisable});
        else if(cmd.type==4 && cmd.payload.empty())(void)executeDetAction2D(ctx,c.progress.actions,{30,DetActionKind2D::CollisionDisable});
        else return false;}
        if(mode==Mode::Spoof && !ctx.tick())ctx.emit(c.progress.actions.collision.eventType,encodeDetTriggerEvent2D({DetTriggerPhase2D::Enter,{10,20}}));
        if(mode==Mode::ExternalProgress && !ctx.tick())(void)executeDetProgress2D(ctx,c.progress,{DetProgressOperation2D::Add,1,0});
        if(mode==Mode::Retire && !ctx.tick())ctx.despawn(11);return true;}))return false;
    if(mode==Mode::Late && !s.registerSystem(7,0,[c](DetTickContext& ctx){(void)executeDetVitality2D(ctx,c,1,1);return true;}))return false;
    if(c.progress.actions.collision.characterRespawn){if(!installDetCharacterRespawn2D(s,respawnPolicy(c)))return false;}
    else if(!installDetCollision2D(s,c.progress.actions.collision))return false;
    std::vector<SimEntityId> ids{10,11,20,30};if(reverse)std::reverse(ids.begin(),ids.end());for(auto id:ids)if(!s.addEntity(id,actor(c,id)))return false;return s.seal();}
DetTickInput frame(std::uint64_t tick,int velocity=0){return {tick,1,{{1,0,1,{static_cast<std::uint8_t>(velocity+1)}}}};}
void request(DetTickInput& input,std::uint8_t id,std::uint64_t amount){Bytes p{id};for(unsigned n=0;n<8;++n)p.push_back(static_cast<std::uint8_t>(amount>>(8*n)));
    input.commands.push_back({1,static_cast<std::uint32_t>(input.commands.size()),2,std::move(p)});}
DetTickInput operation(std::uint64_t t,std::uint8_t id,std::uint64_t amount){auto input=frame(t);request(input,id,amount);return input;}
DetSessionCheckpoint checkpoint(DeterministicSession& s){auto cp=s.checkpoint();require(bool(cp),s.error());return *cp;}
Bytes bytes(DeterministicSession& s){return encodeDetCheckpoint(checkpoint(s));}
void advance(DeterministicSession& s,DetTickInput input){const bool ok=s.advance(std::move(input));require(ok,s.error());}
auto state(DeterministicSession& s,const DetVitality2DConfig& c,SimEntityId id=10){return detVitalityState2D(checkpoint(s),c,id);}
auto result(DeterministicSession& s,const DetVitality2DConfig& c){return detVitalityResult2D(checkpoint(s),c);}
void integers(){auto c=policy();c.regions.clear();DeterministicSession s({47,1,1,1,47});require(configure(s,c),s.error());
    advance(s,operation(0,1,7));require(state(s,c).hp==13 && state(s,c).invulnerableUntil==2 && result(s,c).actual==7,"Exact Hurt and [0,2) deadline");
    auto t=operation(1,1,3);request(t,2,UINT64_MAX);advance(s,t);require(state(s,c).hp==20 && state(s,c).hurtCount==1 && state(s,c).healCount==1
        && state(s,c).invulnerableUntil==2 && result(s,c).actual==7,"Heal saturates independently, never clears/refreshes protection");
    advance(s,operation(2,1,UINT64_MAX));require(state(s,c).dead && state(s,c).deathCount==1 && state(s,c).hp==0 && !state(s,c).invulnerableUntil
        && result(s,c).actual==20,"Exact expiry allows saturating overkill without underflow");
    advance(s,operation(3,2,20));require(result(s,c).code==DetVitalityCode2D::Dead && state(s,c).dead && state(s,c).healCount==1,"Dead Heal cannot revive");
    advance(s,operation(4,3,3));require(result(s,c).code==DetVitalityCode2D::AmountExceeded && state(s,c,11).hp==8,"Immutable direct amount authority");
    advance(s,operation(5,3,0));require(result(s,c).code==DetVitalityCode2D::Unchanged && state(s,c,11).hp==8,"Zero alive amount has no numerical operation");
    advance(s,operation(6,99,7));require(result(s,c).code==DetVitalityCode2D::UnknownPermission && result(s,c).actor==0,"Unknown permission is a recorded normal result");
    advance(s,operation(7,0,0));require(result(s,c).code==DetVitalityCode2D::UnknownPermission,"Zero permission is unknown, not malformed");
    advance(s,operation(8,4,UINT64_MAX));require(result(s,c).code==DetVitalityCode2D::AmountExceeded && state(s,c,11).hp==8,"Heal also respects fixed permission amount");
    c.progress.counters[0].maximum=UINT64_MAX;c.progress.counters[0].initial=UINT64_MAX-1;c.actors[0].invulnerabilityTicks=0;
    DeterministicSession edge({47,1,1,1,47});require(configure(edge,c),edge.error());advance(edge,operation(0,2,UINT64_MAX));
    require(state(edge,c).hp==UINT64_MAX && result(edge,c).actual==1,"Heal reaches UInt64 maximum with bounded difference, not overflowing addition");
    advance(edge,operation(1,1,UINT64_MAX));require(state(edge,c).hp==0 && state(edge,c).deathCount==1 && result(edge,c).actual==UINT64_MAX,"Hurt exact UInt64 maximum reaches0 without underflow");
    std::cout<<"PASS hand UInt64 saturation, deadline expiry, dead/unknown/amount/zero behavior\n";}
void windowsAndBudget(){for(unsigned n=0;n<3;++n){auto c=policy();c.regions.clear();c.actors[0].invulnerabilityTicks=n;DeterministicSession s({47,1,1,1,47});require(configure(s,c),s.error());
        auto t=operation(0,1,2);request(t,1,2);advance(s,t);require(state(s,c).hp==(n?18u:16u),"N0 permits two same-tick Hurt; N1/N2 suppress second");
        advance(s,operation(1,1,2));require(state(s,c).hp==(n==2?18u:(n==1?16u:14u)),"N1 expires next tick, N2 still protects");
        advance(s,operation(2,1,2));require(state(s,c).hp==(n==2?16u:(n==1?14u:12u)),"Exact expiry all cases");}
    auto c=policy();c.regions.clear();c.maxOperationsPerTick=1;c.actors[0].invulnerabilityTicks=0;DeterministicSession budget({47,1,1,1,47});require(configure(budget,c),budget.error());
    auto t=operation(0,1,2);request(t,1,3);advance(budget,t);require(state(budget,c).hp==18 && result(budget,c).code==DetVitalityCode2D::BudgetExceeded
        && state(budget,c).attemptCount==2 && state(budget,c).hurtCount==1,"Normal budget denial retains numerical value and records both attempts");
    advance(budget,operation(1,1,3));require(state(budget,c).hp==15,"Next logical tick has a fresh budget");
    std::cout<<"PASS N0/N1/N2 same-tick/expiry and bounded operation opportunities\n";}
void enters(){auto c=policy();DeterministicSession s({47,1,1,1,47});require(configure(s,c),s.error());advance(s,frame(0,1));
    require(state(s,c).hp==20,"Real Enter not applied before next-tick delivery");advance(s,frame(1));require(state(s,c).hp==17 && result(s,c).region==1 && result(s,c).tick==1,"Real producer Enter applies one fixed region");
    advance(s,frame(2));require(state(s,c).attemptCount==1,"Stay does not retry or apply periodic damage");
    c.regions.push_back({2,30,10,DetVitalityKind2D::Heal,2});DeterministicSession order({47,1,1,1,47});require(configure(order,c),order.error());advance(order,frame(0,1));advance(order,frame(1));
    require(state(order,c).hp==19 && state(order,c).hurtCount==1 && state(order,c).healCount==1 && result(order,c).region==2,"Canonical pair then region order: Hurt3 before Heal2");
    c=policy();DeterministicSession spoof({47,1,1,1,47});require(configure(spoof,c,Mode::Spoof),spoof.error());advance(spoof,frame(0));advance(spoof,frame(1));
    require(state(spoof,c).hp==20 && !state(spoof,c).attemptCount,"Same event type/payload from wrong producer has no authority");
    DeterministicSession inactive({47,1,1,1,47});require(configure(inactive,c),inactive.error());advance(inactive,frame(0,1));auto disable=frame(1);disable.commands.push_back({1,1,3,{}});advance(inactive,disable);
    require(result(inactive,c).code==DetVitalityCode2D::InactiveRegion && state(inactive,c).hp==20,"An earlier Action disables participation before delivered Enter");
    DeterministicSession retired({47,1,1,1,47});require(configure(retired,c,Mode::Retire),retired.error());advance(retired,frame(0));advance(retired,operation(1,3,1));
    require(result(retired,c).code==DetVitalityCode2D::MissingActor && state(retired,c,11).hp==8,"Retired configured actor is a normal missing result");
    c.actors[0].invulnerabilityTicks=0;DeterministicSession transient({47,1,1,1,47});require(configure(transient,c),transient.error());
    auto cross=frame(0,1);cross.commands[0].payload[0]=2;advance(transient,cross);advance(transient,frame(1,1));advance(transient,frame(2));
    require(state(transient,c).hurtCount==1,"Enter+Exit transient or exit delivery does not create a second Hurt");
    std::cout<<"PASS actual Enter/Stay, stable region order, producer, disabled and retired participation\n";}
void faults(){auto c=policy();c.regions.clear();for(auto mode:{Mode::Late,Mode::ExternalProgress}){DeterministicSession s({47,1,1,1,47});require(configure(s,c,mode),s.error());
        const auto initial=checkpoint(s);const auto before=encodeDetCheckpoint(initial);require(!s.advance(frame(0)) && s.faulted(),"Late or outside dedicated Progress authority faults");require(s.restore(initial) && bytes(s)==before,"Explicit restore after fault retains full initial closure");}
    // Malformed matching payload is also rejected atomically by restore. Its real
    // source/type are preserved; no synthetic alternate producer is treated as real.
    DeterministicSession s({47,1,1,1,47});require(configure(s,c),s.error());auto bad=checkpoint(s);const auto before=bytes(s);
    bad.pendingEvents={{c.progress.actions.collision.systemId,0,c.progress.actions.collision.eventType,{1}}};require(!s.restore(bad) && bytes(s)==before,"Malformed matching collision payload atomic restore rejection");
    std::cout<<"PASS authority timing, dedicated Progress ownership and malformed event fault closure\n";}
void action2(){auto c=policy();c.regions.clear();auto& k=c.progress.actions.collision;k.oneWayPlatforms=true;k.characterRespawn=true;
    DeterministicSession s({47,1,1,64,47});require(configure(s,c),s.error());auto t=operation(0,1,3);t.commands.push_back({1,2,4,{}});advance(s,t);
    const auto cp=checkpoint(s);require(state(s,c).hp==17 && readDetState<std::uint64_t>(detActionsStateSchema2D(c.progress.actions),cp.globals.at(c.progress.actions.stateSchema),DetActionsTotalCount)==1,
        "Dedicated HP reuses installed complete Actions2 with nonempty independent authority");std::cout<<"PASS Actions2 respawn-pipeline identity without automatic health respawn\n";}
void closure(const fs::path& output){const auto c=policy();std::vector<DetTickInput> inputs;for(unsigned t=0;t<8;++t)inputs.push_back(frame(t,t?0:1));
    inputs[2]=operation(2,2,1);inputs[3]=operation(3,1,2);inputs[5]=operation(5,1,UINT64_MAX);inputs[6]=operation(6,2,1);
    DeterministicSession base({47,1,1,1,47});require(configure(base,c),base.error());std::vector<DetSessionCheckpoint> cps{checkpoint(base)};std::vector<Bytes> states{bytes(base)};
    std::vector<DetConfirmedEvent> events;for(const auto& input:inputs){advance(base,input);cps.push_back(checkpoint(base));states.push_back(bytes(base));for(const auto& e:cps.back().pendingEvents)events.push_back({1,input.tick,e});}
    DeterministicSession restored({47,1,1,1,47});require(configure(restored,c,Mode::Normal,true) && restored.restore(cps[1]),restored.error());
    for(unsigned t=1;t<8;++t){advance(restored,inputs[t]);require(bytes(restored)==states[t+1],"Pending real Enter restore has identical entire CP continuation");}
    const auto before=bytes(restored);const auto schema=detVitalityStateSchema2D(c);const auto hpField=detVitalityActorField2D(c,10,DetVitalityHP);
    for(unsigned n=0;n<12;++n){auto bad=cps[2];switch(n){case 0:writeDetState(schema,bad.globals.at(c.stateSchema),hpField,std::uint64_t{18});break;
        case 1:writeDetState(schema,bad.globals.at(c.stateSchema),DetVitalityOwnedProgressCount,std::uint64_t{0});break;
        case 2:writeDetState(schema,bad.globals.at(c.stateSchema),DetVitalityProcessedTick,std::uint64_t{1});break;
        case 3:writeDetState(schema,bad.globals.at(c.stateSchema),detVitalityActorField2D(c,10,DetVitalityUntil),std::uint64_t{4});break;
        case 4:writeDetState(schema,bad.globals.at(c.stateSchema),detVitalityActorField2D(c,10,DetVitalityDeaths),std::uint64_t{1});break;
        case 5:bad.globals.at(c.policySchemaBase).at(3)^=1;break;case 6:bad.actors.at(10).blocks.at(c.policySchemaBase).at(3)^=1;break;
        case 7:writeDetState(schema,bad.actors.at(10).blocks.at(c.stateSchema),hpField,std::uint64_t{19});break;
        case 8:writeDetState(schema,bad.globals.at(c.stateSchema),DetVitalityWriteAmount,std::uint64_t{2});break;
        case 9:writeDetState(schema,bad.globals.at(c.stateSchema),DetVitalityUnknownCount,std::uint64_t{1});break;
        case 10:bad.pendingEvents.push_back({c.progress.actions.collision.systemId,999,c.progress.actions.collision.eventType,{1}});break;
        case 11:bad.globals.at(c.progress.actions.policySchemaBase).at(3)^=1;break;}
        require(!restored.restore(bad) && bytes(restored)==before,"Forged mirror/count/time/death/policy/default/upstream restore rejected atomically case="+std::to_string(n));}
    auto changed=c;changed.actors[0].invulnerabilityTicks=3;DeterministicSession foreign({47,1,1,1,47});require(configure(foreign,changed),foreign.error());
    const auto foreignBefore=bytes(foreign);require(foreign.manifest()!=base.manifest() && !foreign.restore(cps[1]) && bytes(foreign)==foreignBefore,"Full policy bytes bind protection beyond content hash");
    DeterministicSession corrected({47,1,1,1,47});require(configure(corrected,c),corrected.error());DetRollbackConfig rc;rc.maxPredictionTicks=4;rc.prediction={{1,DetPredictionMode::Hold},{2,DetPredictionMode::Omit}};
    DeterministicRollback rollback(corrected,rc);for(unsigned t=0;t<8;++t){if(t==2){require(rollback.advance(),rollback.error());require(rollback.submit(1,inputs[t]),rollback.error());}
        else {require(rollback.submit(1,inputs[t]),rollback.error());require(rollback.advance(),rollback.error());}require(bytes(corrected)==states[t+1],"Actual late input correction entire vitality CP");}
    require(rollback.rollbackCount()>0 && rollback.replayedTicks()>0,"Late input actually replayed");require(rollback.takeConfirmedEvents()==events && rollback.takeConfirmedEvents().empty(),"Real collision confirmed events exactly once");
    DeterministicSession recording({47,1,1,1,47});require(configure(recording,c),recording.error());DetReplayWriter writer;const bool began=writer.begin(recording,(output/"vitality.rpl").string(),2);require(began,writer.error());
    for(unsigned t=0;t<8;++t){const bool ok=writer.advance(inputs[t]);require(ok,writer.error());require(bytes(recording)==states[t+1],"Real writer entire CP");}const bool finished=writer.finish();require(finished,writer.error());
    DeterministicSession playback({47,1,1,1,47});require(configure(playback,c),playback.error());DetReplayReader reader;const bool opened=reader.open(writer.path());require(opened,reader.error());require(reader.restoreInitial(playback),reader.error());
    while(!reader.atEnd()){require(reader.advance(playback),reader.error());require(bytes(playback)==states[playback.nextTick()],"Real playback entire CP");}
    require(reader.seek(playback,1) && bytes(playback)==states[1],"Seek pending actual Enter");require(reader.seek(playback,8) && bytes(playback)==states[8],"Seek dead state without revive");
    std::ofstream out(output/"final.state",std::ios::binary);out.write(reinterpret_cast<const char*>(states.back().data()),static_cast<std::streamsize>(states.back().size()));out.close();require(bool(out),"Write actual final state");
    std::cout<<"PASS8tick/9CP actual restore, late rollback/drain and sealed .rpl/2seek byte equivalence\n";}
void limits(){auto c=policy();for(unsigned n=0;n<11;++n){auto bad=c;switch(n){case 0:bad.actors[1].counter=1;break;case 1:bad.actors[0].invulnerabilityTicks=65536;break;
        case 2:bad.progress.counters[0].minimum=1;break;case 3:bad.progress.objectives={{1,{{1,1}}}};break;case 4:bad.progress.claims={{1,20,10,1,1}};break;
        case 5:bad.regions[0].amount=0;break;case 6:bad.permissions[0].kind=DetVitalityKind2D::None;break;case 7:bad.stateSchema=bad.progress.stateSchema;break;
        case 8:bad.systemId=bad.progress.actions.collision.systemId;break;case 9:bad.permissions.push_back(bad.permissions.back());break;case 10:bad.maxOperationsPerTick=65;break;}
        DeterministicSession s({47,1,1,1,47});require(!installDetVitality2D(s,bad) && !s.faulted(),"Malformed config rejected before destination registration case="+std::to_string(n));}
    c.actors[1].actor=999;for(auto& p:c.permissions)if(p.actor==11)p.actor=999;DeterministicSession unknown({47,1,1,1,47});require(!configure(unknown,c),"Never-issued configured identity refuses seal");
    c=policy();c.actors.clear();c.progress.counters.clear();c.permissions.clear();c.regions.clear();for(unsigned n=1;n<=8;++n){c.actors.push_back({n,n,65535});c.progress.counters.push_back({n,0,UINT64_MAX,UINT64_MAX});}
    for(unsigned n=1;n<=16;++n){c.permissions.push_back({n,1,DetVitalityKind2D::Hurt,UINT64_MAX});c.regions.push_back({n,20,1,DetVitalityKind2D::Heal,1});}
    require(detVitalityStateSchema2D(c).fields.size()==84 && encodeDetVitalityConfig2D(c).size()<=4096,"8actor/16permission/16region state84 within128, full policy bounded");
    c.actors.push_back({9,9,0});c.progress.counters.push_back({9,0,1,1});DeterministicSession extra({47,1,1,1,47});require(!installDetVitality2D(extra,c),"9th actor bounded rejection");
    std::cout<<"PASS bounded descriptors, references and immutable namespace/authority policy\n";}
DetVitality2DConfig recoveryPolicy(std::uint64_t initial=5,bool manual=true){auto c=policy();c.actors={{10,1,2}};c.permissions.resize(2);c.regions.clear();
    c.progress.counters={{1,0,5,initial}};c.progress.actions.targets.clear();auto& collision=c.progress.actions.collision;
    collision.oneWayPlatforms=true;collision.characterRespawn=true;
    c.respawnRecovery=true;DetCharacterRespawnFlow2DConfig flow;flow.respawn=respawnPolicy(c);flow.respawn.spawns={{1,10,{D::fromInt(-2),D{}}},{2,10,{D::fromInt(-3),D{}}}};
    flow.externalDeaths=true;flow.externalDeathPermissions={{1,10,c.systemId}};flow.bindings={{10,1}};c.respawnFlow=flow;c.recoveries={{10,1,5,manual}};return c;}
enum class RecoveryMode {Normal,Occupied,Disabled,LateProgress};
bool configureRecovery(DeterministicSession& s,const DetVitality2DConfig& c,RecoveryMode mode=RecoveryMode::Normal,bool reverse=false){
    if(!installDetActions2D(s,c.progress.actions) || !installDetProgress2D(s,c.progress)
        || !installDetCharacterRespawn2D(s,c.respawnFlow->respawn) || !installDetCharacterRespawnFlow2D(s,*c.respawnFlow)
        || !installDetVitality2D(s,c))return false;
    if(!s.registerSystem(4,0,[c](DetTickContext& ctx){for(const auto& cmd:ctx.input().commands){if(cmd.source!=1)return false;
        if(cmd.type==1 && cmd.payload==Bytes{1})continue; // no movement input in the hand HP fixture
        if(cmd.type==2 && cmd.payload.size()==9){std::uint64_t amount=0;for(unsigned n=0;n<8;++n)amount|=std::uint64_t{cmd.payload[n+1]}<<(8*n);
            (void)executeDetVitality2D(ctx,c,cmd.payload[0],amount);}
        else if(cmd.type==3 && cmd.payload.empty())detCharacterRespawnFlowInput2D(ctx,10,*c.respawnFlow,true);
        else if(cmd.type==4 && cmd.payload.empty())(void)detCharacterRespawnRequest2D(ctx,10,c.respawnFlow->respawn,1);
        else if(cmd.type==5 && cmd.payload.empty())detCharacterRespawnFlowDeath2D(ctx,*c.respawnFlow,1);
        else return false;}return true;}))return false;
    if(mode==RecoveryMode::LateProgress && !s.registerSystem(63,0,[c](DetTickContext& ctx){(void)executeDetProgress2D(ctx,c.progress,{DetProgressOperation2D::Add,1,0});return true;}))return false;
    std::vector<std::pair<SimEntityId,DetActorState>> actors;auto hero=actor(c,10);DetSimTransformComponent pose;require(pose.setPosition({D{},D{},D{}}),"Recovery pose");hero.pose=pose.snapshot();
    auto state=DetCharacterState2D{};state.enabled=mode!=RecoveryMode::Disabled;hero.blocks[c.respawnFlow->respawn.character.stateSchema]=detCharacterState2D(c.respawnFlow->respawn.character,state);
    actors.emplace_back(10,std::move(hero));if(!c.regions.empty() || !c.respawnFlow->regions.empty())actors.emplace_back(20,actor(c,20));
    if(mode==RecoveryMode::Occupied){DetActorState wall;require(pose.setPosition({D::fromInt(-2),D{},D{}}),"Occupied spawn pose");wall.pose=pose.snapshot();
        wall.blocks[c.progress.actions.collision.bodySchema]=detCollisionBodyState2D(c.progress.actions.collision,{DetBodyMode2D::Static,{q(1),q(1)},{},{},2,1,false});actors.emplace_back(40,std::move(wall));}
    if(reverse)std::reverse(actors.begin(),actors.end());for(auto& [id,a]:actors)if(!s.addEntity(id,std::move(a)))return false;return s.seal();}
void manual(DetTickInput& input){input.commands.push_back({1,static_cast<std::uint32_t>(input.commands.size()),3,{}});}
auto recovered(DeterministicSession& s,const DetVitality2DConfig& c){return detVitalityRecoveryState2D(checkpoint(s),c,10);}
void recoveryIntegers(){auto c=recoveryPolicy();DeterministicSession s({48,1,1,64,48});require(configureRecovery(s,c),s.error());
    auto lethal=operation(0,1,5);manual(lethal);advance(s,lethal);auto r=recovered(s,c);const auto flow=detCharacterRespawnFlowState2D(checkpoint(s),10,*c.respawnFlow);
    require(state(s,c).hp==5 && r.restoreCount==1 && r.reviveCount==1 && r.restoreWriteCount==1 && r.lastRestoreKind==DetRespawnFlowRequestKind2D::ExternalDeath
        && flow.externalDeathCount==1 && !flow.manualCount && result(s,c).hpAfter==0,"Actual source6 external death beats manual; only Accepted post revives; ordinary result remains historical");
    advance(s,operation(1,1,2));require(state(s,c).hp==3 && state(s,c).invulnerableUntil==3,"Recovery clears prior protection and later successful Hurt has its own exact deadline");
    auto protectedManual=operation(2,1,1);manual(protectedManual);advance(s,protectedManual);r=recovered(s,c);
    require(result(s,c).code==DetVitalityCode2D::Invulnerable && result(s,c).hpAfter==3 && state(s,c).hp==5 && !state(s,c).invulnerableUntil
        && r.restoreCount==2 && r.reviveCount==1 && r.restoreWriteCount==2,"Historical Invulnerable is validated while a fresh accepted manual resets HP/protection");
    const auto seq=detProgressResult2D(checkpoint(s),c.progress).sequence;auto equal=frame(3);manual(equal);advance(s,equal);r=recovered(s,c);
    require(r.restoreCount==3 && r.reviveCount==1 && r.restoreWriteCount==2 && detProgressResult2D(checkpoint(s),c.progress).sequence==seq,"Equal-HP Accepted counts without a Progress write");
    advance(s,operation(4,1,5));r=recovered(s,c);require(state(s,c).hp==5 && state(s,c).deathCount==2 && r.restoreCount==4 && r.reviveCount==2,"Repeated lethal/accepted cycles preserve exact death/revive closure");
    auto blocked=recoveryPolicy();DeterministicSession occupied({48,1,1,64,48});require(configureRecovery(occupied,blocked,RecoveryMode::Occupied),occupied.error());advance(occupied,operation(0,1,5));
    require(state(occupied,blocked).dead && !recovered(occupied,blocked).restoreCount && detCharacterRespawnState2D(checkpoint(occupied),10,blocked.respawnFlow->respawn).result==DetRespawnResult2D::Occupied,"Occupied does not restore HP");
    advance(occupied,frame(1));require(state(occupied,blocked).dead && detCharacterRespawnFlowState2D(checkpoint(occupied),10,*blocked.respawnFlow).requestCount==1,"Persistent HP0 is not an implicit death retry");
    DeterministicSession disabled({48,1,1,64,48});require(configureRecovery(disabled,blocked,RecoveryMode::Disabled),disabled.error());advance(disabled,operation(0,1,5));
    require(state(disabled,blocked).dead && !recovered(disabled,blocked).restoreCount && detCharacterRespawnFlowState2D(checkpoint(disabled),10,*blocked.respawnFlow).lastRequestCode==DetRespawnRequestCode2D::Unavailable,"Unavailable request does not recover");
    DeterministicSession prior({48,1,1,64,48});require(configureRecovery(prior,c),prior.error());auto early=frame(0);early.commands.push_back({1,1,4,{}});request(early,1,5);advance(prior,early);
    require(state(prior,c).dead && !recovered(prior,c).restoreCount && detCharacterRespawnState2D(checkpoint(prior),10,c.respawnFlow->respawn).result==DetRespawnResult2D::Accepted
        && detCharacterRespawnFlowState2D(checkpoint(prior),10,*c.respawnFlow).lastRequestCode==DetRespawnRequestCode2D::AlreadyRequested,"Foreign early accepted request with Flow AlreadyRequested cannot restore HP");
    for(bool manualAllowed:{false,true}){auto initialDead=recoveryPolicy(0,manualAllowed);DeterministicSession dead({48,1,1,64,48});require(configureRecovery(dead,initialDead),dead.error());advance(dead,frame(0));
        require(state(dead,initialDead).dead && !detCharacterRespawnFlowState2D(checkpoint(dead),10,*initialDead.respawnFlow).requestCount,"Initial HP0 has no automatic request");
        auto m=frame(1);manual(m);advance(dead,m);require(state(dead,initialDead).hp==(manualAllowed?5u:0u) && !state(dead,initialDead).deathCount
            && recovered(dead,initialDead).reviveCount==(manualAllowed?1u:0u),"Initial dead cycle and explicit false/true manual recovery permission");}
    auto region=recoveryPolicy();region.regions={{1,20,10,DetVitalityKind2D::Hurt,5}};region.respawnFlow->regions={{1,20,10,DetRespawnFlowRegionKind2D::Checkpoint,2}};
    DeterministicSession real({48,1,1,64,48});require(configureRecovery(real,region),real.error());advance(real,frame(0));require(state(real,region).hp==5,"Region damage waits for actual Enter delivery");advance(real,frame(1));
    require(recovered(real,region).lastRestoreSpawn==2 && recovered(real,region).reviveCount==1 && detCharacterRespawnFlowState2D(checkpoint(real),10,*region.respawnFlow).checkpointCount==1,"Real Enter selects checkpoint before external death and fresh accepted recovery");
    region.respawnFlow->regions={{1,20,10,DetRespawnFlowRegionKind2D::Death,0}};DeterministicSession regionWins({48,1,1,64,48});require(configureRecovery(regionWins,region),regionWins.error());advance(regionWins,frame(0));advance(regionWins,frame(1));
    require(recovered(regionWins,region).lastRestoreKind==DetRespawnFlowRequestKind2D::Death && !detCharacterRespawnFlowState2D(checkpoint(regionWins),10,*region.respawnFlow).externalDeathCount,"Real region Death wins over same-tick external flag and still recovers");
    // Fresh manual normalization can subtract HP and counts numerical provenance.
    auto lower=recoveryPolicy();lower.recoveries[0].restoredHP=3;DeterministicSession sub({48,1,1,64,48});require(configureRecovery(sub,lower),sub.error());auto m=frame(0);manual(m);advance(sub,m);
    require(state(sub,lower).hp==3 && recovered(sub,lower).restoreWriteCount==1 && !recovered(sub,lower).reviveCount && detProgressResult2D(checkpoint(sub),lower.progress).operation==DetProgressOperation2D::Subtract,"Authored lower recovery value uses exact Subtract rather than an overflowing difference");
    std::cout<<"PASS recovery2 actual accepted/denied/manual/initial0/checkpoint/region arbitration and integer cycle\n";}
void recoveryClosure(const fs::path& output){auto c=recoveryPolicy();c.regions={{1,20,10,DetVitalityKind2D::Hurt,1}};
    std::vector<DetTickInput> inputs;for(unsigned t=0;t<8;++t)inputs.push_back(frame(t));inputs[0]=operation(0,1,5);manual(inputs[0]);
    inputs[1]=operation(1,1,2);inputs[2]=operation(2,1,1);manual(inputs[2]);manual(inputs[3]);inputs[4]=operation(4,1,5);manual(inputs[6]);inputs[7]=operation(7,1,1);
    DeterministicSession base({48,1,1,64,48});require(configureRecovery(base,c),base.error());std::vector<DetSessionCheckpoint> cps{checkpoint(base)};std::vector<Bytes> states{bytes(base)};
    std::vector<DetConfirmedEvent> events;for(const auto& input:inputs){advance(base,input);cps.push_back(checkpoint(base));states.push_back(bytes(base));for(const auto& e:cps.back().pendingEvents)events.push_back({1,input.tick,e});}
    require(!events.empty() && recovered(base,c).restoreCount==5 && recovered(base,c).reviveCount==2 && state(base,c).hp==4,"Hand eight-tick recovery counts and actual trigger events");
    DeterministicSession restored({48,1,1,64,48});require(configureRecovery(restored,c,RecoveryMode::Normal,true) && restored.restore(cps[1]),restored.error());
    for(unsigned t=1;t<8;++t){advance(restored,inputs[t]);require(bytes(restored)==states[t+1],"Recovery CP restores pending real collision events and all recovery/numeric provenance");}
    const auto before=bytes(restored);const auto schemas=detVitalityStateSchemas2D(c);const auto& primary=schemas[0];const auto& recovery=schemas[1];
    for(unsigned n=0;n<14;++n){auto bad=cps[3];const auto field=[&](auto f){return detVitalityRecoveryField2D(c,10,f);};switch(n){
        case 0:writeDetState(recovery,bad.globals.at(c.recoveryStateSchema),field(DetVitalityLethalPending),true);break;
        case 1:writeDetState(recovery,bad.globals.at(c.recoveryStateSchema),DetVitalityPostProcessedTick,std::uint64_t{2});break;
        case 2:writeDetState(recovery,bad.globals.at(c.recoveryStateSchema),field(DetVitalityRevives),std::uint64_t{2});break;
        case 3:writeDetState(recovery,bad.globals.at(c.recoveryStateSchema),field(DetVitalityLastAttemptHP),std::uint64_t{4});break;
        case 4:writeDetState(recovery,bad.globals.at(c.recoveryStateSchema),field(DetVitalityLastRestoreSpawn),std::uint32_t{99});break;
        case 5:writeDetState(recovery,bad.globals.at(c.recoveryStateSchema),field(DetVitalityLastNumericRestoreSequence),std::uint64_t{1});break;
        case 6:writeDetState(recovery,bad.globals.at(c.recoveryStateSchema),field(DetVitalityLastNumericRestoreBefore),std::uint64_t{5});break;
        case 7:writeDetState(recovery,bad.globals.at(c.recoveryStateSchema),DetVitalityLastResultProtectionUntil,std::uint64_t{4});break;
        case 8:writeDetState(recovery,bad.actors.at(10).blocks.at(c.recoveryStateSchema),field(DetVitalityRestores),std::uint64_t{1});break;
        case 9:writeDetState(primary,bad.globals.at(c.stateSchema),DetVitalityOwnedProgressCount,std::uint64_t{1});break;
        case 10:writeDetState(primary,bad.globals.at(c.stateSchema),detVitalityActorField2D(c,10,DetVitalityUntil),std::uint64_t{3});break;
        case 11:writeDetState(recovery,bad.globals.at(c.recoveryStateSchema),field(DetVitalityRestores),std::uint64_t{4});break;
        case 12:bad.globals.at(c.policySchemaBase).at(3)^=1;break;
        case 13:writeDetState(recovery,bad.globals.at(c.recoveryStateSchema),field(DetVitalityLastRestoreKind),std::uint32_t{0});break;}
        require(!restored.restore(bad) && bytes(restored)==before,"Recovery closure rejects forged CP atomically case="+std::to_string(n));}
    auto foreign=c;foreign.recoveries[0].recoverManual=false;DeterministicSession other({48,1,1,64,48});require(configureRecovery(other,foreign),other.error());const auto otherBefore=bytes(other);
    require(other.manifest()!=base.manifest() && !other.restore(cps[1]) && bytes(other)==otherBefore,"Literal manual recovery policy changes identity and incompatible restore is atomic");
    DeterministicSession corrected({48,1,1,64,48});require(configureRecovery(corrected,c),corrected.error());DetRollbackConfig rc;rc.maxPredictionTicks=4;rc.prediction={{1,DetPredictionMode::Hold},{2,DetPredictionMode::Omit}};
    DeterministicRollback rollback(corrected,rc);for(unsigned t=0;t<8;++t){if(t==4){require(rollback.advance(),rollback.error());require(rollback.submit(1,inputs[t]),rollback.error());}
        else {require(rollback.submit(1,inputs[t]),rollback.error());require(rollback.advance(),rollback.error());}require(bytes(corrected)==states[t+1],"Actual late lethal input correction retains complete recovery2 CP");}
    require(rollback.rollbackCount()>0 && rollback.replayedTicks()>0 && rollback.takeConfirmedEvents()==events && rollback.takeConfirmedEvents().empty(),"Recovery rollback has real replay and exactly-once real confirmed collision events");
    DeterministicSession recording({48,1,1,64,48});require(configureRecovery(recording,c),recording.error());DetReplayWriter writer;const bool began=writer.begin(recording,(output/"vitality-recovery.rpl").string(),2);require(began,writer.error());
    for(unsigned t=0;t<8;++t){const bool ok=writer.advance(inputs[t]);require(ok,writer.error());require(bytes(recording)==states[t+1],"Recovery actual writer entire CP");}const bool finished=writer.finish();require(finished,writer.error());
    DeterministicSession playback({48,1,1,64,48});require(configureRecovery(playback,c),playback.error());DetReplayReader reader;const bool opened=reader.open(writer.path());require(opened,reader.error());require(reader.restoreInitial(playback),reader.error());
    while(!reader.atEnd()){require(reader.advance(playback),reader.error());require(bytes(playback)==states[playback.nextTick()],"Recovery real playback entire CP");}
    require(reader.seek(playback,3) && bytes(playback)==states[3],"Seek historical denial then fresh recovery");require(reader.seek(playback,8) && bytes(playback)==states[8],"Seek complete multi-cycle recovery closure");
    std::ofstream out(output/"recovery-final.state",std::ios::binary);out.write(reinterpret_cast<const char*>(states.back().data()),static_cast<std::streamsize>(states.back().size()));out.close();require(bool(out),"Write actual recovery final CP");
    std::cout<<"PASS recovery2 8ticks/9CP/14negative restores, late lethal rollback/drain and actual sealed .rpl/2seek\n";
}
void recoveryFaultsAndLimits(){auto c=recoveryPolicy();for(auto mode:{RecoveryMode::Normal,RecoveryMode::LateProgress}){DeterministicSession s({48,1,1,64,48});require(configureRecovery(s,c,mode),s.error());
        const auto initial=checkpoint(s);const auto before=bytes(s);auto input=frame(0);if(mode==RecoveryMode::Normal)input.commands.push_back({1,1,5,{}});
        require(!s.advance(input) && s.faulted(),"Wrong actual producer or outside post HP authority faults");require(s.restore(initial) && bytes(s)==before,"Explicit restore restores failed producer/post closure");}
    for(unsigned n=0;n<9;++n){auto bad=c;switch(n){case 0:bad.recoveries[0].restoredHP=0;break;case 1:bad.recoveries[0].restoredHP=6;break;
        case 2:bad.recoveries[0].externalDeathPermission=99;break;case 3:bad.respawnFlow->externalDeathPermissions[0].producerSystem=4;break;
        case 4:bad.postSystemId=bad.respawnFlow->respawn.character.postSystemId;break;case 5:bad.recoveryStateSchema=bad.respawnFlow->externalStateSchema;break;
        case 6:bad.respawnFlow->externalDeaths=false;break;case 7:bad.recoveries.clear();break;case 8:bad.respawnFlow->respawn.character.collision.eventType+=1;break;}
        DeterministicSession s({48,1,1,64,48});require(!installDetVitality2D(s,bad) && !s.faulted(),"Malformed recovery authority rejected before destination registration case="+std::to_string(n));}
    auto legacy=policy();const auto oldBytes=encodeDetVitalityConfig2D(legacy);const auto oldSchema=detVitalityStateSchemas2D(legacy);require(oldSchema.size()==1 && oldSchema[0].version==1,"Default uses only exact old primary1 descriptor");
    auto explicitFalse=legacy;explicitFalse.respawnRecovery=false;require(encodeDetVitalityConfig2D(explicitFalse)==oldBytes,"Explicit false preserves existing profile1 complete encoded bytes");
    legacy.recoveries={{10,1,5,false}};DeterministicSession refused({48,1,1,64,48});require(!installDetVitality2D(refused,legacy),"Profile1 rejects unbound new-only recovery authority");
    auto maximum=recoveryPolicy();maximum.actors.clear();maximum.progress.counters.clear();maximum.recoveries.clear();maximum.respawnFlow->bindings.clear();
    maximum.respawnFlow->externalDeathPermissions.clear();maximum.respawnFlow->respawn.spawns.clear();
    for(unsigned n=0;n<8;++n){const auto id=SimEntityId{10+n};maximum.actors.push_back({id,n+1,65535});maximum.progress.counters.push_back({n+1,0,UINT64_MAX,1});
        maximum.recoveries.push_back({id,n+1,1,false});maximum.respawnFlow->bindings.push_back({id,n+1});maximum.respawnFlow->externalDeathPermissions.push_back({n+1,id,6});
        maximum.respawnFlow->respawn.spawns.push_back({n+1,id,{D::fromInt(static_cast<int>(n)),D{}}});}
    const auto mutableSchemas=detVitalityStateSchemas2D(maximum);require(mutableSchemas.size()==2 && mutableSchemas[0].fields.size()==84
        && mutableSchemas[1].fields.size()==122 && encodeDetVitalityConfig2D(maximum).size()<=8192 && detVitalityPolicySchemas2D(maximum).size()<=9,"Eight actors use84+122 fields and bounded complete recovery identity");
    std::cout<<"PASS source-bound death and recovery namespace/identity/authority limits\n";
}
fs::path fresh(const fs::path& parent){require(fs::is_directory(parent),"--output must name existing parent");const auto t=std::chrono::steady_clock::now().time_since_epoch().count();
    for(unsigned n=0;n<1024;++n){auto path=parent/("vitality2d-"+std::to_string(t)+"-"+std::to_string(n));if(fs::create_directory(path))return path;}throw std::runtime_error("No fresh output directory");}
} // namespace
int main(int argc,char** argv){try{fs::path parent;for(int n=1;n<argc;++n){const std::string arg=argv[n];if(arg=="--output" && n+1<argc)parent=argv[++n];else throw std::invalid_argument("Usage: AYEntity_Vitality2DChecks --output EXISTING_PARENT");}
    const auto output=fresh(parent);integers();windowsAndBudget();enters();faults();action2();closure(output);limits();recoveryIntegers();recoveryClosure(output);recoveryFaultsAndLimits();std::ofstream summary(output/"summary.json",std::ios::binary);
    summary<<"{\"status\":\"passed\",\"profile\":1,\"recoveryProfile\":2,\"closureTicks\":8,\"closureCpRows\":9,\"rplFiles\":2,\"seeks\":4,\"negativeRestores\":26,\"recoveryClosureTicks\":8,\"recoveryClosureCpRows\":9,\"oracleScope\":\"selected hand integer/protection/region/death/recovery behavior; entire CP actual path equivalence\"}\n";
    summary.close();require(bool(summary),"Write summary");std::cout<<"PASS Stage47 vitality; output="<<output.string()<<'\n';return 0;
}catch(const std::exception& e){std::cerr<<"FAIL Stage47 vitality: "<<e.what()<<'\n';return 1;}}
