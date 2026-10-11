// Stage37 hand integer expectations plus actual collision/restore/replay paths.
#include <AYEntity/DeterministicProgress2D.h>
#include <AYEntity/DeterministicReplay.h>
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
D q(int n){return D::fromInt(n)/D::fromInt(4);}
DetProgress2DConfig policy(){DetProgress2DConfig c;c.actions.collision.extended=true;c.actions.targets={{20,1,true,{}}};
    c.counters={{1,0,3,0},{2,1,UINT64_MAX,UINT64_MAX-1}};c.objectives={{1,{{1,1},{2,UINT64_MAX}}}};c.claims={{1,20,10,1,1}};return c;}
DetActorState actor(const DetCollision2DConfig& c,SimEntityId id){DetActorState a;DetSimTransformComponent pose;
    require(pose.setPosition({id==10?D::fromInt(-1):D{},D{},D{}}),"Fixture pose");a.pose=pose.snapshot();
    a.blocks[c.bodySchema]=detCollisionBodyState2D(c,{id==10?DetBodyMode2D::Kinematic:DetBodyMode2D::Static,
        id==10?V{q(1),q(1)}:V{q(2),q(2)},V{},V{},id==10?1u:2u,id==10?2u:1u,id==20});return a;}
enum class Mode {Normal,NoClaim,Budget,Spoof,RetireBeforeClaim,RetireAfterClaim};
bool configure(DeterministicSession& session,const DetProgress2DConfig& c,Mode mode=Mode::Normal,bool reverse=false){
    if(!installDetActions2D(session,c.actions) || !installDetProgress2D(session,c))return false;
    if(!session.registerSystem(6,0,[c,mode](DetTickContext& ctx){
        const auto ids=ctx.entities();for(const auto& command:ctx.input().commands){
            if(command.source!=1)return false;
            if(command.type==1 && command.payload.size()==1 && command.payload[0]<=2){
                if(std::binary_search(ids.begin(),ids.end(),10))ctx.write(10,c.actions.collision.bodySchema,DetBodyVelocity,V{D::fromInt(int(command.payload[0])-1),D{}});
            }else if(command.type==2 && command.payload.size()==10){std::uint64_t amount=0;for(unsigned n=0;n<8;++n)amount|=std::uint64_t{command.payload[n+2]}<<(8*n);
                (void)executeDetProgress2D(ctx,c,{static_cast<DetProgressOperation2D>(command.payload[0]),command.payload[1],amount});
            }else if(command.type==3 && command.payload.size()==1)(void)claimDetCollectible2D(ctx,c,command.payload[0]);
            else if(command.type==4 && command.payload.size()==1 && command.payload[0]<=1)
                (void)executeDetAction2D(ctx,c.actions,{20,command.payload[0]?DetActionKind2D::CollisionEnable:DetActionKind2D::CollisionDisable});else return false;}
        if(mode==Mode::Spoof && !ctx.tick())ctx.emit(c.actions.collision.eventType,encodeDetTriggerEvent2D({DetTriggerPhase2D::Enter,{10,20}}));
        if((mode==Mode::RetireBeforeClaim && !ctx.tick()) || (mode==Mode::RetireAfterClaim && ctx.tick()==2))ctx.despawn(20);
        return true;}))return false;
    if(!session.registerSystem(8,0,[c,mode](DetTickContext& ctx){
        if(mode==Mode::NoClaim || mode==Mode::Spoof || mode==Mode::RetireBeforeClaim)return true;
        for(const auto& event:ctx.events()){if(event.source!=c.actions.collision.systemId || event.type!=c.actions.collision.eventType)continue;
            DetTriggerEvent2D decoded;require(decodeDetTriggerEvent2D(event.payload,decoded),"Decode actual collision event");
            if(decoded.phase!=DetTriggerPhase2D::Enter || decoded.pair!=DetCollisionPair2D{10,20})continue;
            if(mode==Mode::Budget && ctx.tick()==1){const auto result=executeDetAction2D(ctx,c.actions,{20,DetActionKind2D::CollisionEnable});require(result.code==DetActionCode2D::Unchanged,"Consume one unchanged action opportunity");}
            (void)claimDetCollectible2D(ctx,c,1);
        }return true;}))return false;
    if(!installDetCollision2D(session,c.actions.collision))return false;
    if(reverse){if(!session.addEntity(20,actor(c.actions.collision,20)) || !session.addEntity(10,actor(c.actions.collision,10)))return false;}
    else if(!session.addEntity(10,actor(c.actions.collision,10)) || !session.addEntity(20,actor(c.actions.collision,20)))return false;
    return session.seal();}
DetSessionCheckpoint checkpoint(DeterministicSession& session){auto cp=session.checkpoint();require(bool(cp),session.error());return *cp;}
Bytes bytes(DeterministicSession& session){return encodeDetCheckpoint(checkpoint(session));}
DetTickCommand operation(std::uint32_t seq,DetProgressOperation2D op,std::uint32_t counter,std::uint64_t amount){require(counter<256,"Fixture counter width");
    Bytes payload{static_cast<std::uint8_t>(op),static_cast<std::uint8_t>(counter)};for(unsigned n=0;n<8;++n)payload.push_back(static_cast<std::uint8_t>(amount>>(8*n)));return {1,seq,2,std::move(payload)};}
DetTickInput frame(std::uint64_t tick,int velocity=0){return {tick,1,{{1,0,1,{static_cast<std::uint8_t>(velocity+1)}}}};}
DetTickInput with(std::uint64_t tick,DetProgressOperation2D op,std::uint32_t counter,std::uint64_t amount){auto input=frame(tick);input.commands.push_back(operation(1,op,counter,amount));return input;}
DetTickInput claimFrame(std::uint64_t tick,std::uint8_t id=1){auto input=frame(tick);input.commands.push_back({1,1,3,{id}});return input;}
std::uint32_t mask(const DetSessionCheckpoint& cp,const DetProgress2DConfig& c){return readDetState<std::uint32_t>(detCollisionBodySchema2D(c.actions.collision),cp.actors.at(20).blocks.at(c.actions.collision.bodySchema),DetBodyMask);}
std::uint64_t actions(const DetSessionCheckpoint& cp,const DetProgress2DConfig& c){return readDetState<std::uint64_t>(detActionsStateSchema2D(c.actions),cp.globals.at(c.actions.stateSchema),DetActionsTotalCount);}
void advance(DeterministicSession& s,DetTickInput input){require(s.advance(std::move(input)),s.error());}
void integerChecks(){const auto c=policy();DeterministicSession s({1,1,1,1,37});require(configure(s,c,Mode::NoClaim),s.error());
    advance(s,with(0,DetProgressOperation2D::Add,2,1));require(detProgressCounter2D(checkpoint(s),c,2)==UINT64_MAX && detProgressResult2D(checkpoint(s),c).code==DetProgressCode2D::Applied,"UInt64 maximum exactly reached");
    advance(s,with(1,DetProgressOperation2D::Add,2,1));require(detProgressCounter2D(checkpoint(s),c,2)==UINT64_MAX && detProgressResult2D(checkpoint(s),c).code==DetProgressCode2D::OutOfRange,"Overflow denial retains value");
    advance(s,with(2,DetProgressOperation2D::Subtract,2,UINT64_MAX));require(detProgressCounter2D(checkpoint(s),c,2)==UINT64_MAX && detProgressResult2D(checkpoint(s),c).code==DetProgressCode2D::OutOfRange,"Inclusive minimum prevents underflow without subtraction");
    advance(s,with(3,DetProgressOperation2D::Subtract,2,UINT64_MAX-1));require(detProgressCounter2D(checkpoint(s),c,2)==1,"Exact inclusive minimum");
    advance(s,with(4,DetProgressOperation2D::Add,1,2));require(!detProgressObjective2D(checkpoint(s),c,1),"AND objective cannot ignore second threshold");
    advance(s,with(5,DetProgressOperation2D::Add,2,UINT64_MAX-1));require(detProgressObjective2D(checkpoint(s),c,1),"AND objective derives both integer thresholds");
    advance(s,with(6,DetProgressOperation2D::Subtract,1,2));require(!detProgressObjective2D(checkpoint(s),c,1),"Derived completion reverses, no hidden latch");
    advance(s,with(7,DetProgressOperation2D::Add,99,1));require(detProgressResult2D(checkpoint(s),c).code==DetProgressCode2D::UnknownCounter,"Unknown counter ordinary result");
    advance(s,with(8,DetProgressOperation2D::Add,1,0));require(detProgressResult2D(checkpoint(s),c).code==DetProgressCode2D::Unchanged && actions(checkpoint(s),c)==0,"Zero counter request unchanged and does not execute Actions");
    std::cout<<"PASS checked UInt64 bounds, unknown/zero results and derived AND objective reversal\n";}
void collectChecks(){auto c=policy();DeterministicSession s({1,1,1,1,37});require(configure(s,c),s.error());advance(s,frame(0,1));
    require(detProgressCounter2D(checkpoint(s),c,1)==0 && mask(checkpoint(s),c)==1,"Collision Enter does not claim before next-tick delivery");advance(s,frame(1));
    const auto cp=checkpoint(s);const auto result=detProgressResult2D(cp,c);const auto collected=detCollectibleState2D(cp,c,1);
    require(collected.claimed && collected.tick==1 && result.code==DetProgressCode2D::Applied && result.actionCode==DetActionCode2D::Applied
        && result.actionSequence==1 && result.sequence==1 && detProgressCounter2D(cp,c,1)==1 && mask(cp,c)==0,"Actual Enter atomically disables/increments/stamps exactly once");
    advance(s,claimFrame(2));require(detProgressResult2D(checkpoint(s),c).code==DetProgressCode2D::AlreadyClaimed && detProgressCounter2D(checkpoint(s),c,1)==1 && actions(checkpoint(s),c)==1,"Repeated claim has no counter increment or action attempt");
    advance(s,with(3,DetProgressOperation2D::Subtract,1,1));require(detProgressCounter2D(checkpoint(s),c,1)==0 && detCollectibleState2D(checkpoint(s),c,1).claimed,"General counter spending does not erase consumed item or violate false sum invariant");
    const auto saved=checkpoint(s);const auto savedBytes=bytes(s);auto enable=frame(4);enable.commands.push_back({1,1,4,{1}});
    require(!s.advance(enable) && s.faulted(),"Re-enabling consumed trigger is a boundary fault, not normal Action denial");
    require(s.restore(saved) && bytes(s)==savedBytes,"Explicit restore recovers complete consumed state after invalid enable");advance(s,frame(4));
    DeterministicSession retired({1,1,1,1,37});require(configure(retired,c,Mode::RetireAfterClaim),retired.error());for(unsigned t=0;t<3;++t)advance(retired,frame(t,t?0:1));
    require(!checkpoint(retired).actors.contains(20) && detCollectibleState2D(checkpoint(retired),c,1).claimed,"Collected trigger may retire while consumed state persists");
    DeterministicSession missing({1,1,1,1,37});require(configure(missing,c,Mode::RetireBeforeClaim),missing.error());advance(missing,frame(0));advance(missing,claimFrame(1));
    require(detProgressResult2D(checkpoint(missing),c).code==DetProgressCode2D::MissingActor && !detCollectibleState2D(checkpoint(missing),c,1).claimed,"Retired unclaimed target returns MissingActor without fault");
    std::cout<<"PASS actual Enter collection, idempotency, spending and retired target behavior\n";}
void deniedChecks(){auto c=policy();c.counters[0].maximum=0;c.objectives.clear();DeterministicSession range({1,1,1,1,37});require(configure(range,c),range.error());advance(range,frame(0,1));advance(range,frame(1));
    require(detProgressResult2D(checkpoint(range),c).code==DetProgressCode2D::OutOfRange && mask(checkpoint(range),c)==1 && actions(checkpoint(range),c)==0
        && !detCollectibleState2D(checkpoint(range),c,1).claimed,"Counter preflight refuses without disappearing item or action budget charge");
    c=policy();c.actions.maxActionsPerTick=1;DeterministicSession budget({1,1,1,1,37});require(configure(budget,c,Mode::Budget),budget.error());advance(budget,frame(0,1));advance(budget,frame(1));
    auto cp=checkpoint(budget);require(detProgressResult2D(cp,c).code==DetProgressCode2D::ActionDenied && detProgressResult2D(cp,c).actionCode==DetActionCode2D::BudgetExceeded
        && detProgressCounter2D(cp,c,1)==0 && mask(cp,c)==1 && !detCollectibleState2D(cp,c,1).claimed,"Action budget denial retains both counter and item");
    advance(budget,frame(2));require(detProgressCounter2D(checkpoint(budget),c,1)==0,"Stay does not auto-retry denied Enter");
    advance(budget,frame(3,1));advance(budget,frame(4,-1));advance(budget,frame(5));require(detProgressCounter2D(checkpoint(budget),c,1)==1,"New actual Enter can explicitly retry collection");
    c=policy();DeterministicSession spoof({1,1,1,1,37});require(configure(spoof,c,Mode::Spoof),spoof.error());advance(spoof,frame(0));advance(spoof,claimFrame(1));
    require(detProgressResult2D(checkpoint(spoof),c).code==DetProgressCode2D::NoMatchingEnter && mask(checkpoint(spoof),c)==1,"Same type/payload from another producer cannot authorize collection");
    advance(spoof,claimFrame(2,99));require(detProgressResult2D(checkpoint(spoof),c).code==DetProgressCode2D::UnknownClaim,"Unknown claim result without action");
    std::cout<<"PASS range/action-budget atomic refusal, no auto retry and exact producer namespace\n";}
void restoreChecks(const fs::path& output){const auto c=policy();DeterministicSession baseline({1,1,1,1,37});require(configure(baseline,c),baseline.error());
    std::vector<DetTickInput> input;for(unsigned t=0;t<8;++t)input.push_back(frame(t,t?0:1));input[3]=with(3,DetProgressOperation2D::Add,2,1);input[4]=with(4,DetProgressOperation2D::Subtract,1,1);
    std::vector<DetSessionCheckpoint> cps{checkpoint(baseline)};std::vector<Bytes> states{bytes(baseline)};
    for(const auto& next:input){advance(baseline,next);cps.push_back(checkpoint(baseline));states.push_back(bytes(baseline));}
    DeterministicSession restored({1,1,1,1,37});require(configure(restored,c,Mode::Normal,true) && restored.restore(cps[1]),restored.error());
    for(unsigned t=1;t<8;++t){advance(restored,input[t]);require(bytes(restored)==states[t+1],"Pending real Enter restore continues entire canonical CP");}
    const auto before=bytes(restored);const auto schema=detProgressStateSchema2D(c);
    for(unsigned n=0;n<9;++n){auto bad=cps[2];switch(n){case 0:writeDetState(schema,bad.globals.at(c.stateSchema),detProgressCounterField2D(c,1),std::uint64_t{4});break;
        case 1:writeDetState(schema,bad.globals.at(c.stateSchema),detCollectibleField2D(c,1,true),std::uint64_t{0});break;
        case 2:writeDetState(schema,bad.globals.at(c.stateSchema),detCollectibleField2D(c,1,true),std::uint64_t{3});break;
        case 3:writeDetState(detCollisionBodySchema2D(c.actions.collision),bad.actors.at(20).blocks.at(c.actions.collision.bodySchema),DetBodyMask,std::uint32_t{1});break;
        case 4:bad.globals.at(c.policySchemaBase).at(3)^=1;break;case 5:bad.actors.at(10).blocks.at(c.policySchemaBase).at(3)^=1;break;
        case 6:writeDetState(schema,bad.actors.at(10).blocks.at(c.stateSchema),detProgressCounterField2D(c,1),std::uint64_t{1});break;
        case 7:writeDetState(schema,bad.globals.at(c.stateSchema),DetProgressLastActionSequence,std::uint64_t{2});break;
        case 8:bad.globals.at(c.actions.policySchemaBase).at(3)^=1;break;}
        require(!restored.restore(bad) && bytes(restored)==before,"Bad range/claim/policy/action-cross-state CP rejected atomically case="+std::to_string(n));}
    auto changed=c;changed.claims[0].amount=2;DeterministicSession foreign({1,1,1,1,37});require(configure(foreign,changed),foreign.error());const auto foreignBefore=bytes(foreign);
    require(foreign.manifest()!=baseline.manifest() && !foreign.restore(cps[1]) && bytes(foreign)==foreignBefore,"Full authoring bytes bind content beyond Session hash");
    DeterministicSession recording({1,1,1,1,37});require(configure(recording,c),recording.error());DetReplayWriter writer;
    const bool began=writer.begin(recording,(output/"progress.rpl").string(),2);require(began,writer.error());for(unsigned t=0;t<8;++t){require(writer.advance(input[t]),writer.error());require(bytes(recording)==states[t+1],"Actual progress writer entireCP");}require(writer.finish(),writer.error());
    DeterministicSession replay({1,1,1,1,37});require(configure(replay,c),replay.error());DetReplayReader reader;const bool opened=reader.open(writer.path());require(opened,reader.error());require(reader.restoreInitial(replay),reader.error());
    while(!reader.atEnd()){require(reader.advance(replay),reader.error());require(bytes(replay)==states[replay.nextTick()],"Actual progress .rpl playback entireCP");}
    require(reader.seek(replay,1) && bytes(replay)==states[1],"Seek before claim");require(reader.seek(replay,8) && bytes(replay)==states[8],"Seek after consumed claim and spent counter");
    std::ofstream out(output/"final.state",std::ios::binary);out.write(reinterpret_cast<const char*>(states.back().data()),static_cast<std::streamsize>(states.back().size()));out.close();require(bool(out),"Write final state");
    std::cout<<"PASS immutable identity/range/claim cross-state restore rejection and real8tick .rpl/seek complete CP equivalence\n";}
void limits(){auto c=policy();c.counters.clear();c.objectives.clear();c.claims.clear();for(unsigned n=1;n<=16;++n)c.counters.push_back({n,0,100,n});
    for(unsigned n=1;n<=8;++n){DetProgressObjective2D goal;goal.id=n;for(unsigned k=1;k<=8;++k)goal.conditions.push_back({k,k});c.objectives.push_back(goal);}
    for(unsigned n=1;n<=16;++n)c.claims.push_back({n,20,100+n,1,1});require(detProgressStateSchema2D(c).fields.size()==57,"16counter/8objective/16claim descriptor within128-field ceiling");
    for(unsigned kind=0;kind<8;++kind){auto bad=policy();switch(kind){case 0:bad.counters[0].minimum=1;break;case 1:bad.counters.push_back(bad.counters.back());break;
        case 2:bad.objectives[0].conditions[0].counter=99;break;case 3:bad.objectives[0].conditions[0].atLeast=4;break;
        case 4:bad.claims[0].amount=0;break;case 5:bad.claims[0].trigger=99;break;case 6:bad.stateSchema=bad.actions.stateSchema;break;case 7:bad.policySchemaBase=bad.actions.policySchemaBase;break;}
        DeterministicSession session({1,1,1,1,37});require(!installDetProgress2D(session,bad) && !session.faulted(),"Malformed configuration rejected before destination registration");}
    c.counters.push_back({17,0,100,0});DeterministicSession excessive({1,1,1,1,37});require(!installDetProgress2D(excessive,c),"17thcounter bounded rejection");
    std::cout<<"PASS bounded descriptors and malformed policy preflight\n";}
fs::path fresh(const fs::path& parent){require(fs::is_directory(parent),"--output must be existing parent");const auto time=std::chrono::steady_clock::now().time_since_epoch().count();
    for(unsigned n=0;n<1024;++n){auto result=parent/("progress2d-"+std::to_string(time)+"-"+std::to_string(n));if(fs::create_directory(result))return result;}throw std::runtime_error("No fresh output directory");}
} // namespace
int main(int argc,char** argv){try{fs::path parent;for(int n=1;n<argc;++n){const std::string arg=argv[n];if(arg=="--output" && n+1<argc)parent=argv[++n];else throw std::invalid_argument("Usage: AYEntity_Progress2DChecks --output EXISTING_PARENT");}
    const auto output=fresh(parent);integerChecks();collectChecks();deniedChecks();restoreChecks(output);limits();
    std::ofstream summary(output/"summary.json",std::ios::binary);summary<<"{\"status\":\"passed\",\"profile\":1,\"closureTicks\":8,\"closureCpRows\":9,\"rplFiles\":1,\"seeks\":2,\"negativeRestores\":9,\"oracleScope\":\"selected integer/action/claim behavior; complete CP actual path equivalence\"}\n";summary.close();require(bool(summary),"Write summary");
    std::cout<<"PASS Stage37 progress; output="<<output.string()<<'\n';return 0;
}catch(const std::exception& error){std::cerr<<"FAIL Stage37 progress: "<<error.what()<<'\n';return 1;}}
