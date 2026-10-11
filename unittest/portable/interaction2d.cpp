// Stage34: real collision event routing, bounded state and exact restore/replay.
#include <AYEntity/DeterministicInteraction2D.h>
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
void require(bool condition,const std::string& text){if(!condition)throw std::runtime_error(text);}
D q(int n,int denominator=4){return D::fromInt(n)/D::fromInt(denominator);}
DetInteraction2DConfig policy(){DetInteraction2DConfig c;c.actions.collision.extended=true;
    c.actions.targets={{30,1,true,{}}};c.rules={{1,20,10,DetTriggerPhase2D::Enter,{30,DetActionKind2D::CollisionDisable},false,0,0}};return c;}
DetActorState actor(const DetCollision2DConfig& c,DetBodyMode2D mode,D x,V half,std::uint32_t layer,std::uint32_t mask,bool trigger=false){
    DetActorState state;DetSimTransformComponent pose;require(pose.setPosition({x,{},{}}),"Pose initializer");state.pose=pose.snapshot();
    state.blocks[c.bodySchema]=detCollisionBodyState2D(c,{mode,half,{},{},layer,mask,trigger});return state;}
DetSessionCheckpoint checkpoint(DeterministicSession& session){auto cp=session.checkpoint();require(bool(cp),session.error());return *cp;}
Bytes bytes(DeterministicSession& session){return encodeDetCheckpoint(checkpoint(session));}
DetTickInput frame(std::uint64_t tick,int velocity){return {tick,1,{{1,0,1,{static_cast<std::uint8_t>(velocity+1)}}}};}
int direction(std::uint64_t tick){return tick%4<2?1:-1;}
bool configure(DeterministicSession& session,const DetInteraction2DConfig& c,bool occupied=false,bool spoof=false,bool reverse=false,bool retired=false){
    if(!installDetActions2D(session,c.actions) || !installDetInteraction2D(session,c))return false;
    if(!session.registerSystem(6,0,[c,spoof,retired](DetTickContext& context){
        const auto& commands=context.input().commands;if(commands.size()!=1)return false;const auto& command=commands.front();
        if(command.source!=1 || command.sequence || command.type!=1 || command.payload.size()!=1 || command.payload.front()>2)return false;
        context.write(10,c.actions.collision.bodySchema,DetBodyVelocity,V{D::fromInt(int(command.payload.front())-1),{}});
        if(retired && context.tick()==2)context.despawn(30);
        if(spoof && !context.tick())context.emit(c.actions.collision.eventType,encodeDetTriggerEvent2D({DetTriggerPhase2D::Enter,{10,20}}));
        return true;}))return false;
    if(!installDetCollision2D(session,c.actions.collision))return false;
    std::vector<std::pair<SimEntityId,DetActorState>> actors{
        {10,actor(c.actions.collision,DetBodyMode2D::Kinematic,occupied?D{}:D::fromInt(-1),{q(1),q(1)},1,2)},
        {20,actor(c.actions.collision,DetBodyMode2D::Static,D{},{q(2),q(2)},2,1,true)},
        {30,actor(c.actions.collision,DetBodyMode2D::Static,occupied?D{}:D::fromInt(10),{q(2),q(2)},2,occupied?0:1)}};
    if(reverse)std::reverse(actors.begin(),actors.end());for(auto& [id,state]:actors)if(!session.addEntity(id,std::move(state)))return false;
    return session.seal();
}
std::uint32_t mask(const DetSessionCheckpoint& cp,const DetInteraction2DConfig& c){return readDetState<std::uint32_t>(
    detCollisionBodySchema2D(c.actions.collision),cp.actors.at(30).blocks.at(c.actions.collision.bodySchema),DetBodyMask);}
void realTiming(){
    auto c=policy();c.rules[0].once=true;c.rules[0].delayTicks=2;
    c.rules.push_back({2,20,10,DetTriggerPhase2D::Exit,{30,DetActionKind2D::CollisionEnable},false,4,0});
    DeterministicSession session({1,1,1,1,34});require(configure(session,c),session.error());
    const std::uint64_t schedules[]{0,1,1,1,1,1,1,1};
    for(unsigned t=0;t<8;++t){require(session.advance(frame(t,direction(t))),session.error());const auto cp=checkpoint(session);
        const auto enter=detInteractionState2D(cp,c,1),exit=detInteractionState2D(cp,c,2);
        require(enter.scheduleCount==schedules[t],"Enter next-tick schedule row="+std::to_string(t+1));
        if(t==1 || t==2)require(enter.pending && enter.attemptCount==0 && enter.lastScheduleTick==1 && enter.dueTick==3 && mask(cp,c)==1,"Delay2 fixed pending before due");
        if(t>=3)require(!enter.pending && enter.attemptCount==1 && enter.lastExecutionTick==3 && enter.lastResult==DetActionCode2D::Applied,"Once consumed on schedule and executes exactly at tick3");
        if(t==2)require(exit.attemptCount==1 && exit.lastExecutionTick==2 && exit.lastResult==DetActionCode2D::Unchanged,"Actual Exit delivered next tick");
        if(t==4)require(exit.scheduleCount==1 && mask(cp,c)==0,"Cooldown4 ignores Exit at tick4");
        if(t>=6)require(exit.scheduleCount==2 && exit.lastScheduleTick==6 && exit.lastResult==DetActionCode2D::Applied && mask(cp,c)==1,"Cooldown4 permits Exit at tick6");
    }
    std::cout<<"PASS actual Enter/Exit next-tick delivery, once, delay2 and cooldown4\n";
}
void sourceAndDenial(){
    auto c=policy();DeterministicSession spoof({1,1,1,1,34});require(configure(spoof,c,false,true),spoof.error());
    for(unsigned t=0;t<3;++t)require(spoof.advance(frame(t,0)),spoof.error());
    require(detInteractionState2D(checkpoint(spoof),c,1).scheduleCount==0,"Same event type/payload from input system6 cannot spoof collision source30");
    c.rules[0].once=true;c.rules[0].action.kind=DetActionKind2D::CollisionEnable;
    DeterministicSession denied({1,1,1,1,34});require(configure(denied,c,true),denied.error());
    for(unsigned t=0;t<2;++t)require(denied.advance(frame(t,0)),denied.error());
    auto cp=checkpoint(denied);auto r=detInteractionState2D(cp,c,1);
    require(r.fired && !r.pending && r.scheduleCount==1 && r.attemptCount==1 && r.lastScheduleTick==1
        && r.lastExecutionTick==1 && r.lastResult==DetActionCode2D::Occupied && mask(cp,c)==0,"Normal Occupied denial is registered and consumes once without changing target");
    for(unsigned t=2;t<7;++t)require(denied.advance(frame(t,t==2 || t==3?1:-1)),denied.error());
    require(detInteractionState2D(checkpoint(denied),c,1).attemptCount==1,"Once denial does not auto-retry or reschedule later Enter");
    c=policy();c.rules[0].once=true;c.rules[0].delayTicks=2;
    DeterministicSession gone({1,1,1,1,34});require(configure(gone,c,false,false,false,true),gone.error());
    for(unsigned t=0;t<4;++t)require(gone.advance(frame(t,direction(t))),gone.error());
    r=detInteractionState2D(checkpoint(gone),c,1);require(r.fired && r.attemptCount==1 && r.lastResult==DetActionCode2D::MissingActor,"Pending action retains retired target identity and records MissingActor once");
    std::cout<<"PASS exact producer namespace, normal occupied denial, once consumption and retired-target delayed denial\n";
}
void dueOrderAndBudget(){auto c=policy();c.actions.maxActionsPerTick=1;c.rules[0].delayTicks=2;c.rules[0].once=true;
    c.rules.push_back({2,20,10,DetTriggerPhase2D::Enter,{30,DetActionKind2D::CollisionEnable},true,0,2});
    c.rules.push_back({3,20,10,DetTriggerPhase2D::Enter,{30,DetActionKind2D::CollisionEnable},true,0,0});
    DeterministicSession s({1,1,1,1,34});require(configure(s,c),s.error());
    for(unsigned t=0;t<4;++t)require(s.advance(frame(t,direction(t))),s.error());const auto cp=checkpoint(s);
    require(detInteractionState2D(cp,c,1).lastResult==DetActionCode2D::Applied
        && detInteractionState2D(cp,c,2).lastResult==DetActionCode2D::BudgetExceeded
        && detInteractionState2D(cp,c,3).lastResult==DetActionCode2D::Unchanged && mask(cp,c)==0,
        "Due ruleID order shares Action tick budget, denial doesn't mutate target");
    std::cout<<"PASS old due jobs rule-ID order and shared action budget denial\n";
}
void pendingAndRollback(){auto c=policy();c.rules[0].delayTicks=4;
    DeterministicSession pending({1,1,1,1,34});require(configure(pending,c),pending.error());
    for(unsigned t=0;t<5;++t)require(pending.advance(frame(t,direction(t))),pending.error());
    auto r=detInteractionState2D(checkpoint(pending),c,1);
    require(r.pending && r.scheduleCount==1 && r.attemptCount==0 && r.dueTick==5,"Non-once pending ignores repeated actual Enter at processingtick3");
    require(pending.advance(frame(5,direction(5))),pending.error());r=detInteractionState2D(checkpoint(pending),c,1);
    require(r.pending && r.scheduleCount==2 && r.attemptCount==1 && r.lastExecutionTick==5 && r.dueTick==9
        && r.lastResult==DetActionCode2D::Applied,"Due job runs before new same-tick Enter, which creates a new delay4 pending job");
    c=policy();c.rules[0].delayTicks=2;
    DeterministicSession baseline({1,1,1,1,34}),corrected({1,1,1,1,34});
    require(configure(baseline,c) && configure(corrected,c),"Rollback rule fixtures");
    std::vector<Bytes> states{bytes(baseline)};std::vector<DetConfirmedEvent> expectedEvents;
    for(unsigned t=0;t<8;++t){require(baseline.advance(frame(t,direction(t))),baseline.error());states.push_back(bytes(baseline));
        for(const auto& event:checkpoint(baseline).pendingEvents)expectedEvents.push_back({1,t,event});}
    DetRollbackConfig config;config.maxPredictionTicks=4;config.prediction={{1,DetPredictionMode::Hold}};
    DeterministicRollback rollback(corrected,config);
    for(unsigned t=0;t<8;++t){const auto input=frame(t,direction(t));
        if(t==2){require(rollback.advance(),rollback.error());require(rollback.submit(1,input),rollback.error());}
        else require(rollback.submit(1,input) && rollback.advance(),rollback.error());
        require(bytes(corrected)==states[t+1],"Actual delayed-input rollback includes rule pending/results entireCP");
    }
    require(rollback.rollbackCount()>0 && rollback.replayedTicks()>0,"Real rollback correction actually executed");
    const auto events=rollback.takeConfirmedEvents();require(!events.empty() && events==expectedEvents && rollback.takeConfirmedEvents().empty(),"Actual collision effects match baseline and are confirmed/drained exactly once");
    std::cout<<"PASS non-once pending deduplication/due-first ordering and actual delayed-input rollback/confirmed drain\n";
}
void identityAndRestore(const fs::path& output){auto c=policy();c.rules[0].once=true;c.rules[0].delayTicks=2;
    DeterministicSession baseline({1,1,1,1,34});require(configure(baseline,c),baseline.error());
    std::vector<DetSessionCheckpoint> cps{checkpoint(baseline)};std::vector<Bytes> states{bytes(baseline)};
    for(unsigned t=0;t<8;++t){require(baseline.advance(frame(t,direction(t))),baseline.error());cps.push_back(checkpoint(baseline));states.push_back(bytes(baseline));}
    DeterministicSession branch({1,1,1,1,34});require(configure(branch,c,false,false,true) && branch.restore(cps[2]),branch.error());
    for(unsigned t=2;t<8;++t){require(branch.advance(frame(t,direction(t))),branch.error());require(bytes(branch)==states[t+1],"Pending restore entire canonical checkpoint continuation");}
    const auto before=bytes(branch);const auto schema=detInteractionStateSchema2D(c);
    for(unsigned n=0;n<9;++n){auto bad=cps[2];switch(n){
        case 0:writeDetState(schema,bad.globals.at(c.stateSchema),detInteractionField2D(c,1,DetInteractionDueTick),std::uint64_t{1});break;
        case 1:writeDetState(schema,bad.globals.at(c.stateSchema),detInteractionField2D(c,1,DetInteractionScheduleCount),std::uint64_t{2});break;
        case 2:writeDetState(schema,bad.globals.at(c.stateSchema),detInteractionField2D(c,1,DetInteractionLastScheduleTick),std::uint64_t{2});break;
        case 3:writeDetState(schema,bad.globals.at(c.stateSchema),detInteractionField2D(c,1,DetInteractionLastResult),std::uint32_t{9});break;
        case 4:bad.globals.at(c.policySchemaBase).at(3)^=1;break;
        case 5:bad.actors.at(10).blocks.at(c.policySchemaBase).at(3)^=1;break;
        case 6:writeDetState(schema,bad.actors.at(10).blocks.at(c.stateSchema),detInteractionField2D(c,1,DetInteractionFired),true);break;
        case 7:bad.globals.at(c.actions.policySchemaBase).at(3)^=1;break;
        case 8:bad.pendingEvents={{c.actions.collision.systemId,0,c.actions.collision.eventType,{1,2,3}}};break;}
        require(!branch.restore(bad) && bytes(branch)==before,"Malformed rule/actions policy/state restore is atomic case="+std::to_string(n));}
    auto changed=c;changed.rules[0].delayTicks=3;DeterministicSession other({1,1,1,1,34});require(configure(other,changed),other.error());
    require(other.manifest()!=baseline.manifest(),"Full immutable timing content binds manifest, not only a hash");const auto otherBefore=bytes(other);
    require(!other.restore(cps[2]) && bytes(other)==otherBefore,"Foreign rule content restore atomic");
    auto newer=cps.back();writeDetState(schema,newer.globals.at(c.stateSchema),detInteractionField2D(c,1,DetInteractionLastScheduleTick),std::uint64_t{4});
    writeDetState(schema,newer.globals.at(c.stateSchema),detInteractionField2D(c,1,DetInteractionLastExecutionTick),std::uint64_t{6});
    require(!branch.restore(newer) && bytes(branch)==before,"Otherwise valid delayed execution cannot be newer than latest Actions execution stamp");
    auto repeat=policy();repeat.rules[0].delayTicks=2;DeterministicSession forged({1,1,1,1,34});require(configure(forged,repeat),forged.error());
    for(unsigned t=0;t<4;++t)require(forged.advance(frame(t,direction(t))),forged.error());auto countBad=checkpoint(forged);const auto forgedBefore=bytes(forged);
    const auto repeatSchema=detInteractionStateSchema2D(repeat);
    writeDetState(repeatSchema,countBad.globals.at(repeat.stateSchema),detInteractionField2D(repeat,1,DetInteractionScheduleCount),std::uint64_t{3});
    writeDetState(repeatSchema,countBad.globals.at(repeat.stateSchema),detInteractionField2D(repeat,1,DetInteractionAttemptCount),std::uint64_t{2});
    require(!forged.restore(countBad) && bytes(forged)==forgedBefore,"Otherwise coherent non-once pending counters cannot exceed registered total Actions attempts");
    auto mismatched=c.actions;mismatched.maxActionsPerTick=63;DeterministicSession mismatch({1,1,1,1,34});
    require(installDetActions2D(mismatch,mismatched) && installDetInteraction2D(mismatch,c) && installDetCollision2D(mismatch,c.actions.collision),"Separate Actions mismatch fixture registration");
    require(mismatch.addEntity(10,actor(c.actions.collision,DetBodyMode2D::Kinematic,D::fromInt(-1),{q(1),q(1)},1,2))
        && mismatch.addEntity(20,actor(c.actions.collision,DetBodyMode2D::Static,D{},{q(2),q(2)},2,1,true))
        && mismatch.addEntity(30,actor(c.actions.collision,DetBodyMode2D::Static,D::fromInt(10),{q(2),q(2)},2,1)),"Mismatch actors");
    require(!mismatch.seal(),"Rules require exact matching independently installed Actions policy before first tick");
    DeterministicSession recording({1,1,1,1,34});require(configure(recording,c),recording.error());DetReplayWriter writer;
    const bool began=writer.begin(recording,(output/"interaction.rpl").string(),2);require(began,writer.error());
    for(unsigned t=0;t<8;++t){require(writer.advance(frame(t,direction(t))),writer.error());require(bytes(recording)==states[t+1],"Rule actual writer fullCP");}require(writer.finish(),writer.error());
    DeterministicSession playback({1,1,1,1,34});require(configure(playback,c),playback.error());DetReplayReader reader;
    const bool opened=reader.open(writer.path());require(opened,reader.error());require(reader.restoreInitial(playback),reader.error());
    while(!reader.atEnd()){require(reader.advance(playback),reader.error());require(bytes(playback)==states[playback.nextTick()],"Rule actual .rpl playback fullCP");}
    require(reader.seek(playback,2) && bytes(playback)==states[2],"Actual seek restores pending job");
    require(reader.seek(playback,8) && bytes(playback)==states[8],"Actual seek consumes pending action exactly once");
    std::ofstream out(output/"final.state",std::ios::binary);out.write(reinterpret_cast<const char*>(states.back().data()),static_cast<std::streamsize>(states.back().size()));out.close();require(bool(out),"Write full final checkpoint");
    std::cout<<"PASS complete policy identity, corrupted/foreign restore atomicity, pending branch and actual sealed .rpl playback/seek\n";
}
void limitsAndRoles(){auto c=policy();
    for(unsigned n=0;n<16;++n){auto r=c.rules.front();r.id=n+1;r.delayTicks=65535;r.cooldownTicks=65535;if(n)c.rules.push_back(r);else c.rules[0]=r;}
    require(detInteractionStateSchema2D(c).fields.size()==128,"16-rule exact128-field endpoint");DeterministicSession bound({1,1,1,1,34});require(configure(bound,c),bound.error());
    for(unsigned t=0;t<2;++t)require(bound.advance(frame(t,direction(t))),bound.error());
    const auto end=detInteractionState2D(checkpoint(bound),c,16);require(end.pending && end.dueTick==65536 && end.cooldownUntil==65536,"65535 timer schedule addition exact, no wrap");
    for(unsigned kind=0;kind<7;++kind){auto bad=policy();switch(kind){case 0:bad.rules[0].delayTicks=65536;break;
        case 1:bad.rules[0].phase=DetTriggerPhase2D::Stay;break;case 2:bad.rules[0].actor=20;break;
        case 3:bad.systemId=bad.actions.collision.systemId;break;case 4:bad.stateSchema=bad.actions.stateSchema;break;
        case 5:bad.rules[0].action.kind=static_cast<DetActionKind2D>(8);break;case 6:bad.policySchemaBase=bad.actions.policySchemaBase;break;}
        DeterministicSession fresh({1,1,1,1,34});require(!installDetInteraction2D(fresh,bad) && !fresh.faulted() && !fresh.sealed(),"Malformed own config rejected before registration case="+std::to_string(kind));}
    auto extra=c.rules.back();extra.id=17;c.rules.push_back(extra);DeterministicSession tooMany({1,1,1,1,34});require(!installDetInteraction2D(tooMany,c),"17th rule rejected before registration");
    c=policy();DeterministicSession badRole({1,1,1,1,34});require(installDetActions2D(badRole,c.actions) && installDetInteraction2D(badRole,c)
        && installDetCollision2D(badRole,c.actions.collision),"Role fixture registration");
    require(badRole.addEntity(10,actor(c.actions.collision,DetBodyMode2D::Kinematic,D::fromInt(-1),{q(1),q(1)},1,2))
        && badRole.addEntity(20,actor(c.actions.collision,DetBodyMode2D::Static,D{},{q(2),q(2)},2,1,false))
        && badRole.addEntity(30,actor(c.actions.collision,DetBodyMode2D::Static,D::fromInt(10),{q(2),q(2)},2,1)),"Role fixture actors");
    require(!badRole.seal(),"Configured trigger identity must actually be a Trigger body");
    std::cout<<"PASS16-rule/timer endpoints, bounded malformed config and actual trigger-role seal validation\n";
}
fs::path fresh(const fs::path& parent){require(fs::is_directory(parent),"--output requires existing parent");const auto stamp=std::chrono::steady_clock::now().time_since_epoch().count();
    for(unsigned n=0;n<1024;++n){auto path=parent/("interaction2d-"+std::to_string(stamp)+"-"+std::to_string(n));if(fs::create_directory(path))return path;}throw std::runtime_error("No fresh evidence directory");}
} // namespace
int main(int argc,char** argv){try{fs::path parent;for(int n=1;n<argc;++n){const std::string arg=argv[n];
    if(arg=="--output" && n+1<argc)parent=argv[++n];else throw std::invalid_argument("Usage: AYEntity_Interaction2DChecks --output EXISTING_PARENT");}
    const auto output=fresh(parent);realTiming();sourceAndDenial();dueOrderAndBudget();pendingAndRollback();identityAndRestore(output);limitsAndRoles();
    std::ofstream summary(output/"summary.json",std::ios::binary);summary<<"{\"status\":\"passed\",\"profile\":1,\"ticks\":8,\"cpRows\":9,\"rplFiles\":1,\"seeks\":2,\"maxRules\":16,\"maxTimer\":65535,\"oracleScope\":\"selected hand state/timing; whole CP actual path equivalence\"}\n";
    summary.close();require(bool(summary),"Summary write");std::cout<<"PASS Stage34 interactions; output="<<output.string()<<'\n';return 0;
}catch(const std::exception& error){std::cerr<<"FAIL Stage34: "<<error.what()<<'\n';return 1;}}
