// design reference: AYEntity/design.md Stage27; registered prescribed platform paths.
// Tiny dyadic path oracles plus short complete-state recovery/rollback/file replay.
#include <AYEntity/DeterministicPathMotion2D.h>
#include <AYEntity/DeterministicCharacterController2D.h>
#include <AYEntity/DeterministicReplay.h>
#include <AYEntity/DeterministicRollback.h>
#include <algorithm>
#include <array>
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
constexpr SimEntityId platform=20,hero=10;
constexpr std::uint64_t closureTicks=16;
void require(bool ok,const std::string& why){if(!ok)throw std::runtime_error(why);}
D q(int n,int denominator=4){return D::fromInt(n)/D::fromInt(denominator);}
V v(int x,int y){return V::fromInts(x,y);}
DetPathMotion2DConfig policy(DetPathPlayback2D playback=DetPathPlayback2D::PingPong){DetPathMotion2DConfig c;
    c.collision.extended=true;c.collision.maxBodies=8;c.collision.maxTriggerPairs=4;c.tickPeriod=q(1);c.playback=playback;
    c.waypoints={{{},4,0},{v(1,0),4,0},{v(1,1),4,0}};return c;}
DetCharacterController2DConfig characterPolicy(const DetPathMotion2DConfig& c){DetCharacterController2DConfig character;
    character.collision=c.collision;character.gravity={};return character;}
// Descriptors are test-local views of public stable fields, never a substitute
// installer. Public factories author blocks; typed snapshots test restore rules.
DetTypedStateSchema pathSchema(const DetPathMotion2DConfig& c){if(c.movingTrigger)return detPathMotionStateSchema2D(c);return {c.stateSchema,1,{
    {10,false},{20,false},{30,false},{40,false},{50,V{}},{60,std::uint32_t{0}},
    {70,std::int32_t{1}},{80,std::uint32_t{0}},{90,std::uint32_t{0}},{100,V{}},
    {110,false},{120,false},{130,V{}},{140,false}}};}
DetTypedStateSchema bodySchema(const DetPathMotion2DConfig& c){if(c.movingTrigger)return detCollisionBodySchema2D(c.collision);return {c.collision.bodySchema,2,{
    {DetBodyMode,std::uint32_t{0}},{DetBodyHalf,V{}},{DetBodyOffset,V{}},{DetBodyVelocity,V{}},
    {DetBodyLayer,std::uint32_t{1}},{DetBodyMask,std::uint32_t{UINT32_MAX}},{DetBodyTrigger,false}}};}
DetTypedStateSchema characterSchema(const DetCharacterController2DConfig& c){return {c.stateSchema,1,{
    {DetCharacterEnabled,false},{DetCharacterGrounded,false},{DetCharacterSupport,DetEntityRef{}},
    {DetCharacterVelocity,V{}},{DetCharacterSupportVelocity,V{}},{DetCharacterTargetSpeed,D{}},
    {DetCharacterJumpRequested,false},{DetCharacterAppliedCarry,V{}},{DetCharacterIntentVelocity,V{}},
    {DetCharacterJumped,false},{DetCharacterSnapped,false},{DetCharacterSupportTick,std::uint64_t{0}}}};}
template<class T>T pathState(const DetSessionCheckpoint& cp,const DetPathMotion2DConfig& c,std::uint32_t field){return readDetState<T>(pathSchema(c),cp.actors.at(platform).blocks.at(c.stateSchema),field);}
template<class T>void setPath(DetSessionCheckpoint& cp,const DetPathMotion2DConfig& c,std::uint32_t field,T value){writeDetState(pathSchema(c),cp.actors.at(platform).blocks.at(c.stateSchema),field,value);}
template<class T>T body(const DetSessionCheckpoint& cp,const DetPathMotion2DConfig& c,std::uint32_t field){return readDetState<T>(bodySchema(c),cp.actors.at(platform).blocks.at(c.collision.bodySchema),field);}
DetSessionCheckpoint checkpoint(DeterministicSession& s){auto cp=s.checkpoint();require(bool(cp),s.error());return *cp;}
Bytes bytes(DeterministicSession& s){return encodeDetCheckpoint(checkpoint(s));}
DetActorState actor(const DetPathMotion2DConfig& c,DetBodyMode2D mode,V position,V half,V velocity={},bool isHero=false,bool trigger=false){
    DetActorState a;DetSimTransformComponent pose;require(pose.setPosition({position.x,position.y,{}}),"fixture pose");a.pose=pose.snapshot();
    a.blocks[c.collision.bodySchema]=detCollisionBodyState2D(c.collision,{mode,half,{},velocity,isHero?1u:(trigger?4u:2u),isHero?6u:1u,trigger});
    if(isHero){DetCharacterState2D state;state.enabled=true;a.blocks[characterPolicy(c).stateSchema]=detCharacterState2D(characterPolicy(c),state);}return a;}
Actors scene(const DetPathMotion2DConfig& c,bool carry=false,bool trigger=false,V origin={}){Actors actors;
    auto moving=actor(c,DetBodyMode2D::MovingObstacle,origin,{D::fromInt(2),q(2)});
    DetPathMotionState2D initial;initial.bound=true;initial.origin=origin;moving.blocks[c.stateSchema]=detPathMotionState2D(c,initial);actors.emplace_back(platform,std::move(moving));
    if(carry)actors.emplace_back(hero,actor(c,DetBodyMode2D::Kinematic,origin+v(0,1),{q(2),q(2)},{},true));
    if(trigger)actors.emplace_back(30,actor(c,DetBodyMode2D::Static,{q(3),D::fromInt(1)},{q(1,8),q(1,8)},{},false,true));return actors;}
bool configure(DeterministicSession& s,const DetPathMotion2DConfig& c,Actors actors,bool carry=false,bool reverse=false){
    auto input=[c](DetTickContext& ctx){for(const auto& command:ctx.input().commands){
        if(command.source!=1 || command.type!=1 || command.payload.size()!=3
            || std::any_of(command.payload.begin(),command.payload.end(),[](auto b){return b>1;}))return false;
        require(detPathMotionState2D(ctx,platform,c).bound,"public context getter reads the bound platform");
        detPathMotionInput2D(ctx,platform,c,{bool(command.payload[0]),bool(command.payload[1]),bool(command.payload[2])});}return true;};
    // Collision registration precedes path registration; runtime execution order
    // is immutable system IDs: input10, path15, character20/40, collision30, post45.
    if(!reverse && !s.registerSystem(10,0,input))return false;
    if(carry?!installDetCharacterController2D(s,characterPolicy(c)):!installDetCollision2D(s,c.collision))return false;
    if(!installDetPathMotion2D(s,c))return false;if(reverse && !s.registerSystem(10,0,input))return false;
    if(reverse)std::reverse(actors.begin(),actors.end());for(auto& [id,a]:actors)if(!s.addEntity(id,std::move(a)))return false;return s.seal();}
DetTickInput frame(std::uint64_t tick,bool enabled=true,bool paused=false,bool restart=false){return {tick,1,{{1,0,1,{std::uint8_t(enabled),std::uint8_t(paused),std::uint8_t(restart)}}}};}
void advance(DeterministicSession& s,bool enabled=true,bool paused=false){require(s.advance(frame(s.nextTick(),enabled,paused)),s.error());}
void expect(DeterministicSession& s,const DetPathMotion2DConfig& c,V position,V velocity,const std::string& why,bool carry=false){const auto cp=checkpoint(s);const auto& p=cp.actors.at(platform).pose.position;
    require(p[0]==position.x.bits() && p[1]==position.y.bits(),why+" platform expectedBits="+std::to_string(position.x.bits())+","+std::to_string(position.y.bits())+
        " actualBits="+std::to_string(p[0])+","+std::to_string(p[1]));
    require(body<V>(cp,c,DetBodyVelocity)==velocity && pathState<V>(cp,c,100)==velocity,why+" generated/current body velocity");
    require(!pathState<bool>(cp,c,110),why+" unblocked dyadic motion");
    require(!pathState<bool>(cp,c,120) && pathState<V>(cp,c,130)==V{} && !pathState<bool>(cp,c,140),why+" internal pending intent cleared");
    if(carry){const auto cc=characterPolicy(c);const auto& hp=cp.actors.at(hero).pose.position;const auto& words=cp.actors.at(hero).blocks.at(cc.stateSchema);
        require(hp[0]==position.x.bits() && hp[1]==(position.y+D::fromInt(1)).bits(),why+" real carried character pose");
        require(readDetState<V>(characterSchema(cc),words,DetCharacterVelocity)==V{}
            && readDetState<V>(characterSchema(cc),words,DetCharacterSupportVelocity)==velocity
            && readDetState<bool>(characterSchema(cc),words,DetCharacterGrounded)
            && readDetState<DetEntityRef>(characterSchema(cc),words,DetCharacterSupport).value==platform
            && readDetState<std::uint64_t>(characterSchema(cc),words,DetCharacterSupportTick)==cp.nextTick,why+" same-tick carrier state, zero own velocity and fresh support");}}
void playbackChecks(){
    for(auto mode:{DetPathPlayback2D::Once,DetPathPlayback2D::Loop,DetPathPlayback2D::PingPong}){const auto c=policy(mode);const V origin=v(-2,3);
        DeterministicSession s({1,1,1,4,27});require(configure(s,c,scene(c,false,false,origin)),s.error());
        const unsigned ticks=mode==DetPathPlayback2D::Once?10:(mode==DetPathPlayback2D::Loop?13:17);int oldX=0,oldY=0;
        for(unsigned n=1;n<=ticks;++n){int x=0,y=0;
            if(n<=4)x=int(n);else if(n<=8){x=4;y=int(n)-4;}
            else if(mode==DetPathPlayback2D::Once){x=y=4;}
            else if(mode==DetPathPlayback2D::Loop){if(n<=12)x=y=12-int(n);else x=int(n)-12;}
            else if(n<=12){x=4;y=12-int(n);}else if(n<=16)x=16-int(n);else x=int(n)-16;
            advance(s);expect(s,c,origin+V{q(x),q(y)},v(x-oldX,y-oldY),"hand path mode="+std::to_string(unsigned(mode))+" boundary="+std::to_string(n));oldX=x;oldY=y;
            require(pathState<bool>(checkpoint(s),c,40)==(mode==DetPathPlayback2D::Once && n>=8),"Once settles final endpoint; repeat modes remain live");}}
    // Outgoing edge duration belongs to the lower-index waypoint when reversing.
    {auto c=policy();c.waypoints={{{},2,0},{v(1,0),4,0}};DeterministicSession s({1,1,1,4,27});require(configure(s,c,scene(c)),s.error());
        for(int n=1;n<=5;++n){advance(s);const int x=n<=2?2*n:(n<=4?8-2*n:2);
            expect(s,c,{q(x),{}},{D::fromInt(n<=2||n==5?2:-2),{}},"PingPong lower-index edge ticks, not final unused ticks");}}
    std::cout<<"PASS short hand-derived Once/Loop/PingPong, relative origin, no endpoint teleport and unequal reverse-edge durations\n";
}
void dwellAndControlChecks(){
    {auto c=policy(DetPathPlayback2D::Once);c.waypoints[0].dwellTicks=2;c.waypoints[1].dwellTicks=2;DeterministicSession s({1,1,1,4,27});require(configure(s,c,scene(c)),s.error());
        require(pathState<std::uint32_t>(checkpoint(s),c,90)==2,"factory captures first-waypoint dwell");int oldX=0,oldY=0;
        for(int n=1;n<=13;++n){const int x=n<=2?0:(n<=6?n-2:4);const int y=n<=8?0:std::min(n-8,4);
            advance(s);expect(s,c,{q(x),q(y)},v(x-oldX,y-oldY),"initial/endpoint two-tick dwell boundary="+std::to_string(n));oldX=x;oldY=y;}}
    {const auto c=policy();DeterministicSession s({1,1,1,4,27});require(configure(s,c,scene(c)),s.error());advance(s);const auto active=checkpoint(s);
        for(auto [enabled,paused]:{std::pair{true,true},std::pair{false,false},std::pair{false,true}}){advance(s,enabled,paused);expect(s,c,{q(1),{}},{},"pause/disable stops velocity and freezes cursor");const auto cp=checkpoint(s);
            require(pathState<bool>(cp,c,20)==enabled && pathState<bool>(cp,c,30)==paused,"sampled enabled/paused registered state");
            for(auto f:{60u,80u,90u})require(pathState<std::uint32_t>(cp,c,f)==pathState<std::uint32_t>(active,c,f),"paused/disabled cursor remains unchanged");
            require(pathState<std::int32_t>(cp,c,70)==pathState<std::int32_t>(active,c,70),"pause direction unchanged");}
        advance(s);expect(s,c,{q(2),{}},v(1,0),"enable/unpause resumes remaining edge, not origin restart");}
    // Unbound defaults own neither a velocity nor pose: prescribed ordinary
    // MovingObstacle motion continues untouched when no path input targets it.
    {const auto c=policy();Actors actors{{platform,actor(c,DetBodyMode2D::MovingObstacle,{}, {D::fromInt(2),q(2)},v(1,0))}};
        DeterministicSession s({1,1,1,4,27});require(configure(s,c,std::move(actors)),s.error());require(s.advance({0,1,{}}),s.error());const auto cp=checkpoint(s);
        require(cp.actors.at(platform).pose.position[0]==q(1).bits() && body<V>(cp,c,DetBodyVelocity)==v(1,0)
            && !pathState<bool>(cp,c,10),"default unbound path leaves ordinary collision movement alone");}
    {const auto c=policy();DeterministicSession s({1,1,1,4,27});require(configure(s,c,scene(c,true),true),s.error());int oldX=0,oldY=0;
        for(int n=1;n<=9;++n){const int x=n<=4?n:4;const int y=n<=4?0:(n<=8?n-4:3);advance(s);
            expect(s,c,{q(x),q(y)},v(x-oldX,y-oldY),"real horizontal/up/down same-tick legacy character carry",true);oldX=x;oldY=y;}}
    std::cout<<"PASS exact dwell counts, paused/disabled cursor preservation, unbound ordinary motion and real same-tick character carry\n";
}
void rejectRestore(DeterministicSession& s,const DetSessionCheckpoint& bad,const std::string& why){const auto before=bytes(s);
    require(!s.restore(bad) && !s.faulted() && bytes(s)==before,why+" atomic rejection");}
void contractChecks(){const auto c=policy();
    for(unsigned change=0;change<10;++change){auto bad=c;
        if(change==0)bad.waypoints.resize(1);if(change==1)bad.waypoints.resize(25);
        if(change==2)bad.waypoints[0].offset=v(1,0);if(change==3)bad.waypoints[0].travelTicks=0;
        if(change==4)bad.waypoints[0].travelTicks=65536;if(change==5)bad.waypoints[1].dwellTicks=65536;
        if(change==6)bad.waypoints[1].offset.x=D::fromBits(0x7f800000u);if(change==7)bad.playback=static_cast<DetPathPlayback2D>(3);
        if(change==8)bad.tickPeriod={};if(change==9)bad.tickPeriod=D::fromBits(0x7f800000u);
        DeterministicSession s({1,1,1,4,27});require(!installDetPathMotion2D(s,bad),"invalid bounded path policy");}
    for(auto mode:{DetBodyMode2D::Static,DetBodyMode2D::Kinematic}){auto actors=scene(c);actors[0].second.blocks[c.collision.bodySchema]=detCollisionBodyState2D(c.collision,{mode,{D::fromInt(2),q(2)}});
        DeterministicSession s({1,1,1,4,27});require(!configure(s,c,std::move(actors)),"bound path requires MovingObstacle sole-motion adapter");}
    {auto actors=scene(c);DetSimTransformComponent shifted;require(shifted.setPosition({D::fromInt(1),{}, {}}),"bad origin fixture pose");actors[0].second.pose=shifted.snapshot();
        DeterministicSession s({1,1,1,4,27});require(!configure(s,c,std::move(actors)),"initial path origin must match actual pose");}
    DeterministicSession s({1,1,1,4,27});require(configure(s,c,scene(c)),s.error());advance(s);const auto cp=checkpoint(s);
    for(auto field:{120u,140u}){auto bad=cp;setPath(bad,c,field,true);rejectRestore(s,bad,"committed internal path request");}
    {auto bad=cp;setPath(bad,c,130,v(1,0));rejectRestore(s,bad,"committed pending expected-position intent");}
    {auto bad=cp;setPath(bad,c,60,std::uint32_t{3});rejectRestore(s,bad,"waypoint index outside policy");}
    {auto bad=cp;setPath(bad,c,70,std::int32_t{0});rejectRestore(s,bad,"invalid path direction");}
    {auto bad=cp;setPath(bad,c,80,std::uint32_t{65536});rejectRestore(s,bad,"elapsed outside current edge");}
    {auto otherPolicy=c;otherPolicy.waypoints[1].offset=v(2,0);DeterministicSession other({1,1,1,4,27});require(configure(other,otherPolicy,scene(otherPolicy)),other.error());
        rejectRestore(other,cp,"different immutable waypoint policy identity");}
    require(s.restore(cp) && bytes(s)==encodeDetCheckpoint(cp),"healthy path checkpoint restores exactly");
    {const auto oncePolicy=policy(DetPathPlayback2D::Once);DeterministicSession once({1,1,1,4,27});require(configure(once,oncePolicy,scene(oncePolicy)),once.error());advance(once);
        const auto midway=checkpoint(once);require(midway.actors.at(platform).pose.position[0]==q(1).bits(),"real Once checkpoint remains midway on first edge");
        auto bad=midway;setPath(bad,oncePolicy,DetPathFinished,true);setPath(bad,oncePolicy,DetPathWaypoint,std::uint32_t{2});
        setPath(bad,oncePolicy,DetPathElapsedTicks,std::uint32_t{0});setPath(bad,oncePolicy,DetPathDwellRemaining,std::uint32_t{0});
        rejectRestore(once,bad,"finished path phase requires actual final waypoint pose");
        for(unsigned tick=1;tick<8;++tick)advance(once);const auto arrived=checkpoint(once);
        require(pathState<bool>(arrived,oncePolicy,DetPathFinished) && pathState<V>(arrived,oncePolicy,DetPathLastVelocity)==v(0,1)
            && once.restore(arrived) && bytes(once)==encodeDetCheckpoint(arrived),"real finished arrival restores with this tick's nonzero velocity");}
    {auto dwellPolicy=policy(DetPathPlayback2D::Once);dwellPolicy.waypoints[1].dwellTicks=2;DeterministicSession dwell({1,1,1,4,27});require(configure(dwell,dwellPolicy,scene(dwellPolicy)),dwell.error());
        for(unsigned tick=0;tick<4;++tick)advance(dwell);const auto arrived=checkpoint(dwell);
        require(pathState<std::uint32_t>(arrived,dwellPolicy,DetPathDwellRemaining)==2 && pathState<V>(arrived,dwellPolicy,DetPathLastVelocity)==v(1,0),"real arrival enters dwell with this tick's velocity");
        auto bad=arrived;bad.actors.at(platform).pose.position[0]=q(3).bits();
        rejectRestore(dwell,bad,"dwelling path phase requires actual current waypoint pose");
        require(dwell.restore(arrived) && bytes(dwell)==encodeDetCheckpoint(arrived),"real dwell arrival restores with this tick's nonzero velocity");}
    {auto mismatched=c;mismatched.tickPeriod=q(1,8);DeterministicSession badDt({1,1,1,4,27});require(configure(badDt,mismatched,scene(mismatched)),badDt.error());const auto initial=checkpoint(badDt);
        require(!badDt.advance(frame(0)) && badDt.faulted(),"actual Session dt must bit-match declared path period");
        require(badDt.restore(initial) && bytes(badDt)==encodeDetCheckpoint(initial),"explicit restore after period mismatch");}
    {auto notRepresentable=policy(DetPathPlayback2D::Once);notRepresentable.waypoints={{{},1,0},{v(100000000,0),1,0},{{-q(2),{}},1,0}};
        auto actors=scene(notRepresentable);actors[0].second.blocks[notRepresentable.collision.bodySchema]=detCollisionBodyState2D(notRepresentable.collision,{DetBodyMode2D::MovingObstacle,{D::fromInt(16),q(2)}});
        DeterministicSession huge({1,1,1,4,27});require(configure(huge,notRepresentable,std::move(actors)),huge.error());advance(huge);const auto before=checkpoint(huge);
        require(before.actors.at(platform).pose.position[0]==D::fromInt(100000000).bits(),"large represented waypoint really reached");
        // 1e8 has binary32 ULP8. Subtracting it from -.5 loses the half unit;
        // adding the resulting displacement back reaches0, not authored-.5.
        require(!huge.advance(frame(1)) && huge.faulted(),"unreachable rounded target faults rather than drifting to zero");
        require(huge.restore(before) && bytes(huge)==encodeDetCheckpoint(before),"explicit full-state restore after representation fault");}
    std::cout<<"PASS bounded path/mode/period/content arguments, bound body/origin and atomic typed restore/manifest rejection\n";
}
struct Evidence {std::vector<Bytes> states;std::vector<std::uint64_t> hashes;std::vector<DetConfirmedEvent> events;};
DetTickInput closureInput(std::uint64_t tick){return frame(tick,tick!=4 && tick!=5,tick==1 || tick==2);}
Evidence closureChecks(){const auto c=policy();DeterministicSession a({1,1,1,4,27}),b({1,1,1,4,27});
    require(configure(a,c,scene(c,true,true),true) && configure(b,c,scene(c,true,true),true,true),"short closure seal");Evidence evidence;
    std::vector<DetSessionCheckpoint> cps{checkpoint(a)};evidence.states.push_back(bytes(a));evidence.hashes.push_back(detCheckpointHash(cps.back()));require(bytes(a)==bytes(b),"initial canonical full state reverse registration");
    // Independent integer-quarter schedule: explicit pause at1/2, disabled4/5;
    // remaining ticks move around the first PingPong corner with no hidden time.
    constexpr std::array<std::array<int,2>,closureTicks> expected={{{1,0},{1,0},{1,0},{2,0},{2,0},{2,0},{3,0},{4,0},
        {4,1},{4,2},{4,3},{4,4},{4,3},{4,2},{4,1},{4,0}}};int oldX=0,oldY=0;
    for(std::uint64_t tick=0;tick<closureTicks;++tick){const auto in=closureInput(tick);require(a.advance(in) && b.advance(in),"short canonical path input advance");
        const auto [x,y]=expected[tick];expect(a,c,{q(x),q(y)},v(x-oldX,y-oldY),"hand pause/enable/corner schedule tick="+std::to_string(tick),true);oldX=x;oldY=y;
        require(bytes(a)==bytes(b),"every complete checkpoint reverse registration");cps.push_back(checkpoint(a));evidence.states.push_back(encodeDetCheckpoint(cps.back()));evidence.hashes.push_back(detCheckpointHash(cps.back()));
        for(const auto& event:cps.back().pendingEvents)evidence.events.push_back({1,tick,event});}
    require(!evidence.events.empty(),"short path scene produces actual collision trigger events");
    for(std::size_t restart:{2u,8u}){require(a.restore(cps[restart]) && bytes(a)==evidence.states[restart],"restore pause/corner full checkpoint");
        for(auto tick=std::uint64_t(restart);tick<closureTicks;++tick){require(a.advance(closureInput(tick)),a.error());require(bytes(a)==evidence.states[tick+1],"restored continuation complete bytes");}}
    std::cout<<"PASS short16-tick hand schedule, real carry/events, reverse registration and pause/corner complete-state restore\n";return evidence;
}
std::pair<std::uint64_t,std::uint64_t> rollbackChecks(const Evidence& evidence){const auto c=policy();DeterministicSession s({1,1,1,4,27});require(configure(s,c,scene(c,true,true),true,true),s.error());
    DetRollbackConfig config;config.members={1};config.historyTicks=24;config.maxPredictionTicks=4;config.prediction={{1,DetPredictionMode::Hold}};DeterministicRollback owner(s,config);std::vector<DetConfirmedEvent> effects;
    for(std::uint64_t tick=0;tick<closureTicks;++tick){if(!tick)require(owner.submit(1,closureInput(0)),owner.error());if(tick>=2)require(owner.submit(1,closureInput(tick-2)),owner.error());require(owner.advance(),owner.error());
        const auto cp=owner.checkpointAt(owner.confirmedNextTick());require(cp && encodeDetCheckpoint(*cp)==evidence.states[owner.confirmedNextTick()],"late path pause/enable input confirmed full bytes");
        const auto e=owner.takeConfirmedEvents();effects.insert(effects.end(),e.begin(),e.end());}
    for(auto tick=closureTicks-2;tick<closureTicks;++tick)require(owner.submit(1,closureInput(tick)),owner.error());const auto e=owner.takeConfirmedEvents();effects.insert(effects.end(),e.begin(),e.end());
    require(owner.confirmedNextTick()==closureTicks && bytes(s)==evidence.states.back(),"late input restores final actual path/carry complete bytes");
    require(effects==evidence.events && owner.takeConfirmedEvents().empty(),"actual path events released once with exact identities/payloads");
    require(owner.rollbackCount()>0 && owner.replayedTicks()>0,"late path inputs really change predictions");
    std::cout<<"PASS short late-input corrections of pause/enable, final full state and exactly-once real events\n";return {owner.rollbackCount(),owner.replayedTicks()};}
void replayChecks(const fs::path& directory,const Evidence& evidence){const auto c=policy();DeterministicSession recording({1,1,1,4,27});require(configure(recording,c,scene(c,true,true),true),recording.error());
    DetReplayWriter writer;require(writer.begin(recording,(directory/"path-motion").string(),4),writer.error());for(std::uint64_t tick=0;tick<closureTicks;++tick){require(writer.advance(closureInput(tick)),writer.error());require(bytes(recording)==evidence.states[tick+1],"real rpl recording each full checkpoint");}
    require(writer.finish() && fs::is_regular_file(writer.path()),"actual sealed rpl exists");DeterministicSession playback({1,1,1,4,27});require(configure(playback,c,scene(c,true,true),true,true),playback.error());DetReplayReader reader;
    require(reader.open(writer.path()) && reader.restoreInitial(playback),reader.error());require(reader.tickCount()==closureTicks,"real path rpl16 ticks");
    while(!reader.atEnd()){require(reader.advance(playback),reader.error());require(bytes(playback)==evidence.states[playback.nextTick()],"real path rpl full-byte playback");}
    for(std::uint64_t tick:{2u,6u,12u}){require(reader.seek(playback,tick) && bytes(playback)==evidence.states[tick],"real seek paused/disabled/corner full checkpoint");
        while(!reader.atEnd()){require(reader.advance(playback),reader.error());require(bytes(playback)==evidence.states[playback.nextTick()],"real path seek full-byte continuation");}}
    std::cout<<"PASS actual sealed16-tick path .rpl replay and3 pause/disable/corner seeks\n";}
// Stage50 role-specific checks. The old scene/golden and replay above remain
// separate; this scene has no character facade and never gives a trigger carry.
constexpr std::uint64_t triggerTicks=12;
DetPathMotion2DConfig triggerPolicy(DetPathPlayback2D mode=DetPathPlayback2D::PingPong){auto c=policy(mode);
    c.movingTrigger=true;c.collision.motionPolicy=DetCollisionMotionPolicy2D::StopPrescribedOnConflict;
    c.waypoints={{{},4,0},{v(2,0),4,1}};return c;}
Actors triggerScene(const DetPathMotion2DConfig& c){Actors result;
    auto moving=actor(c,DetBodyMode2D::MovingObstacle,v(-1,0),{q(1,8),q(1)},{},false,true);
    moving.blocks[c.collision.bodySchema]=detCollisionBodyState2D(c.collision,{DetBodyMode2D::MovingObstacle,{q(1,8),q(1)},{},{},4,3,true});
    DetPathMotionState2D initial;initial.bound=true;initial.origin=v(-1,0);moving.blocks[c.stateSchema]=detPathMotionState2D(c,initial);
    result.emplace_back(platform,std::move(moving));
    auto stationary=actor(c,DetBodyMode2D::Kinematic,{},{q(1),q(1)});
    stationary.blocks[c.collision.bodySchema]=detCollisionBodyState2D(c.collision,{DetBodyMode2D::Kinematic,{q(1),q(1)},{},{},1,4,false});
    result.emplace_back(hero,std::move(stationary));
    auto wall=actor(c,DetBodyMode2D::Static,{q(2),{}},{q(1,8),q(2)});
    wall.blocks[c.collision.bodySchema]=detCollisionBodyState2D(c.collision,{DetBodyMode2D::Static,{q(1,8),q(2)},{},{},2,4,false});
    result.emplace_back(30,std::move(wall));return result;
}
bool configureTrigger(DeterministicSession& s,const DetPathMotion2DConfig& c,Actors actors,bool reverse=false,
    const DetCollision2DConfig* actualCollision=nullptr){auto input=[c](DetTickContext& ctx){for(const auto& command:ctx.input().commands){
        if(command.source!=1)return false;
        if(command.type==1){if(command.payload.size()!=3 || std::any_of(command.payload.begin(),command.payload.end(),[](auto b){return b>1;}))return false;
            detPathMotionInput2D(ctx,platform,c,{bool(command.payload[0]),bool(command.payload[1]),bool(command.payload[2])});}
        else if(command.type==2){if(command.payload.size()!=1 || command.payload[0]>1)return false;
            ctx.write(platform,c.collision.bodySchema,DetBodyMask,command.payload[0]?3u:0u);}
        else return false;}return true;};
    if(!reverse && !s.registerSystem(10,c.collision.priority,input))return false;
    if(!installDetCollision2D(s,actualCollision?*actualCollision:c.collision) || !installDetPathMotion2D(s,c))return false;
    if(reverse && !s.registerSystem(10,c.collision.priority,input))return false;
    if(reverse)std::reverse(actors.begin(),actors.end());for(auto& [id,a]:actors)if(!s.addEntity(id,std::move(a)))return false;return s.seal();
}
DetTickInput triggerInput(std::uint64_t tick){auto in=frame(tick,tick!=3,tick==1);
    // Participation is an explicit application command, independent from path
    // controls. The masked hazard continues travelling; there is no hidden retry.
    in.commands.push_back({1,1,2,{std::uint8_t(tick<5 || tick>=7)}});return in;
}
void triggerRoleChecks(){const auto c=triggerPolicy();const auto selected=detPathMotionPolicySchema2D(c);
    require(selected.version==2 && detPathMotionStateSchema2D(c).version==2
        && readDetState<bool>(selected,detStateDefaults(selected),12),"explicit trigger policy/state2 and full true role");
    const auto legacy=policy();const auto oldPolicy=detPathMotionPolicySchema2D(legacy);
    require(oldPolicy.version==1 && detPathMotionStateSchema2D(legacy).version==1 && oldPolicy.fields.size()==11+3*legacy.waypoints.size()
        && std::none_of(oldPolicy.fields.begin(),oldPolicy.fields.end(),[](const auto& f){return f.id==12;}),"default path1 has no added false role field");
    for(auto mode:{DetPathPlayback2D::Once,DetPathPlayback2D::Loop,DetPathPlayback2D::PingPong}){auto basic=triggerPolicy(mode);basic.waypoints[1].dwellTicks=0;
        DeterministicSession s({1,1,1,4,50});require(configureTrigger(s,basic,triggerScene(basic)),s.error());int old=-4;
        for(int tick=0;tick<9;++tick){const int n=tick+1;const int x=n<=4?-4+2*n:(mode==DetPathPlayback2D::Once?4:(n<=8?12-2*n:-4+2*(n-8)));
            advance(s);expect(s,basic,{q(x),{}},v(x-old,0),"trigger hand playback mode="+std::to_string(unsigned(mode))+" tick="+std::to_string(tick));old=x;}}
    for(auto mode:{DetBodyMode2D::Disabled,DetBodyMode2D::Static,DetBodyMode2D::Kinematic,DetBodyMode2D::MovingObstacle}){auto actors=triggerScene(c);
        const bool trigger=mode!=DetBodyMode2D::MovingObstacle;
        actors[0].second.blocks[c.collision.bodySchema]=detCollisionBodyState2D(c.collision,{mode,{q(1,8),q(1)},{},{},4,3,trigger});
        DeterministicSession s({1,1,1,4,50});require(!configureTrigger(s,c,std::move(actors)),"explicit trigger role rejects other mode/solid binding");}
    {auto old=c;old.movingTrigger=false;DeterministicSession s({1,1,1,4,50});require(!configureTrigger(s,old,triggerScene(old)),"old solid role still rejects moving trigger");}
    {auto wrong=c;wrong.tickPeriod=q(1,8);DeterministicSession s({1,1,1,4,50});require(!installDetPathMotion2D(s,wrong),"Path2 period mismatch is rejected before registration");}
    for(bool priority:{false,true}){auto actual=c.collision;if(priority)actual.priority=1;else actual.maxBodies=7;
        DeterministicSession s({1,1,1,4,50});require(!configureTrigger(s,c,triggerScene(c),false,&actual),"new role requires actual complete collision policy and priority");}
    DeterministicSession s({1,1,1,4,50});require(configureTrigger(s,c,triggerScene(c)),s.error());advance(s);const auto cp=checkpoint(s);
    {auto bad=cp;const auto bs=detCollisionBodySchema2D(c.collision);writeDetState(bs,bad.actors.at(platform).blocks.at(bs.id),DetBodyTrigger,false);
        rejectRestore(s,bad,"moving trigger role cannot restore a solid bound actor");}
    {auto bad=cp;setPath(bad,c,DetPathAttempted,true);rejectRestore(s,bad,"Path2 committed attempted flag must be cleared");}
    require(!s.advance(frame(s.nextTick(),true,false,true)) && s.faulted(),"trigger restart away from origin remains rejected");
    require(s.restore(cp) && bytes(s)==encodeDetCheckpoint(cp),"explicit restoration after rejected trigger restart");
    {DeterministicSession origin({1,1,1,4,50});require(configureTrigger(origin,c,triggerScene(c)),origin.error());
        require(origin.advance(frame(0,true,false,true)),origin.error());expect(origin,c,{-q(2),{}},v(2,0),"origin-only trigger restart resets phase without teleport");}
    std::cout<<"PASS explicit trigger role, hand Once/Loop/PingPong, actual policy binding and atomic malformed restore/restart refusal\n";
}
struct TriggerEvidence {Evidence evidence;std::uint64_t rollbacks=0,replayed=0;};
TriggerEvidence triggerClosureChecks(const fs::path& directory){const auto c=triggerPolicy();DeterministicSession a({1,1,1,4,50}),b({1,1,1,4,50});
    require(configureTrigger(a,c,triggerScene(c)) && configureTrigger(b,c,triggerScene(c),true),"moving trigger closure seal");TriggerEvidence result;auto& evidence=result.evidence;
    std::vector<DetSessionCheckpoint> cps{checkpoint(a)};evidence.states.push_back(bytes(a));evidence.hashes.push_back(detCheckpointHash(cps.back()));
    require(bytes(a)==bytes(b),"trigger initial full checkpoint reverse registration");
    constexpr std::array<int,triggerTicks> xs={-2,-2,0,0,2,4,4,2,0,-2,-4,-2};int old=-4;
    const DetCollisionPair2D characterPair{hero,platform},wallPair{platform,30};
    for(std::uint64_t tick=0;tick<triggerTicks;++tick){require(a.advance(triggerInput(tick)) && b.advance(triggerInput(tick)),"moving trigger scripted advance");
        expect(a,c,{q(xs[tick]),{}},v(xs[tick]-old,0),"hand trigger pause/disable/dwell/mask tick="+std::to_string(tick));old=xs[tick];
        const auto cp=checkpoint(a);require(cp.actors.at(hero).pose.position[0]==D{}.bits() && cp.actors.at(hero).pose.position[1]==D{}.bits()
            && cp.actors.at(30).pose.position[0]==q(2).bits(),"hazard crosses mutually masked character/wall without solid displacement or carry");
        require(body<std::uint32_t>(cp,c,DetBodyMotionResult)==0 && body<DetEntityRef>(cp,c,DetBodyMotionOther).value==0
            && body<std::uint32_t>(cp,c,DetBodyMotionAxis)==0 && body<std::uint64_t>(cp,c,DetBodyMotionResultTick)==0,"moving trigger never reports Stop conflict/pinch");
        std::vector<DetTriggerEvent2D> expected;
        if(tick==2)expected.push_back({DetTriggerPhase2D::Enter,characterPair});if(tick==3)expected.push_back({DetTriggerPhase2D::Stay,characterPair});
        if(tick==4)expected={{DetTriggerPhase2D::Exit,characterPair},{DetTriggerPhase2D::Enter,wallPair}};
        if(tick==5)expected.push_back({DetTriggerPhase2D::Exit,wallPair});if(tick==7)expected.push_back({DetTriggerPhase2D::Enter,wallPair});
        if(tick==8)expected={{DetTriggerPhase2D::Enter,characterPair},{DetTriggerPhase2D::Exit,wallPair}};
        if(tick==9)expected.push_back({DetTriggerPhase2D::Exit,characterPair});
        std::vector<DetTriggerEvent2D> actual;for(const auto& event:cp.pendingEvents){require(event.source==c.collision.systemId && event.type==c.collision.eventType,"real collision owns hazard events");
            DetTriggerEvent2D decoded;require(decodeDetTriggerEvent2D(event.payload,decoded),"real hazard event payload");actual.push_back(decoded);evidence.events.push_back({1,tick,event});}
        require(actual==expected,"hand trigger Enter/Stay/Exit and mask transitions tick="+std::to_string(tick));
        require(bytes(a)==bytes(b),"trigger complete checkpoint reverse registration each boundary");cps.push_back(cp);evidence.states.push_back(encodeDetCheckpoint(cp));evidence.hashes.push_back(detCheckpointHash(cp));}
    for(std::size_t start:{3u,6u}){require(a.restore(cps[start]) && bytes(a)==evidence.states[start],"restore active contact/masked dwell complete checkpoint");
        for(auto tick=std::uint64_t(start);tick<triggerTicks;++tick){require(a.advance(triggerInput(tick)),a.error());require(bytes(a)==evidence.states[tick+1],"restored moving trigger full-byte continuation");}}
    DeterministicSession late({1,1,1,4,50});require(configureTrigger(late,c,triggerScene(c),true),late.error());DetRollbackConfig rc;
    rc.members={1};rc.historyTicks=24;rc.maxPredictionTicks=4;rc.prediction={{1,DetPredictionMode::Hold}};DeterministicRollback owner(late,rc);std::vector<DetConfirmedEvent> effects;
    for(std::uint64_t tick=0;tick<triggerTicks;++tick){if(!tick)require(owner.submit(1,triggerInput(0)),owner.error());if(tick>=2)require(owner.submit(1,triggerInput(tick-2)),owner.error());require(owner.advance(),owner.error());
        const auto cp=owner.checkpointAt(owner.confirmedNextTick());require(cp && encodeDetCheckpoint(*cp)==evidence.states[owner.confirmedNextTick()],"corrected trigger phase/mask confirmed full bytes");
        const auto e=owner.takeConfirmedEvents();effects.insert(effects.end(),e.begin(),e.end());}
    for(auto tick=triggerTicks-2;tick<triggerTicks;++tick)require(owner.submit(1,triggerInput(tick)),owner.error());const auto e=owner.takeConfirmedEvents();effects.insert(effects.end(),e.begin(),e.end());
    require(owner.confirmedNextTick()==triggerTicks && bytes(late)==evidence.states.back() && effects==evidence.events && owner.takeConfirmedEvents().empty(),"trigger late correction full state and actual confirmed events released once");
    result.rollbacks=owner.rollbackCount();result.replayed=owner.replayedTicks();require(result.rollbacks && result.replayed,"pause/disable/mask actually correct predictions");
    DeterministicSession recording({1,1,1,4,50});require(configureTrigger(recording,c,triggerScene(c)),recording.error());DetReplayWriter writer;
    require(writer.begin(recording,(directory/"path-trigger").string(),4),writer.error());for(std::uint64_t tick=0;tick<triggerTicks;++tick){require(writer.advance(triggerInput(tick)),writer.error());require(bytes(recording)==evidence.states[tick+1],"actual trigger rpl records full state");}
    require(writer.finish() && fs::is_regular_file(writer.path()),"actual sealed trigger replay file");DeterministicSession replay({1,1,1,4,50});require(configureTrigger(replay,c,triggerScene(c),true),replay.error());DetReplayReader reader;
    require(reader.open(writer.path()) && reader.restoreInitial(replay) && reader.tickCount()==triggerTicks,reader.error());
    for(std::uint64_t seek:{0u,3u,6u}){require(reader.seek(replay,seek) && bytes(replay)==evidence.states[seek],"actual trigger replay seek initial/contact/masked dwell");
        while(!reader.atEnd()){require(reader.advance(replay),reader.error());require(bytes(replay)==evidence.states[replay.nextTick()],"actual trigger seek complete-byte continuation");}}
    std::cout<<"PASS short12-tick moving trigger hand geometry/events, full CP restore, late correction and actual sealed rpl/seek\n";return result;
}
void write(const fs::path& file,const std::string& text){std::ofstream out(file,std::ios::binary);out<<text;require(bool(out),"artifact write "+file.string());}
std::string quote(const std::string& value){std::string result="\"";for(unsigned char b:value){if(b=='\\'||b=='\"'){result+='\\';result+=char(b);}else if(b<32){const char* hex="0123456789abcdef";result+="\\u00";result+=hex[b>>4];result+=hex[b&15];}else result+=char(b);}return result+'\"';}
fs::path fresh(const fs::path& parent){require(fs::is_directory(parent),"--output must name an existing parent directory");const auto stamp=std::chrono::steady_clock::now().time_since_epoch().count();
    for(unsigned attempt=0;attempt<1024;++attempt){auto dir=parent/("pathmotion-"+std::to_string(stamp)+"-"+std::to_string(attempt));if(fs::create_directory(dir))return dir;}throw std::runtime_error("fresh path fixture directory unavailable");}
} // namespace
int main(int argc,char** argv){fs::path directory;try{fs::path parent=fs::temp_directory_path();if(argc==3 && std::string(argv[1])=="--output")parent=argv[2];else if(argc!=1){std::cerr<<"usage: [--output EXISTING_PARENT_DIRECTORY]\n";return 2;}directory=fresh(parent);
    write(directory/"progress.json","{\"status\":\"running\",\"phase\":\"directed\"}\n");playbackChecks();dwellAndControlChecks();contractChecks();triggerRoleChecks();
    write(directory/"progress.json","{\"status\":\"running\",\"phase\":\"closure\"}\n");const auto evidence=closureChecks();const auto [rollbacks,replayed]=rollbackChecks(evidence);replayChecks(directory,evidence);
    const auto trigger=triggerClosureChecks(directory);
    std::ostringstream trace;for(std::size_t tick=0;tick<evidence.hashes.size();++tick)trace<<tick<<' '<<evidence.hashes[tick]<<'\n';write(directory/"tick-hashes.txt",trace.str());
    std::ostringstream summary;summary<<"{\"status\":\"passed\",\"ticks\":"<<closureTicks<<",\"checkpointRows\":"<<evidence.states.size()<<",\"confirmedEvents\":"<<evidence.events.size()
        <<",\"rollbacks\":"<<rollbacks<<",\"replayedTicks\":"<<replayed<<",\"replayFiles\":1,\"pathProfile\":1,\"collisionProfile\":2,\"finalHash\":\""<<evidence.hashes.back()<<"\",\"artifacts\":"<<quote(directory.string())<<"}\n";
    std::ostringstream triggerTrace;for(std::size_t tick=0;tick<trigger.evidence.hashes.size();++tick)triggerTrace<<tick<<' '<<trigger.evidence.hashes[tick]<<'\n';write(directory/"tick-hashes-trigger.txt",triggerTrace.str());
    std::ostringstream triggerSummary;triggerSummary<<"{\"status\":\"passed\",\"ticks\":"<<triggerTicks<<",\"checkpointRows\":"<<trigger.evidence.states.size()<<",\"confirmedEvents\":"<<trigger.evidence.events.size()
        <<",\"rollbacks\":"<<trigger.rollbacks<<",\"replayedTicks\":"<<trigger.replayed<<",\"replayFiles\":1,\"pathProfile\":2,\"collisionProfile\":5,\"finalHash\":\""<<trigger.evidence.hashes.back()<<"\",\"artifacts\":"<<quote(directory.string())<<"}\n";
    write(directory/"summary-trigger.json",triggerSummary.str());write(directory/"summary.json",summary.str());write(directory/"progress.json","{\"status\":\"passed\",\"phase\":\"complete\"}\n");std::cout<<summary.str()<<triggerSummary.str();return 0;
}catch(const std::exception& e){if(!directory.empty())try{write(directory/"summary.json","{\"status\":\"failed\",\"error\":"+quote(e.what())+"}\n");write(directory/"progress.json","{\"status\":\"failed\"}\n");}catch(...){}std::cerr<<e.what()<<'\n';return 1;}}
