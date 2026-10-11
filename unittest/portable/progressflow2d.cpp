// design reference: AYEntity/design.md Stage41 automatic registered progress flow.
#include <AYEntity/DeterministicProgressFlow2D.h>
#include <AYEntity/DeterministicReplay.h>
#include <AYEntity/DeterministicRollback.h>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

using namespace ayt::entity;
namespace {
namespace fs=std::filesystem;
using D=ayt::math::DetFloat32;using V=ayt::math::DetVec2;using Bytes=std::vector<std::uint8_t>;
void require(bool good,const std::string& message){if(!good)throw std::runtime_error(message);}
D q(int value){return D::fromInt(value)/D::fromInt(4);}
DetProgressFlow2DConfig policy(){DetProgressFlow2DConfig c;auto& p=c.progress;p.actions.collision.extended=true;
    p.actions.targets={{20,1,true,{}},{30,1,true,{}}};p.counters={{1,0,4,0},{2,0,4,0}};
    p.objectives={{1,{{1,1}}},{2,{{2,1}}}};p.claims={{1,20,10,1,1}};
    c.claims={{1,0}};c.rules={{1,1,{30,DetActionKind2D::CollisionDisable},false}};return c;}
DetActorState actor(const DetCollision2DConfig& c,SimEntityId id){DetActorState a;DetSimTransformComponent pose;
    require(pose.setPosition({id==10?D::fromInt(-1):(id==30?D::fromInt(10):D{}),D{},D{}}),"Fixture pose");a.pose=pose.snapshot();
    a.blocks[c.bodySchema]=detCollisionBodyState2D(c,{id==10?DetBodyMode2D::Kinematic:DetBodyMode2D::Static,
        id==10?V{q(1),q(1)}:V{q(2),q(2)},V{},V{},id==10?1u:2u,id==10?2u:1u,id==20 || id==21});return a;}
enum class Mode {Normal,Spoof,Late,Retire};
bool configure(DeterministicSession& s,const DetProgressFlow2DConfig& c,Mode mode=Mode::Normal,bool reverse=false){
    if(!installDetActions2D(s,c.progress.actions) || !installDetProgress2D(s,c.progress) || !installDetProgressFlow2D(s,c))return false;
    if(!s.registerSystem(6,c.progress.actions.collision.priority,[c,mode](DetTickContext& ctx){
        for(const auto& command:ctx.input().commands){if(command.source!=1)return false;
            if(command.type==1 && command.payload.size()==1 && command.payload[0]<=2)ctx.write(10,c.progress.actions.collision.bodySchema,DetBodyVelocity,V{D::fromInt(int(command.payload[0])-1),D{}});
            else if(command.type==2 && command.payload.size()==3 && command.payload[0]>=1 && command.payload[0]<=2)
                (void)executeDetProgress2D(ctx,c.progress,{static_cast<DetProgressOperation2D>(command.payload[0]),command.payload[1],command.payload[2]});
            else if(command.type==3 && command.payload.empty()){}else return false;}
        if(mode==Mode::Spoof && !ctx.tick())ctx.emit(c.progress.actions.collision.eventType,encodeDetTriggerEvent2D({DetTriggerPhase2D::Enter,{10,20}}));
        if(mode==Mode::Retire && !ctx.tick())ctx.despawn(20);return true;}))return false;
    if(mode==Mode::Late && !s.registerSystem(9,c.progress.actions.collision.priority,[c](DetTickContext& ctx){for(const auto& command:ctx.input().commands)
        if(command.type==3)(void)executeDetProgress2D(ctx,c.progress,{DetProgressOperation2D::Add,1,1});return true;}))return false;
    if(!installDetCollision2D(s,c.progress.actions.collision))return false;
    std::vector<SimEntityId> ids{10,20,30};if(c.progress.claims.size()==2 && c.progress.claims[1].trigger==21)ids.insert(ids.begin()+2,21);
    if(reverse)std::reverse(ids.begin(),ids.end());for(const auto id:ids)if(!s.addEntity(id,actor(c.progress.actions.collision,id)))return false;return s.seal();}
DetSessionCheckpoint checkpoint(DeterministicSession& s){auto cp=s.checkpoint();require(bool(cp),s.error());return *cp;}
Bytes bytes(DeterministicSession& s){return encodeDetCheckpoint(checkpoint(s));}
void advance(DeterministicSession& s,DetTickInput in){const bool good=s.advance(std::move(in));require(good,s.error());}
DetTickInput frame(std::uint64_t tick,int velocity=0){return {tick,1,{{1,0,1,{static_cast<std::uint8_t>(velocity+1)}}}};}
DetTickInput operation(std::uint64_t tick,DetProgressOperation2D op,std::uint8_t counter,std::uint8_t amount){auto in=frame(tick);in.commands.push_back({1,1,2,{static_cast<std::uint8_t>(op),counter,amount}});return in;}
std::uint32_t mask(const DetSessionCheckpoint& cp,const DetProgressFlow2DConfig& c,SimEntityId id){return readDetState<std::uint32_t>(detCollisionBodySchema2D(c.progress.actions.collision),cp.actors.at(id).blocks.at(c.progress.actions.collision.bodySchema),DetBodyMask);}
void collectionAndEdges(){auto c=policy();DeterministicSession s({1,1,1,1,41});require(configure(s,c),s.error());advance(s,frame(0,1));
    require(detProgressCounter2D(checkpoint(s),c.progress,1)==0 && detProgressFlowClaim2D(checkpoint(s),c,1).attemptCount==0,"Actual Enter remains deferred until next tick");advance(s,frame(1));
    auto cp=checkpoint(s);auto claim=detProgressFlowClaim2D(cp,c,1);auto rule=detProgressFlowObjective2D(cp,c,1);
    require(claim.attemptCount==1 && claim.lastTick==1 && claim.progressSequence==1 && claim.lastResult==DetProgressCode2D::Applied
        && claim.actionAttemptCount==1 && claim.lastActionCode==DetActionCode2D::Applied && claim.lastActionSequence==1
        && mask(cp,c,20)==0 && detProgressCounter2D(cp,c.progress,1)==1,"Automatic real Enter consumes once with registered exact result");
    require(rule.previousSatisfied && !rule.onceConsumed && rule.attemptCount==1 && rule.lastTick==1 && rule.lastResult==DetActionCode2D::Applied
        && rule.actionSequence==2 && mask(cp,c,30)==0,"Collected count triggers objective action after collection");
    advance(s,frame(2));require(detProgressFlowObjective2D(checkpoint(s),c,1).attemptCount==1,"True objective never retries each tick");
    advance(s,operation(3,DetProgressOperation2D::Subtract,1,1));require(!detProgressFlowObjective2D(checkpoint(s),c,1).previousSatisfied,"Spent count reverses registered edge memory");
    advance(s,operation(4,DetProgressOperation2D::Add,1,1));rule=detProgressFlowObjective2D(checkpoint(s),c,1);
    require(rule.attemptCount==2 && rule.lastTick==4 && rule.lastResult==DetActionCode2D::Unchanged && rule.actionSequence==3,"New rising edge attempts again, records Unchanged");
    c=policy();c.progress.counters[0].initial=1;c.claims.clear();DeterministicSession initial({1,1,1,1,41});require(configure(initial,c),initial.error());
    require(!detProgressFlowObjective2D(checkpoint(initial),c,1).previousSatisfied,"Initial true objective has unconsumed edge");advance(initial,frame(0));
    require(detProgressFlowObjective2D(checkpoint(initial),c,1).attemptCount==1 && detProgressFlowObjective2D(checkpoint(initial),c,1).lastTick==0,"Initial true fires at actual tick0");
    std::cout<<"PASS real deferred Enter, automatic collection, initial edge and reversible objectives\n";}
void gatesAndOrdering(){auto c=policy();c.claims[0].requiredObjective=2;DeterministicSession gate({1,1,1,1,41});require(configure(gate,c),gate.error());
    advance(gate,frame(0,1));advance(gate,frame(1));require(detProgressFlowClaim2D(checkpoint(gate),c,1).attemptCount==0 && mask(checkpoint(gate),c,20)==1,"Closed gate ignores Enter without attempt or disappearing item");
    advance(gate,operation(2,DetProgressOperation2D::Add,2,1));require(detProgressFlowClaim2D(checkpoint(gate),c,1).attemptCount==0,"Stay after unlock does not collect/retry");
    advance(gate,frame(3,1));advance(gate,frame(4,-1));advance(gate,frame(5));require(detProgressCounter2D(checkpoint(gate),c.progress,1)==1,"New real Enter after unlock authorizes collection");
    c=policy();c.progress.actions.targets.insert(c.progress.actions.targets.begin()+1,{21,1,true,{}});c.progress.claims.push_back({2,21,10,1,1});c.claims.push_back({2,1});
    DeterministicSession order({1,1,1,1,41});require(configure(order,c,Mode::Normal,true),order.error());advance(order,frame(0,1));advance(order,frame(1));
    const auto cp=checkpoint(order);require(detProgressCounter2D(cp,c.progress,1)==2 && detProgressFlowClaim2D(cp,c,1).progressSequence==1
        && detProgressFlowClaim2D(cp,c,2).progressSequence==2 && mask(cp,c,20)==0 && mask(cp,c,21)==0
        && detProgressFlowObjective2D(cp,c,1).actionSequence==3,"Canonical first event's claim unlocks later event, then objective action");
    std::cout<<"PASS gate/new Enter policy and canonical event then stable claim then rule order\n";}
void denialsAndFaults(){auto c=policy();c.progress.actions.maxActionsPerTick=1;c.rules[0].once=true;
    DeterministicSession once({1,1,1,1,41});require(configure(once,c),once.error());advance(once,frame(0,1));advance(once,frame(1));
    auto rule=detProgressFlowObjective2D(checkpoint(once),c,1);require(rule.onceConsumed && rule.attemptCount==1 && rule.lastResult==DetActionCode2D::BudgetExceeded
        && rule.actionSequence==2 && mask(checkpoint(once),c,30)==1,"Claim uses opportunity first; denied objective consumes once without disabling target");
    advance(once,operation(2,DetProgressOperation2D::Subtract,1,1));advance(once,operation(3,DetProgressOperation2D::Add,1,1));
    require(detProgressFlowObjective2D(checkpoint(once),c,1).attemptCount==1 && mask(checkpoint(once),c,30)==1,"Consumed once denial never auto-retries even on another edge");
    c=policy();c.progress.counters[0].maximum=0;c.progress.objectives[0].conditions[0].atLeast=0;c.rules.clear();
    DeterministicSession range({1,1,1,1,41});require(configure(range,c),range.error());advance(range,frame(0,1));advance(range,frame(1));
    const auto denied=detProgressFlowClaim2D(checkpoint(range),c,1);require(denied.attemptCount==1 && denied.lastResult==DetProgressCode2D::OutOfRange
        && denied.actionAttemptCount==0 && mask(checkpoint(range),c,20)==1,"Range denial remains registered, preserving pickup/action budget");
    advance(range,frame(2));require(detProgressFlowClaim2D(checkpoint(range),c,1).attemptCount==1,"Stay cannot retry refused claim");
    c=policy();DeterministicSession spoof({1,1,1,1,41});require(configure(spoof,c,Mode::Spoof),spoof.error());advance(spoof,frame(0));advance(spoof,frame(1));
    require(detProgressFlowClaim2D(checkpoint(spoof),c,1).attemptCount==0,"Other producer same type/payload cannot authorize Flow");
    DeterministicSession late({1,1,1,1,41});require(configure(late,c,Mode::Late),late.error());const auto initial=checkpoint(late);const auto initialBytes=encodeDetCheckpoint(initial);
    auto input=frame(0);input.commands.push_back({1,1,3,{}});require(!late.advance(input) && late.faulted(),"Late Progress truth write faults committed boundary");
    require(late.restore(initial) && bytes(late)==initialBytes,"Explicit restore recovers complete state after late writer");advance(late,frame(0));
    std::cout<<"PASS normal claim/once Action denials, exact producer namespace and late-write recovery\n";}
void closure(const fs::path& output){const auto c=policy();std::vector<DetTickInput> inputs;for(unsigned t=0;t<8;++t)inputs.push_back(frame(t,t?0:1));
    inputs[2]=operation(2,DetProgressOperation2D::Subtract,1,1);inputs[3]=operation(3,DetProgressOperation2D::Add,1,1);
    DeterministicSession baseline({1,1,1,1,41});require(configure(baseline,c),baseline.error());std::vector<DetSessionCheckpoint> cps{checkpoint(baseline)};std::vector<Bytes> states{bytes(baseline)};
    std::vector<DetConfirmedEvent> expectedEvents;for(unsigned t=0;t<8;++t){advance(baseline,inputs[t]);cps.push_back(checkpoint(baseline));states.push_back(bytes(baseline));
        for(const auto& event:cps.back().pendingEvents)expectedEvents.push_back({1,t,event});}
    DeterministicSession restored({1,1,1,1,41});require(configure(restored,c,Mode::Normal,true) && restored.restore(cps[1]),restored.error());
    for(unsigned t=1;t<8;++t){advance(restored,inputs[t]);require(bytes(restored)==states[t+1],"Pending real Enter restore complete CP continuation");}
    const auto before=bytes(restored);const auto collect=detProgressFlowCollectSchema2D(c);const auto objective=detProgressFlowObjectiveSchema2D(c);
    for(unsigned n=0;n<10;++n){auto bad=cps[2];switch(n){case 0:writeDetState(objective,bad.globals.at(objective.id),detProgressFlowObjectiveField2D(c,1,DetFlowPreviousSatisfied),false);break;
        case 1:writeDetState(objective,bad.globals.at(objective.id),detProgressFlowObjectiveField2D(c,1,DetFlowOnceConsumed),true);break;
        case 2:writeDetState(collect,bad.globals.at(collect.id),detProgressFlowClaimField2D(c,1,DetFlowClaimLastTick),std::uint64_t{3});break;
        case 3:writeDetState(collect,bad.globals.at(collect.id),detProgressFlowClaimField2D(c,1,DetFlowClaimProgressSequence),std::uint64_t{2});break;
        case 4:writeDetState(collect,bad.globals.at(collect.id),detProgressFlowClaimField2D(c,1,DetFlowClaimActionAttemptCount),std::uint64_t{2});break;
        case 5:bad.globals.at(c.policySchemaBase).at(3)^=1;break;case 6:bad.actors.at(10).blocks.at(c.policySchemaBase).at(3)^=1;break;
        case 7:writeDetState(collect,bad.actors.at(10).blocks.at(collect.id),detProgressFlowClaimField2D(c,1,DetFlowClaimAttemptCount),std::uint64_t{1});break;
        case 8:bad.pendingEvents.push_back({c.progress.actions.collision.systemId,999,c.progress.actions.collision.eventType,{1}});break;
        case 9:writeDetState(objective,bad.globals.at(objective.id),detProgressFlowObjectiveField2D(c,1,DetFlowObjectiveAttemptCount),std::uint64_t{2});break;}
        require(!restored.restore(bad) && bytes(restored)==before,"Bad Flow policy/count/time/truth/event CP restore atomic case="+std::to_string(n));}
    auto changed=c;changed.rules[0].once=true;DeterministicSession foreign({1,1,1,1,41});require(configure(foreign,changed),foreign.error());const auto foreignBytes=bytes(foreign);
    require(foreign.manifest()!=baseline.manifest() && !foreign.restore(cps[1]) && bytes(foreign)==foreignBytes,"Full Flow identity differs with once policy, not just hash");
    DeterministicSession corrected({1,1,1,1,41});require(configure(corrected,c),corrected.error());DetRollbackConfig rc;rc.maxPredictionTicks=4;rc.prediction={{1,DetPredictionMode::Hold}};
    DeterministicRollback rollback(corrected,rc);for(unsigned t=0;t<8;++t){if(t==2){require(rollback.advance(),rollback.error());require(rollback.submit(1,inputs[t]),rollback.error());}
        else require(rollback.submit(1,inputs[t]) && rollback.advance(),rollback.error());require(bytes(corrected)==states[t+1],"Actual late input correction complete Flow CP");}
    require(rollback.rollbackCount()>0 && rollback.replayedTicks()>0,"Late input actually replayed");require(rollback.takeConfirmedEvents()==expectedEvents && rollback.takeConfirmedEvents().empty(),"Actual collision events match and drain exactly once");
    DeterministicSession recording({1,1,1,1,41});require(configure(recording,c),recording.error());DetReplayWriter writer;
    const bool began=writer.begin(recording,(output/"progressflow.rpl").string(),2);require(began,writer.error());for(unsigned t=0;t<8;++t){const bool advanced=writer.advance(inputs[t]);require(advanced,writer.error());require(bytes(recording)==states[t+1],"Actual Flow .rpl recording entire CP");}
    const bool finished=writer.finish();require(finished,writer.error());DeterministicSession playback({1,1,1,1,41});require(configure(playback,c),playback.error());DetReplayReader reader;
    const bool opened=reader.open(writer.path());require(opened,reader.error());require(reader.restoreInitial(playback),reader.error());while(!reader.atEnd()){require(reader.advance(playback),reader.error());require(bytes(playback)==states[playback.nextTick()],"Real Flow playback complete CP");}
    require(reader.seek(playback,1) && bytes(playback)==states[1],"Actual seek before collection");require(reader.seek(playback,8) && bytes(playback)==states[8],"Actual seek after edge retrigger");
    std::ofstream state(output/"final.state",std::ios::binary);state.write(reinterpret_cast<const char*>(states.back().data()),static_cast<std::streamsize>(states.back().size()));state.close();require(bool(state),"Write actual final.state");
    std::cout<<"PASS8tick/9CP restore, actual delayed rollback/confirmed drain and sealed .rpl/2seek byte equivalence\n";}
void limits(){auto c=policy();c.progress.claims.clear();c.claims.clear();c.rules.clear();for(unsigned n=1;n<=16;++n){c.progress.claims.push_back({n,20,100+n,1,1});c.claims.push_back({n,0});}
    for(unsigned n=1;n<=8;++n)c.rules.push_back({n,1,{30,DetActionKind2D::CollisionDisable},false});
    require(detProgressFlowCollectSchema2D(c).fields.size()==113 && detProgressFlowObjectiveSchema2D(c).fields.size()==49,"16claim/8rule descriptors bounded to113/49 fields");
    for(unsigned n=0;n<8;++n){auto bad=policy();switch(n){case 0:bad.claims[0].claim=99;break;case 1:bad.claims[0].requiredObjective=99;break;
        case 2:bad.rules[0].objective=99;break;case 3:bad.rules[0].action.target=99;break;case 4:bad.collectStateSchema=bad.progress.stateSchema;break;
        case 5:bad.objectiveStateSchema=bad.collectStateSchema;break;case 6:bad.systemId=bad.progress.actions.collision.systemId;break;case 7:bad.rules.push_back(bad.rules[0]);break;}
        DeterministicSession s({1,1,1,1,41});require(!installDetProgressFlow2D(s,bad) && !s.faulted(),"Malformed Flow preflight rejected case="+std::to_string(n));}
    c.claims.push_back({17,0});DeterministicSession extra({1,1,1,1,41});require(!installDetProgressFlow2D(extra,c),"17th binding rejected");
    c=policy();DeterministicSession missing({1,1,1,1,41});require(installDetActions2D(missing,c.progress.actions) && installDetProgressFlow2D(missing,c)
        && installDetCollision2D(missing,c.progress.actions.collision),"Missing Progress dependency fixture installs only other policies");
    for(const auto id:{10,20,30})require(missing.addEntity(id,actor(c.progress.actions.collision,id)),"Missing dependency actors");require(!missing.seal(),"Missing installed Progress closure refuses seal");
    std::cout<<"PASS bounded descriptor endpoints, references/namespaces and actual dependency seal closure\n";}
fs::path fresh(const fs::path& parent){require(fs::is_directory(parent),"--output must be existing parent");const auto time=std::chrono::steady_clock::now().time_since_epoch().count();
    for(unsigned n=0;n<1024;++n){auto p=parent/("progressflow2d-"+std::to_string(time)+"-"+std::to_string(n));if(fs::create_directory(p))return p;}throw std::runtime_error("No fresh output directory");}
} // namespace
int main(int argc,char** argv){try{fs::path parent;for(int n=1;n<argc;++n){const std::string arg=argv[n];if(arg=="--output" && n+1<argc)parent=argv[++n];else throw std::invalid_argument("Usage: AYEntity_ProgressFlow2DChecks --output EXISTING_PARENT");}
    const auto output=fresh(parent);collectionAndEdges();gatesAndOrdering();denialsAndFaults();closure(output);limits();std::ofstream summary(output/"summary.json",std::ios::binary);
    summary<<"{\"status\":\"passed\",\"profile\":1,\"closureTicks\":8,\"closureCpRows\":9,\"rplFiles\":1,\"seeks\":2,\"negativeRestores\":10,\"oracleScope\":\"selected hand count/claim/edge/action behavior; complete CP actual path equivalence\"}\n";
    summary.close();require(bool(summary),"Write summary");std::cout<<"PASS Stage41 progress flow; output="<<output.string()<<'\n';return 0;
}catch(const std::exception& e){std::cerr<<"FAIL Stage41 progress flow: "<<e.what()<<'\n';return 1;}}
