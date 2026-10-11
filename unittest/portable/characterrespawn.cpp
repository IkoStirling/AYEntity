// design reference: AYEntity/design.md Stage44. Short hand-calculated cases,
// then complete execution-equivalence closure; no independent manifest golden.
#include <AYEntity/DeterministicCharacterRespawn2D.h>
#include <AYEntity/DeterministicReplay.h>
#include <AYEntity/DeterministicRollback.h>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace {
using namespace ayt::entity;namespace fs=std::filesystem;
using D=ayt::math::DetFloat32;using V=ayt::math::DetVec2;using Bytes=std::vector<std::uint8_t>;
using Actors=std::vector<std::pair<SimEntityId,DetActorState>>;
void require(bool value,const std::string& why){if(!value)throw std::runtime_error(why);}
D q(int n,int denominator=4){return D::fromInt(n)/D::fromInt(denominator);}
DetCharacterRespawn2DConfig policy(){DetCharacterRespawn2DConfig c;auto& k=c.character;
    k.advanced=true;k.collision.oneWayPlatforms=true;k.collision.characterRespawn=true;
    k.gravity={};k.acceleration=k.deceleration=D::fromInt(32);k.jumpSpeed=D::fromInt(4);
    k.coyoteTicks=2;k.jumpBufferTicks=2;k.dropThroughTicks=2;k.crouchEnabled=true;
    k.collision.maxBodies=16;k.collision.maxTriggerPairs=8;
    c.spawns={{1,10,{D::fromInt(4),q(2)}},{2,10,{D::fromInt(8),q(2)}}};return c;}
DetTypedStateSchema characterSchema(const DetCharacterRespawn2DConfig& c){const auto& k=c.character;
    DetTypedStateSchema s{k.stateSchema,8,{{10,false},{20,false},{30,DetEntityRef{}},{40,V{}},{50,V{}},
        {60,D{}},{70,false},{80,V{}},{90,V{}},{100,false},{110,false},{120,std::uint64_t{}},
        {130,std::uint32_t{}},{140,std::uint32_t{}},{150,false},{160,false},{170,false},{180,false},
        {190,false},{200,false},{210,V{}},{220,V{}},{230,std::uint32_t{}},{240,D{}},{250,false},{260,false},{270,std::uint64_t{}}}};
    if(k.collision.stepUp)s.fields.push_back({280,false});if(k.collision.staticSlopes)s.fields.push_back({290,V{}});return s;}
DetTypedStateSchema respawnSchema(const DetCharacterRespawn2DConfig& c){return {c.stateSchema,1,{{10,std::uint32_t{}},{20,std::uint64_t{}},
    {30,std::uint32_t{}},{40,std::uint32_t{}},{50,std::uint64_t{}},{60,false},{70,false}}};}
template<class T>T character(const DetSessionCheckpoint& cp,const DetCharacterRespawn2DConfig& c,std::uint32_t field){
    return readDetState<T>(characterSchema(c),cp.actors.at(10).blocks.at(c.character.stateSchema),field);}
template<class T>T body(const DetSessionCheckpoint& cp,const DetCharacterRespawn2DConfig& c,std::uint32_t field,SimEntityId actor=10){
    return readDetState<T>(detCollisionBodySchema2D(c.character.collision),cp.actors.at(actor).blocks.at(c.character.collision.bodySchema),field);}
DetSessionCheckpoint checkpoint(DeterministicSession& s){const auto cp=s.checkpoint();require(bool(cp),s.error());return *cp;}
Bytes bytes(DeterministicSession& s){return encodeDetCheckpoint(checkpoint(s));}
DetActorState actor(const DetCharacterRespawn2DConfig& c,DetBodyMode2D mode,V position,V half,bool hero=false,bool trigger=false,
    bool oneWay=false,DetCollisionShape2D shape=DetCollisionShape2D::Aabb,std::uint32_t mask=0){
    DetActorState a;DetSimTransformComponent pose;require(pose.setPosition({position.x,position.y,{}}),"fixture pose");a.pose=pose.snapshot();
    a.blocks[c.character.collision.bodySchema]=detCollisionBodyState2D(c.character.collision,{mode,half,{}, {},hero?1u:(trigger?4u:2u),
        mask?mask:(hero?6u:1u),trigger,oneWay,{},shape});
    if(hero){DetCharacterState2D state;state.enabled=true;a.blocks[c.character.stateSchema]=detCharacterState2D(c.character,state);}return a;}
Actors scene(const DetCharacterRespawn2DConfig& c,bool triggers=true){Actors a{
    {10,actor(c,DetBodyMode2D::Kinematic,{{},q(2)},{q(1),q(2)},true)},
    {20,actor(c,DetBodyMode2D::Static,{D::fromInt(4),-q(1)},{D::fromInt(12),q(1)})},
    {30,actor(c,DetBodyMode2D::Static,{D::fromInt(8),q(2)},{q(2),q(2)})}};
    if(triggers)for(const auto [id,x]:{std::pair{60,0},std::pair{61,2},std::pair{62,4}})
        a.emplace_back(id,actor(c,DetBodyMode2D::Static,{D::fromInt(x),q(2)},{q(1),q(2)},false,true));return a;}
bool configure(DeterministicSession& s,const DetCharacterRespawn2DConfig& c,Actors a,bool reverse=false,bool fill=false,bool enableExtra=false){
    if(fill && !s.registerSystem(5,0,[](DetTickContext& ctx){if(ctx.tick()==1)for(unsigned n=0;n<1023;++n)ctx.emit(77);return true;}))return false;
    auto input=[c,enableExtra](DetTickContext& ctx){
        if(enableExtra && ctx.tick()==1)ctx.write(64,c.character.collision.bodySchema,DetBodyMask,std::uint32_t{1});
        for(const auto& command:ctx.input().commands){if(command.source!=1 || command.type!=1 || command.payload.size()!=5)return false;
            const auto& p=command.payload;for(unsigned i=2;i<5;++i)if(p[i]>1)return false;
            const int speed=p[0]<128?int(p[0]):int(p[0])-256;
            const auto entities=ctx.entities();if(std::binary_search(entities.begin(),entities.end(),SimEntityId{10})
                && ctx.read<bool>(10,c.character.stateSchema,DetCharacterEnabled))
                detCharacterInput2D(ctx,10,c.character,{q(speed),bool(p[2]),bool(p[3]),bool(p[4])});
            if(p[1]){const auto request=detCharacterRespawnRequest2D(ctx,10,c,p[1]);
                if(p[1]==9)require(request==DetRespawnRequestCode2D::UnknownSpawn,"normal unknown spawn");
                else require(request==DetRespawnRequestCode2D::Queued || request==DetRespawnRequestCode2D::AlreadyRequested
                    || request==DetRespawnRequestCode2D::Unavailable,"bounded normal request");}}
        return true;};
    if(reverse){if(!installDetCharacterRespawn2D(s,c) || !s.registerSystem(10,0,input))return false;std::reverse(a.begin(),a.end());}
    else if(!s.registerSystem(10,0,input) || !installDetCharacterRespawn2D(s,c))return false;
    for(auto& [id,value]:a)if(!s.addEntity(id,std::move(value)))return false;return s.seal();}
DetTickInput input(std::uint64_t tick,int speed=0,unsigned spawn=0,bool jump=false,bool crouch=false,bool drop=false){
    return {tick,1,{{1,0,1,{std::uint8_t(speed),std::uint8_t(spawn),std::uint8_t(jump),std::uint8_t(crouch),std::uint8_t(drop)}}}};}
void advance(DeterministicSession& s,int speed=0,unsigned spawn=0,bool jump=false,bool crouch=false,bool drop=false){
    require(s.advance(input(s.nextTick(),speed,spawn,jump,crouch,drop)),s.error());}
V position(const DetSessionCheckpoint& cp){const auto& p=cp.actors.at(10).pose.position;return {D::fromBits(p[0]),D::fromBits(p[1])};}
void accepted(DeterministicSession& s,const DetCharacterRespawn2DConfig& c,std::uint32_t spawn=1){const auto cp=checkpoint(s);const auto r=detCharacterRespawnState2D(cp,10,c);
    require(r.result==DetRespawnResult2D::Accepted && r.spawn==spawn && r.resultTick==cp.nextTick,"explicit accepted result");
    require(!character<bool>(cp,c,20) && character<DetEntityRef>(cp,c,30).value==0 && character<V>(cp,c,40)==V{}
        && character<V>(cp,c,50)==V{} && character<std::uint64_t>(cp,c,120)==0 && character<std::uint32_t>(cp,c,130)==0
        && character<std::uint32_t>(cp,c,140)==0 && character<std::uint32_t>(cp,c,230)==0 && !character<bool>(cp,c,190)
        && body<V>(cp,c,DetBodyVelocity)==V{} && !body<DetEntityRef>(cp,c,DetBodyIgnoredOneWay).value,"accepted registered motion/support/windows clear");
    require(cp.actors.at(10).pose.position==cp.actors.at(10).pose.previousPosition && !cp.actors.at(10).pose.hasPreviousPosition,"no presentation interpolation chord");
    const auto& words=cp.actors.at(10).blocks.at(c.stateSchema);require(!readDetState<std::uint32_t>(respawnSchema(c),words,10)
        && !readDetState<std::uint64_t>(respawnSchema(c),words,20) && !readDetState<bool>(respawnSchema(c),words,60)
        && !readDetState<bool>(respawnSchema(c),words,70),"private pending/applied/pre protocol cleared");}
void directed(){const auto c=policy();
    {auto a=scene(c);a.emplace_back(63,actor(c,DetBodyMode2D::Static,{{},q(5)},{q(1,8),q(1,8)},false,true));
        DeterministicSession s({44,1,1,4,44});require(configure(s,c,std::move(a)),s.error());advance(s);
        advance(s,0,1,true,true,true);accepted(s,c);auto cp=checkpoint(s);require(position(cp)==V{D::fromInt(4),q(2)},"exact tick-end XY");
        require(character<bool>(cp,c,150) && character<bool>(cp,c,250) && character<bool>(cp,c,160) && character<bool>(cp,c,260),"held samples consumed, not reset edges");
        std::vector<std::pair<SimEntityId,DetTriggerPhase2D>> events;for(const auto& command:cp.pendingEvents){DetTriggerEvent2D e;
            require(command.source==30 && decodeDetTriggerEvent2D(command.payload,e),"real collision events");events.push_back({e.pair.second,e.phase});}
        require(std::find(events.begin(),events.end(),std::pair{SimEntityId{61},DetTriggerPhase2D::Enter})==events.end(),"no key pickup along teleport chord");
        require(std::find(events.begin(),events.end(),std::pair{SimEntityId{62},DetTriggerPhase2D::Enter})!=events.end(),"real destination endpoint enter");
        require(std::find(events.begin(),events.end(),std::pair{SimEntityId{60},DetTriggerPhase2D::Exit})!=events.end(),"old endpoint exit");
        require(std::count_if(events.begin(),events.end(),[](const auto& e){return e.first==63;})==2,"actual ordinary upward transient trace retained");
        advance(s,0,0,true,false,true);cp=checkpoint(s);require(position(cp)==V{D::fromInt(4),q(2)} && character<bool>(cp,c,20),"held jump/drop do not repeat after respawn");}
    {DeterministicSession s({44,1,1,4,44});require(configure(s,c,scene(c,false)),s.error());advance(s);advance(s,8,2);
        const auto cp=checkpoint(s);require(detCharacterRespawnState2D(cp,10,c).result==DetRespawnResult2D::Occupied
            && position(cp)==V{q(2),q(2)},"occupied destination retains hand-calculated baseline X.5");}
    {auto a=scene(c,false);a.emplace_back(40,actor(c,DetBodyMode2D::Static,{D::fromInt(4),q(7,8)},{q(2),q(1,8)}));
        DeterministicSession s({44,1,1,4,44});require(configure(s,c,std::move(a)),s.error());advance(s);advance(s,0,1,false,true);
        const auto cp=checkpoint(s);require(detCharacterRespawnState2D(cp,10,c).result==DetRespawnResult2D::Occupied && character<bool>(cp,c,190),"destination checks restored standing, rejection preserves crouch");}
    {auto k=c;k.spawns[0].position={D::fromInt(6),q(2)};auto a=scene(k,false);a.emplace_back(40,actor(k,DetBodyMode2D::Static,{D::fromInt(6),q(2)},{q(2),q(1)},false,false,true));
        DeterministicSession s({44,1,1,4,44});require(configure(s,k,std::move(a)),s.error());advance(s);advance(s,0,1);
        require(detCharacterRespawnState2D(checkpoint(s),10,k).result==DetRespawnResult2D::Occupied,"one-way full thickness conservatively blocks destination");}
    {auto k=c;k.character.collision.staticSlopes=true;k.spawns[0].position={D::fromInt(7),q(6)};k.spawns[1].position={D::fromInt(9),q(4)};
        auto a=scene(k,false);a.erase(a.begin()+2);a.emplace_back(40,actor(k,DetBodyMode2D::Static,{D::fromInt(8),D::fromInt(1)},{D::fromInt(2),D::fromInt(1)},false,false,false,DetCollisionShape2D::SlopeRisesRight));
        DeterministicSession s({44,1,1,4,44});require(configure(s,k,std::move(a)),s.error());advance(s);advance(s,0,1);accepted(s,k);
        advance(s,0,2);require(detCharacterRespawnState2D(checkpoint(s),10,k).result==DetRespawnResult2D::Occupied,"real filled wedge interior blocks, empty bounding-box region did not");}
    {auto k=c;k.character.collision.stepUp=true;k.character.collision.maxStepHeight=q(2);
        DeterministicSession s({44,1,1,4,44});require(configure(s,k,scene(k,false)),s.error());advance(s);advance(s,8,1);accepted(s,k);
        const auto cp=checkpoint(s);require(!body<bool>(cp,k,DetBodyStepRequested) && !body<bool>(cp,k,DetBodyStepped),"step pre protocol cleared after accepted relocation");}
    {auto k=c;k.character.collision.maxTriggerPairs=1;auto a=scene(k);a.emplace_back(64,actor(k,DetBodyMode2D::Static,{D::fromInt(4),q(2)},{q(1),q(2)},false,true));
        DeterministicSession s({44,1,1,4,44});require(configure(s,k,std::move(a)),s.error());advance(s);advance(s,8,1);
        const auto cp=checkpoint(s);require(detCharacterRespawnState2D(cp,10,k).result==DetRespawnResult2D::TriggerCapacity && position(cp)==V{q(2),q(2)},"destination pair overflow retains baseline");}
    {auto k=c;k.character.collision.maxTriggerPairs=1;auto a=scene(k);auto extra=actor(k,DetBodyMode2D::Static,{{},q(2)},{q(1),q(2)},false,true);
        writeDetState(detCollisionBodySchema2D(k.character.collision),extra.blocks.at(k.character.collision.bodySchema),DetBodyMask,std::uint32_t{});a.emplace_back(64,std::move(extra));
        DeterministicSession s({44,1,1,4,44});require(configure(s,k,std::move(a),false,false,true),s.error());advance(s);
        require(!s.advance(input(s.nextTick(),0,1)) && s.faulted(),"respawn does not hide baseline pair budget fault");}
    {auto k=c;k.spawns[0].position={D::fromInt(1),q(2)};auto a=scene(k,false);a.emplace_back(61,actor(k,DetBodyMode2D::Static,{D::fromInt(1),q(2)},{q(1,8),q(2)},false,true));
        DeterministicSession s({44,1,1,4,44});require(configure(s,k,std::move(a),false,true),s.error());advance(s);
        require(!s.advance(input(s.nextTick(),32,1)) && s.faulted(),"baseline Enter+Exit cannot be reduced to endpoint Enter to evade exact Session budget");}
    {DeterministicSession s({44,1,1,4,44});require(configure(s,c,scene(c,false)),s.error());advance(s);auto in=input(s.nextTick(),0,1);
        auto second=in.commands.front();second.sequence=1;second.payload[1]=2;in.commands.push_back(second);require(s.advance(in),s.error());accepted(s,c);
        advance(s,0,9);require(detCharacterRespawnState2D(checkpoint(s),10,c).spawn==1,"normal unknown request preserves last resolved result");}
    std::cout<<"PASS directed tick-end relocation, actual endpoint/normal trace, standing/solid/wedge/step/held/budget policies\n";
}
void contracts(){const auto c=policy();
    for(unsigned kind=0;kind<4;++kind){auto bad=c;if(kind==0)bad.character.collision.characterRespawn=false;if(kind==1)bad.character.advanced=false;
        if(kind==2)bad.character.collision.maxTriggerPairs=56;if(kind==3)bad.spawns[1].id=bad.spawns[0].id;
        DeterministicSession s;require(!installDetCharacterRespawn2D(s,bad),"false configuration rejected before registration");require(installDetCharacterRespawn2D(s,c),"invalid preflight preserved fresh registration");}
    {DeterministicSession s;require(!installDetCharacterController2D(s,c.character) && !installDetCollision2D(s,c.character.collision),"ordinary installers reject opt-in flag");}
    DeterministicSession s({44,1,1,4,44});require(configure(s,c,scene(c,false)),s.error());advance(s);advance(s,0,1);accepted(s,c);
    const auto cp=checkpoint(s);const auto before=bytes(s);for(unsigned kind=0;kind<5;++kind){auto malformed=cp;auto& words=malformed.actors.at(10).blocks.at(c.stateSchema);
        if(kind==0)writeDetState(respawnSchema(c),words,DetRespawnPendingSpawn,std::uint32_t{1});
        if(kind==1)writeDetState(respawnSchema(c),words,DetRespawnApplied,true);
        if(kind==2)writeDetState(respawnSchema(c),words,DetRespawnPreCompleted,true);
        if(kind==3)malformed.actors.at(10).pose.hasPreviousPosition=true;
        if(kind==4)writeDetState(characterSchema(c),malformed.actors.at(10).blocks.at(c.character.stateSchema),DetCharacterCoyoteRemaining,std::uint32_t{1});
        require(!s.restore(malformed) && bytes(s)==before,"malformed committed respawn restore rejects atomically");}
    auto mismatched=c;mismatched.character.gravity=D::fromInt(1);bool threw=false;try{(void)detCharacterRespawnState2D(cp,10,mismatched);}catch(const std::exception&){threw=true;}
    require(threw,"getter binds complete immutable character policy, not IDs alone");
    {DeterministicSession retired({44,1,1,4,44});require(configure(retired,c,scene(c,false)),retired.error());
        // Restore can retire the issued actor; it cannot introduce an unknown spawn identity.
        auto old=checkpoint(retired);old.actors.erase(10);old.retiredIds={10};require(retired.restore(old),retired.error());
        advance(retired,0,1);require(!checkpoint(retired).actors.contains(10),"retired target is a normal unavailable request, not a stale handle");}
    std::cout<<"PASS explicit registration/profile isolation and atomic malformed pending/result/presentation restore checks\n";
}
DetTickInput closureInput(std::uint64_t tick){return input(tick,tick==3?8:0,tick==3?1:0,tick>=3 && tick<=5);}
void closure(const fs::path& directory){const auto c=policy();constexpr std::uint64_t ticks=12;std::vector<Bytes> checkpoints;std::vector<DetSessionCheckpoint> copies;std::vector<DetConfirmedEvent> events;
    DeterministicSession a({44,1,1,4,44}),b({44,1,1,4,44});require(configure(a,c,scene(c)) && configure(b,c,scene(c),true),"closure configuration");
    checkpoints.push_back(bytes(a));copies.push_back(checkpoint(a));
    for(std::uint64_t tick=0;tick<ticks;++tick){require(a.advance(closureInput(tick)) && b.advance(closureInput(tick)),"closure actual ticks");
        require(bytes(a)==bytes(b),"complete boundary bytes reverse registration");checkpoints.push_back(bytes(a));copies.push_back(checkpoint(a));
        for(const auto& event:copies.back().pendingEvents)events.push_back({1,tick,event});}
    require(a.restore(copies[4]) && bytes(a)==checkpoints[4],"restore actual accepted respawn boundary");
    for(std::uint64_t tick=4;tick<ticks;++tick){require(a.advance(closureInput(tick)),a.error());require(bytes(a)==checkpoints[tick+1],"restored continuation full bytes");}
    DeterministicSession late({44,1,1,4,44});require(configure(late,c,scene(c)),late.error());DetRollbackConfig rc;rc.members={1};rc.historyTicks=32;rc.maxPredictionTicks=4;rc.prediction={{1,DetPredictionMode::Hold}};
    DeterministicRollback owner(late,rc);std::vector<DetConfirmedEvent> released;
    for(std::uint64_t tick=0;tick<ticks;++tick){if(!tick)require(owner.submit(1,closureInput(0)),owner.error());if(tick>=2)require(owner.submit(1,closureInput(tick-2)),owner.error());require(owner.advance(),owner.error());
        const auto confirmed=owner.checkpointAt(owner.confirmedNextTick());require(confirmed && encodeDetCheckpoint(*confirmed)==checkpoints[owner.confirmedNextTick()],"late-input corrected complete boundary");
        const auto drained=owner.takeConfirmedEvents();released.insert(released.end(),drained.begin(),drained.end());}
    for(auto tick=ticks-2;tick<ticks;++tick)require(owner.submit(1,closureInput(tick)),owner.error());const auto drained=owner.takeConfirmedEvents();released.insert(released.end(),drained.begin(),drained.end());
    require(owner.rollbackCount()>0 && owner.replayedTicks()>0 && bytes(late)==checkpoints.back() && released==events,"actual rollback and exactly-once typed trigger closure");
    DeterministicSession recording({44,1,1,4,44});require(configure(recording,c,scene(c)),recording.error());DetReplayWriter writer;require(writer.begin(recording,(directory/"respawn").string(),2),writer.error());
    for(std::uint64_t tick=0;tick<ticks;++tick){require(writer.advance(closureInput(tick)),writer.error());require(bytes(recording)==checkpoints[tick+1],"real recording complete boundary");}require(writer.finish(),writer.error());
    DeterministicSession playback({44,1,1,4,44});require(configure(playback,c,scene(c),true),playback.error());DetReplayReader reader;require(reader.open(writer.path()) && reader.restoreInitial(playback),reader.error());
    for(std::uint64_t target:{0u,4u,7u}){require(reader.seek(playback,target) && bytes(playback)==checkpoints[target],"real seek into initial/accepted/ordinary boundary");
        while(!reader.atEnd()){require(reader.advance(playback),reader.error());require(bytes(playback)==checkpoints[playback.nextTick()],"real replay full continuation");}}
    std::ostringstream summary;summary<<"{\"status\":\"passed\",\"ticks\":12,\"checkpointRows\":13,\"confirmedEvents\":"<<events.size()
        <<",\"rollbacks\":"<<owner.rollbackCount()<<",\"replayedTicks\":"<<owner.replayedTicks()<<",\"replayFiles\":1,\"controllerProfile\":8,\"collisionProfile\":9,\"respawnProfile\":1,\"finalHash\":\""<<detCheckpointHash(copies.back())<<"\"}\n";
    std::ofstream out(directory/"summary.json",std::ios::binary);out<<summary.str();out.close();require(!out.fail(),"summary close");std::cout<<summary.str();
}
}
int main(int argc,char** argv){try{fs::path parent=fs::temp_directory_path();if(argc==3 && std::string(argv[1])=="--output")parent=argv[2];else if(argc!=1)throw std::invalid_argument("usage: [--output EXISTING_PARENT_DIRECTORY]");
    require(fs::is_directory(parent),"ordinary existing output parent required");const auto stamp=std::chrono::steady_clock::now().time_since_epoch().count();fs::path directory;
    for(unsigned i=0;i<1024;++i){auto p=parent/("characterrespawn-"+std::to_string(stamp)+"-"+std::to_string(i));if(fs::create_directory(p)){directory=p;break;}}
    require(!directory.empty(),"fresh output directory");directed();contracts();closure(directory);return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
