// Stage49 independent periodic behavior and real delivered event frontier.
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
fs::path fresh(const fs::path& parent){require(fs::is_directory(parent),"--output must name existing parent");const auto t=std::chrono::steady_clock::now().time_since_epoch().count();
    for(unsigned n=0;n<1024;++n){auto path=parent/("vitality2d-"+std::to_string(t)+"-"+std::to_string(n));if(fs::create_directory(path))return path;}throw std::runtime_error("No fresh output directory");}
DetVitality2DConfig periodicPolicy(std::uint32_t period=3){auto c=policy();c.periodicRegions=true;c.regions[0].periodTicks=period;c.actors[0].invulnerabilityTicks=0;return c;}
auto regionState(DeterministicSession& s,const DetVitality2DConfig& c){return detVitalityRegionState2D(checkpoint(s),c,1);}
void periodicIntegers(){auto c=periodicPolicy();DeterministicSession s({49,1,1,1,49});require(configure(s,c),s.error());
    for(unsigned t=0;t<8;++t){advance(s,frame(t,t?0:1));const auto attempts=t>=7?3u:(t>=4?2u:(t>=1?1u:0u));
        require(state(s,c).hp==20-3*attempts && regionState(s,c).attemptCount==attempts,"Independent stationary period3 attempts at processing1/4/7 only");}
    require(regionState(s,c).active && regionState(s,c).episodeCount==1 && regionState(s,c).lastAttemptTick==7 && regionState(s,c).nextEligible==10,"Delivered Stay advances the literal interval without extra episodes");
    auto heal=periodicPolicy(2);heal.progress.counters[0].initial=10;heal.regions[0].kind=DetVitalityKind2D::Heal;DeterministicSession healing({49,1,1,1,49});require(configure(healing,heal),healing.error());
    for(unsigned t=0;t<10;++t)advance(healing,frame(t,t?0:1));require(state(healing,heal).hp==20 && state(healing,heal).healCount==4
        && regionState(healing,heal).attemptCount==5 && result(healing,heal).code==DetVitalityCode2D::Unchanged && regionState(healing,heal).nextEligible==11,"Heal saturates, unchanged Stay attempt consumes the next interval");
    auto protectedPolicy=periodicPolicy(1);protectedPolicy.actors[0].invulnerabilityTicks=2;DeterministicSession protectedZone({49,1,1,1,49});require(configure(protectedZone,protectedPolicy),protectedZone.error());
    for(unsigned t=0;t<4;++t)advance(protectedZone,frame(t,t?0:1));require(state(protectedZone,protectedPolicy).hp==14 && regionState(protectedZone,protectedPolicy).attemptCount==3
        && state(protectedZone,protectedPolicy).hurtCount==2,"Invulnerability denial is a real attempt; N2 expires at3, no hidden timer retry");
    auto budgetPolicy=periodicPolicy();budgetPolicy.maxOperationsPerTick=1;DeterministicSession budget({49,1,1,1,49});require(configure(budget,budgetPolicy),budget.error());
    advance(budget,frame(0,1));advance(budget,operation(1,1,2));require(result(budget,budgetPolicy).code==DetVitalityCode2D::BudgetExceeded && regionState(budget,budgetPolicy).nextEligible==4,"Budget denial consumes region interval before any next attempt");
    for(unsigned t=2;t<5;++t)advance(budget,frame(t));require(state(budget,budgetPolicy).hp==15 && regionState(budget,budgetPolicy).attemptCount==2,"No budget denial retry before the next real Stay deadline");
    auto transientPolicy=periodicPolicy(1);DeterministicSession transient({49,1,4,1,49});require(configure(transient,transientPolicy),transient.error());advance(transient,frame(0,1));advance(transient,frame(1));advance(transient,frame(2));
    require(state(transient,transientPolicy).hp==17 && regionState(transient,transientPolicy).attemptCount==1 && !regionState(transient,transientPolicy).active
        && !regionState(transient,transientPolicy).nextEligible && regionState(transient,transientPolicy).lastPhase==DetTriggerPhase2D::Exit,"Actual transient Enter+Exit handles Exit even after the same-tick attempt");
    auto reentryPolicy=periodicPolicy(11);DeterministicSession reentry({49,1,1,1,49});require(configure(reentry,reentryPolicy),reentry.error());advance(reentry,frame(0,1));advance(reentry,frame(1));advance(reentry,frame(2,1));advance(reentry,frame(3,-1));
    require(!regionState(reentry,reentryPolicy).active,"Real delivered Exit clears the old episode");advance(reentry,frame(4));
    require(state(reentry,reentryPolicy).hp==14 && regionState(reentry,reentryPolicy).episodeCount==2 && regionState(reentry,reentryPolicy).attemptCount==2
        && regionState(reentry,reentryPolicy).nextEligible==15,"Real reentry attempts immediately before old period12, new deadline15");
    auto recoveredPolicy=recoveryPolicy();recoveredPolicy.periodicRegions=true;recoveredPolicy.regions={{1,20,10,DetVitalityKind2D::Hurt,5,3}};DeterministicSession revived({49,1,1,64,49});require(configureRecovery(revived,recoveredPolicy),revived.error());
    advance(revived,frame(0));advance(revived,frame(1));advance(revived,frame(2));require(state(revived,recoveredPolicy).hp==5 && recovered(revived,recoveredPolicy).restoreCount==1
        && regionState(revived,recoveredPolicy).attemptCount==1 && !regionState(revived,recoveredPolicy).active,"Accepted recovery does not fake a timer reset: actual endpoint Exit closes the episode next tick");
    std::cout<<"PASS periodic3 hand intervals/saturation/denials and actual transient/reentry/recovery frontier\n";}
void periodicClosure(const fs::path& output){const auto c=periodicPolicy();std::vector<DetTickInput> inputs;for(unsigned t=0;t<8;++t)inputs.push_back(frame(t,t==0 || t==2?1:(t==3?-1:0)));
    DeterministicSession base({49,1,1,1,49});require(configure(base,c),base.error());std::vector<DetSessionCheckpoint> cps{checkpoint(base)};std::vector<Bytes> states{bytes(base)};std::vector<DetConfirmedEvent> events;
    for(const auto& input:inputs){advance(base,input);cps.push_back(checkpoint(base));states.push_back(bytes(base));for(const auto& e:cps.back().pendingEvents)events.push_back({1,input.tick,e});}
    require(state(base,c).hp==11 && regionState(base,c).attemptCount==3 && !events.empty(),"Hand periodic closure has actual attempts1/4/7 and producer events");
    DeterministicSession restored({49,1,1,1,49});require(configure(restored,c,Mode::Normal,true) && restored.restore(cps[2]),restored.error());
    for(unsigned t=2;t<8;++t){advance(restored,inputs[t]);require(bytes(restored)==states[t+1],"Pending interval exact CP restoration and delivered Exit/reentry continuation");}
    const auto before=bytes(restored);const auto schema=detVitalityStateSchemas2D(c).back();const auto field=[&](auto f){return detVitalityRegionField2D(c,1,f);};
    for(unsigned n=0;n<6;++n){auto bad=cps[2];switch(n){case 0:writeDetState(schema,bad.globals.at(c.regionStateSchema),field(DetVitalityRegionNextEligible),std::uint64_t{5});break;
        case 1:writeDetState(schema,bad.globals.at(c.regionStateSchema),field(DetVitalityRegionLastPhase),std::uint32_t{3});break;
        case 2:writeDetState(schema,bad.globals.at(c.regionStateSchema),DetVitalityRegionTotalCount,std::uint64_t{2});break;
        case 3:writeDetState(schema,bad.actors.at(10).blocks.at(c.regionStateSchema),field(DetVitalityRegionAttempts),std::uint64_t{1});break;
        case 4:writeDetState(schema,bad.globals.at(c.regionStateSchema),DetVitalityRegionProcessedTick,std::uint64_t{1});break;
        case 5:bad.globals.at(c.policySchemaBase).at(3)^=1;break;}
        require(!restored.restore(bad) && bytes(restored)==before,"Periodic forged deadline/count/phase/default/process/policy rejected atomically case="+std::to_string(n));}
    DeterministicSession corrected({49,1,1,1,49});require(configure(corrected,c),corrected.error());DetRollbackConfig rc;rc.maxPredictionTicks=4;rc.prediction={{1,DetPredictionMode::Hold},{2,DetPredictionMode::Omit}};DeterministicRollback rollback(corrected,rc);
    for(unsigned t=0;t<8;++t){if(t==2){require(rollback.advance(),rollback.error());require(rollback.submit(1,inputs[t]),rollback.error());}else{require(rollback.submit(1,inputs[t]),rollback.error());require(rollback.advance(),rollback.error());}
        require(bytes(corrected)==states[t+1],"Late real exit input correction matches complete periodic CP");}
    require(rollback.rollbackCount()>0 && rollback.replayedTicks()>0 && rollback.takeConfirmedEvents()==events && rollback.takeConfirmedEvents().empty(),"Periodic actual replay and exactly-once collision events");
    DeterministicSession recording({49,1,1,1,49});require(configure(recording,c),recording.error());DetReplayWriter writer;const bool began=writer.begin(recording,(output/"vitality-periodic.rpl").string(),2);require(began,writer.error());
    for(unsigned t=0;t<8;++t){const bool ok=writer.advance(inputs[t]);require(ok,writer.error());require(bytes(recording)==states[t+1],"Periodic actual recorder CP");}const bool finished=writer.finish();require(finished,writer.error());
    DeterministicSession playback({49,1,1,1,49});require(configure(playback,c),playback.error());DetReplayReader reader;const bool opened=reader.open(writer.path());require(opened,reader.error());require(reader.restoreInitial(playback),reader.error());
    while(!reader.atEnd()){require(reader.advance(playback),reader.error());require(bytes(playback)==states[playback.nextTick()],"Periodic actual playback CP");}
    require(reader.seek(playback,2) && bytes(playback)==states[2] && reader.seek(playback,8) && bytes(playback)==states[8],"Periodic actual seek preserves pending interval and final episode");
    auto old=policy();old.regions[0].periodTicks=1;DeterministicSession refused({49,1,1,1,49});require(!installDetVitality2D(refused,old),"Default profile1 cannot silently authorize a periodic interval");
    auto malformed=c;malformed.regions[0].periodTicks=65536;DeterministicSession tooLong({49,1,1,1,49});require(!installDetVitality2D(tooLong,malformed),"Period above65535 is rejected");
    auto maximum=c;maximum.regions.clear();for(unsigned n=1;n<=16;++n)maximum.regions.push_back({n,20,10,DetVitalityKind2D::Hurt,1,65535});
    const auto mutableSchemas=detVitalityStateSchemas2D(maximum);require(mutableSchemas.back().fields.size()==114 && mutableSchemas.back().version==3
        && encodeDetVitalityConfig2D(maximum).size()<=8192 && detVitalityPolicySchemas2D(maximum).size()<=9,"Sixteen region/period65535 descriptor is exactly114 fields within bounded full identity");
    auto enterOnly=periodicPolicy(0);DeterministicSession once({49,1,1,1,49});require(configure(once,enterOnly),once.error());
    advance(once,frame(0,1));advance(once,frame(1));advance(once,frame(2));require(state(once,enterOnly).hp==17 && regionState(once,enterOnly).attemptCount==1
        && !regionState(once,enterOnly).active && !regionState(once,enterOnly).episodeCount,"Explicit profile3 period0 retains Enter-only behavior with no contact timer");
    std::cout<<"PASS periodic3 8tick actual restore/late rollback/real .rpl and6negative CP closure\n";}
} // namespace
int main(int argc,char** argv){try{fs::path parent;for(int n=1;n<argc;++n){const std::string arg=argv[n];
    if(arg=="--output" && n+1<argc)parent=argv[++n];else throw std::invalid_argument("Usage: AYEntity_VitalityRegions3Checks --output EXISTING_PARENT");}
    const auto output=fresh(parent);periodicIntegers();periodicClosure(output);std::ofstream summary(output/"summary.json",std::ios::binary);
    summary<<"{\"status\":\"passed\",\"profile\":3,\"closureTicks\":8,\"closureCpRows\":9,\"rplFiles\":1,\"seeks\":2,\"negativeRestores\":6,\"oracleScope\":\"selected independent integer HP and interval behavior; full CP actual path equivalence\"}\n";
    summary.close();require(bool(summary),"Write summary");std::cout<<"PASS Stage49 periodic Vitality; output="<<output.string()<<'\n';return 0;
}catch(const std::exception& e){std::cerr<<"FAIL Stage49 periodic Vitality: "<<e.what()<<'\n';return 1;}}
