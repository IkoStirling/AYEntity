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
fs::path fresh(const fs::path& parent){require(fs::is_directory(parent),"--output must name existing parent");const auto t=std::chrono::steady_clock::now().time_since_epoch().count();
    for(unsigned n=0;n<1024;++n){auto path=parent/("vitality2d-"+std::to_string(t)+"-"+std::to_string(n));if(fs::create_directory(path))return path;}throw std::runtime_error("No fresh output directory");}
} // namespace
int main(int argc,char** argv){try{fs::path parent;for(int n=1;n<argc;++n){const std::string arg=argv[n];if(arg=="--output" && n+1<argc)parent=argv[++n];else throw std::invalid_argument("Usage: AYEntity_Vitality2DChecks --output EXISTING_PARENT");}
    const auto output=fresh(parent);integers();windowsAndBudget();enters();faults();action2();closure(output);limits();std::ofstream summary(output/"summary.json",std::ios::binary);
    summary<<"{\"status\":\"passed\",\"profile\":1,\"closureTicks\":8,\"closureCpRows\":9,\"rplFiles\":1,\"seeks\":2,\"negativeRestores\":12,\"oracleScope\":\"selected hand integer/protection/region/death behavior; entire CP actual path equivalence\"}\n";
    summary.close();require(bool(summary),"Write summary");std::cout<<"PASS Stage47 vitality; output="<<output.string()<<'\n';return 0;
}catch(const std::exception& e){std::cerr<<"FAIL Stage47 vitality: "<<e.what()<<'\n';return 1;}}
