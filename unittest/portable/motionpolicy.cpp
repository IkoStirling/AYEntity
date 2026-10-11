// design reference: AYEntity/design.md Stage28; classified prescribed-motion recovery.
// Dyadic hand cases check individual behavior; full checkpoint closure compares
// executed paths and does not claim an independent golden for every byte.
#include <AYEntity/DeterministicCollision2D.h>
#include <AYEntity/DeterministicCollisionQuery2D.h>
#include <AYEntity/DeterministicReplay.h>
#include <AYEntity/DeterministicRollback.h>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
using namespace ayt::entity;
namespace fs=std::filesystem;
namespace {
using D=ayt::math::DetFloat32;using V=ayt::math::DetVec2;using Bytes=std::vector<std::uint8_t>;
using Actors=std::vector<std::pair<SimEntityId,DetActorState>>;
void require(bool ok,const std::string& why){if(!ok)throw std::runtime_error(why);}
D q(int n,int den=4){return D::fromInt(n)/D::fromInt(den);}
V v(int x,int y){return V::fromInts(x,y);}
DetCollision2DConfig policy(){DetCollision2DConfig c;c.extended=true;c.motionPolicy=DetCollisionMotionPolicy2D::StopPrescribedOnConflict;
    c.maxContactIterations=8;c.maxBodies=16;c.maxTriggerPairs=8;return c;}
DetActorState actor(const DetCollision2DConfig& c,DetBodyMode2D mode,V position,V half,V velocity={}){
    DetActorState a;DetSimTransformComponent p;require(p.setPosition({position.x,position.y,{}}),"pose");a.pose=p.snapshot();
    a.blocks[c.bodySchema]=detCollisionBodyState2D(c,{mode,half,{},velocity});return a;}
DetSessionCheckpoint cp(DeterministicSession& s){auto state=s.checkpoint();require(bool(state),s.error());return *state;}
Bytes bytes(DeterministicSession& s){return encodeDetCheckpoint(cp(s));}
template<class T>T field(const DetSessionCheckpoint& state,const DetCollision2DConfig& c,SimEntityId id,std::uint32_t f){
    return readDetState<T>(detCollisionBodySchema2D(c),state.actors.at(id).blocks.at(c.bodySchema),f);}
void expect(const DetSessionCheckpoint& state,const DetCollision2DConfig& c,SimEntityId id,V p,V velocity,
    DetCollisionMotionResult2D result,SimEntityId other,std::uint32_t axis,const std::string& why){
    const auto& actual=state.actors.at(id).pose.position;
    require(actual[0]==p.x.bits() && actual[1]==p.y.bits(),why+" position");
    require(field<V>(state,c,id,DetBodyVelocity)==velocity,why+" velocity");
    require(field<std::uint32_t>(state,c,id,DetBodyMotionResult)==static_cast<std::uint32_t>(result),why+" result");
    require(field<DetEntityRef>(state,c,id,DetBodyMotionOther).value==other && field<std::uint32_t>(state,c,id,DetBodyMotionAxis)==axis,why+" identity/axis");
    require(field<std::uint64_t>(state,c,id,DetBodyMotionResultTick)==(result==DetCollisionMotionResult2D::Clear?0:state.nextTick),why+" stamp");
}
bool configure(DeterministicSession& s,const DetCollision2DConfig& c,Actors actors,bool reverse=false,bool commanded=false){
    auto input=[c,commanded](DetTickContext& ctx){
        if(!commanded)return ctx.input().commands.empty();
        for(const auto& command:ctx.input().commands){
            if(command.source!=1 || command.sequence!=0 || command.type!=1 || command.payload.size()!=1 || command.payload[0]>1)return false;
            ctx.write(20,c.bodySchema,DetBodyVelocity,V{{},command.payload[0]?D::fromInt(1):D{}});
        }return true;};
    auto effects=[c,commanded](DetTickContext& ctx){if(commanded)for(auto id:ctx.entities()){
        const auto result=ctx.read<std::uint32_t>(id,c.bodySchema,DetBodyMotionResult);
        if(result){const std::uint8_t payload[]{std::uint8_t(id),std::uint8_t(result),std::uint8_t(ctx.read<std::uint32_t>(id,c.bodySchema,DetBodyMotionAxis))};ctx.emit(0x50001,payload);}
    }return true;};
    if(reverse){if(!s.registerSystem(50,0,effects) || !installDetCollision2D(s,c) || !s.registerSystem(10,0,input))return false;std::reverse(actors.begin(),actors.end());}
    else if(!s.registerSystem(10,0,input) || !installDetCollision2D(s,c) || !s.registerSystem(50,0,effects))return false;
    for(auto& [id,a]:actors)if(!s.addEntity(id,std::move(a)))return false;return s.seal();
}
Actors crushed(const DetCollision2DConfig& c){return {
    {10,actor(c,DetBodyMode2D::Kinematic,v(0,1),{q(2),q(2)},v(0,1))},
    {20,actor(c,DetBodyMode2D::MovingObstacle,v(0,0),{D::fromInt(2),q(2)},v(0,1))},
    {30,actor(c,DetBodyMode2D::Static,v(0,2),{D::fromInt(2),q(2)})},
    {40,actor(c,DetBodyMode2D::MovingObstacle,v(8,0),{q(2),q(2)},v(0,1))}};}
DetTickInput input(std::uint64_t tick){return {tick,1,{{1,0,1,{std::uint8_t(tick%2==0)}}}};}
void directed(){const auto c=policy();
    {
        DeterministicSession s({1,1,1,4,28});Actors a{{20,actor(c,DetBodyMode2D::MovingObstacle,v(0,0),{q(2),q(2)},v(4,1))},
            {30,actor(c,DetBodyMode2D::Static,v(2,0),{q(2),q(2)})},
            {40,actor(c,DetBodyMode2D::MovingObstacle,v(8,0),{q(2),q(2)},v(0,1))}};
        require(configure(s,c,a),s.error());require(s.advance({0,1,{}}),s.error());const auto state=cp(s);
        expect(state,c,20,v(0,0),{},DetCollisionMotionResult2D::Blocked,30,0,"MO/static freezes whole velocity");
        expect(state,c,40,{D::fromInt(8),q(1)},v(0,1),DetCollisionMotionResult2D::Clear,0,0,"independent mover progresses");
    }
    {
        DeterministicSession s({1,1,1,4,28});Actors a{{20,actor(c,DetBodyMode2D::MovingObstacle,v(0,0),{q(2),q(2)},v(4,0))},
            {30,actor(c,DetBodyMode2D::MovingObstacle,v(2,0),{q(2),q(2)},v(-4,0))}};
        require(configure(s,c,a),s.error());require(s.advance({0,1,{}}),s.error());const auto state=cp(s);
        expect(state,c,20,v(0,0),{},DetCollisionMotionResult2D::Blocked,30,0,"MO/MO first");
        expect(state,c,30,v(2,0),{},DetCollisionMotionResult2D::Blocked,20,0,"MO/MO second");
    }
    {
        DeterministicSession s({1,1,1,4,28});Actors a{{10,actor(c,DetBodyMode2D::Kinematic,v(0,0),{q(2),q(2)},v(4,0))},
            {30,actor(c,DetBodyMode2D::Static,v(2,0),{q(2),q(2)})}};
        require(configure(s,c,a),s.error());require(s.advance({0,1,{}}),s.error());
        expect(cp(s),c,10,v(1,0),{},DetCollisionMotionResult2D::Blocked,30,0,"ordinary wall clip");
    }
    {
        DeterministicSession s({1,1,1,4,28});require(configure(s,c,crushed(c)),s.error());require(s.advance({0,1,{}}),s.error());auto state=cp(s);
        expect(state,c,10,v(0,1),{},DetCollisionMotionResult2D::CrushPrevented,20,1,"roof actor pinch");
        expect(state,c,20,v(0,0),{},DetCollisionMotionResult2D::CrushPrevented,10,1,"roof platform pinch");
        expect(state,c,40,{D::fromInt(8),q(1)},v(0,1),DetCollisionMotionResult2D::Clear,0,0,"roof independent mover");
        const auto saved=bytes(s);auto bad=state;const auto schema=detCollisionBodySchema2D(c);
        writeDetState(schema,bad.actors.at(20).blocks.at(c.bodySchema),DetBodyVelocity,v(0,1));
        require(!s.restore(bad) && bytes(s)==saved,"malformed stopped velocity restore atomic");
        bad=state;writeDetState(schema,bad.actors.at(20).blocks.at(c.bodySchema),DetBodyMotionResultTick,std::uint64_t{0});
        require(!s.restore(bad) && bytes(s)==saved,"malformed result stamp restore atomic");
        bad=state;writeDetState(schema,bad.actors.at(20).blocks.at(c.bodySchema),DetBodyMotionOther,DetEntityRef{999});
        require(!s.restore(bad) && bytes(s)==saved,"unknown result identity restore atomic");
        bool rejected=false;try{DetCollisionQuery2D query(bad,c);(void)query;}catch(const std::exception&){rejected=true;}require(rejected,"query validates profile5 result");
        require(s.advance({1,1,{}}),s.error());
        expect(cp(s),c,20,v(0,0),{},DetCollisionMotionResult2D::Clear,0,0,"result clears next tick");
    }
    {
        auto legacy=c;legacy.motionPolicy=DetCollisionMotionPolicy2D::Fault;
        DeterministicSession s({1,1,1,4,28});require(configure(s,legacy,crushed(legacy)),s.error());const auto before=cp(s);
        require(!s.advance({0,1,{}}),"legacy pinch must fault");require(s.restore(before),s.error());require(bytes(s)==encodeDetCheckpoint(before),"legacy fault explicit restore");
    }
    {
        auto invalid=c;invalid.maxTriggerPairs=60;DeterministicSession s({1,1,1,4,28});require(!installDetCollision2D(s,invalid),"profile5 capacity rejects before writes");
        require(configure(s,c,crushed(c)),s.error());
    }
    {
        auto limited=c;limited.maxContactIterations=1;DeterministicSession s({1,1,1,4,28});
        Actors a{{10,actor(limited,DetBodyMode2D::Kinematic,v(0,0),{q(2),q(2)},v(4,1))},
            {30,actor(limited,DetBodyMode2D::Static,v(1,0),{q(2),q(2)})}};
        require(configure(s,limited,a),s.error());const auto before=cp(s);
        require(!s.advance({0,1,{}}),"unclassified budget exhaustion remains fault");require(s.restore(before),s.error());
    }
    {
        DeterministicSession s({1,1,4,1,28});auto a=crushed(c);
        a[1].second.blocks[c.bodySchema]=detCollisionBodyState2D(c,{DetBodyMode2D::MovingObstacle,{D::fromInt(2),q(2)},{},{D{},D::fromBits(0x7f7fffff)}});
        require(configure(s,c,a),s.error());require(!s.advance({0,1,{}}),"unrepresentable original relative motion remains fault");
    }
    std::cout<<"PASS direct motion, opposed pinch, independent motion and rejection boundaries\n";
}
struct Evidence{std::vector<DetSessionCheckpoint> states;std::uint64_t rollbacks=0,replayed=0;};
Evidence closure(const fs::path& directory){const auto c=policy();Evidence result;
    DeterministicSession baseline({1,1,1,4,28}),reverse({1,1,1,4,28});
    require(configure(baseline,c,crushed(c),false,true),baseline.error());require(configure(reverse,c,crushed(c),true,true),reverse.error());
    result.states.push_back(cp(baseline));require(bytes(reverse)==encodeDetCheckpoint(result.states.back()),"initial reversed closure");
    for(std::uint64_t t=0;t<8;++t){require(baseline.advance(input(t)),baseline.error());require(reverse.advance(input(t)),reverse.error());
        result.states.push_back(cp(baseline));require(bytes(reverse)==encodeDetCheckpoint(result.states.back()),"reverse closure tick "+std::to_string(t));}
    require(reverse.restore(result.states[4]),reverse.error());for(std::uint64_t t=4;t<8;++t){require(reverse.advance(input(t)),reverse.error());require(bytes(reverse)==encodeDetCheckpoint(result.states[t+1]),"restore continuation");}
    DeterministicSession predicted({1,1,1,4,28});require(configure(predicted,c,crushed(c),true,true),predicted.error());
    DetRollbackConfig rc;rc.prediction={{1,DetPredictionMode::Omit}};rc.maxPredictionTicks=8;DeterministicRollback owner(predicted,rc);
    std::vector<DetConfirmedEvent> events;auto drain=[&]{auto current=owner.takeConfirmedEvents();events.insert(events.end(),current.begin(),current.end());};
    for(std::uint64_t t=0;t<8;++t){require(owner.advance(),owner.error());if(t>=2){require(owner.submit(1,input(t-2)),owner.error());require(owner.submit(1,input(t-2)),owner.error());}drain();}
    for(std::uint64_t t=6;t<8;++t){require(owner.submit(1,input(t)),owner.error());drain();}
    require(owner.confirmedNextTick()==8 && bytes(predicted)==encodeDetCheckpoint(result.states.back()),"late rollback complete checkpoint closure");
    require(owner.rollbackCount()>0 && owner.replayedTicks()>0,"late inputs genuinely replay");require(events.size()==8,"exactly once confirmed motion effects");
    std::size_t index=0;for(std::uint64_t t=0;t<8;t+=2)for(std::uint32_t sequence=0;sequence<2;++sequence){const auto& e=events[index++];
        require(e.epoch==1 && e.tick==t && e.command==DetTickCommand{50,sequence,0x50001,{std::uint8_t(sequence?20:10),2,1}},"confirmed effect identity and bytes");}
    require(owner.takeConfirmedEvents().empty(),"confirmed effects drained once");result.rollbacks=owner.rollbackCount();result.replayed=owner.replayedTicks();
    DeterministicSession recording({1,1,1,4,28});require(configure(recording,c,crushed(c),false,true),recording.error());DetReplayWriter writer;
    require(writer.begin(recording,(directory/"motion").string(),2),writer.error());for(std::uint64_t t=0;t<8;++t){require(writer.advance(input(t)),writer.error());require(bytes(recording)==encodeDetCheckpoint(result.states[t+1]),"recording closure");}require(writer.finish(),writer.error());
    DeterministicSession playback({1,1,1,4,28});require(configure(playback,c,crushed(c),true,true),playback.error());DetReplayReader reader;
    require(reader.open(writer.path()) && reader.restoreInitial(playback),reader.error());for(std::uint64_t t=0;t<8;++t){require(reader.advance(playback),reader.error());require(bytes(playback)==encodeDetCheckpoint(result.states[t+1]),"real file playback closure");}
    require(reader.atEnd(),"sealed file end");for(auto tick:{0u,4u,8u}){require(reader.seek(playback,tick),reader.error());require(bytes(playback)==encodeDetCheckpoint(result.states[tick]),"real file seek closure");}
    std::cout<<"PASS8tick reverse/restore/late rollback/confirmed effects/sealed replay closure\n";return result;
}
fs::path fresh(const fs::path& parent){require(fs::is_directory(parent),"existing --output parent required");const auto stamp=std::chrono::steady_clock::now().time_since_epoch().count();
    for(unsigned i=0;i<1024;++i){auto path=parent/("motionpolicy-"+std::to_string(stamp)+"-"+std::to_string(i));if(fs::create_directory(path))return path;}throw std::runtime_error("fresh output unavailable");}
void write(const fs::path& file,const std::string& text){std::ofstream stream(file,std::ios::binary);stream<<text;require(bool(stream),"write artifact");}
}
int main(int argc,char** argv){fs::path directory;try{fs::path parent=fs::temp_directory_path();if(argc==3 && std::string(argv[1])=="--output")parent=argv[2];else if(argc!=1)return 2;
    directory=fresh(parent);write(directory/"progress.json","{\"status\":\"running\",\"phase\":\"directed\"}\n");directed();const auto result=closure(directory);
    std::ostringstream out;out<<"{\"schema\":1,\"status\":\"passed\",\"collisionProfile\":5,\"directedGroups\":8,\"ticks\":\"8\",\"checkpointRows\":\"9\",\"confirmedEvents\":\"8\",\"rollbacks\":\""<<result.rollbacks<<"\",\"replayedTicks\":\""<<result.replayed<<"\",\"replayFiles\":1,\"seeks\":3,\"finalHash\":\""<<detCheckpointHash(result.states.back())<<"\"}\n";
    write(directory/"summary.json",out.str());write(directory/"progress.json",out.str());std::cout<<out.str();return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';if(!directory.empty())try{write(directory/"failure.txt",e.what());write(directory/"progress.json","{\"status\":\"failed\"}\n");}catch(...){}return 1;}}
