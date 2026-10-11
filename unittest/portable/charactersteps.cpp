// design reference: AYEntity/design.md Stage26; opt-in static AABB step-up.
// Short, dyadic hand-calculated paths. Full-state closure is a separate check,
// not a claim that every checkpoint byte has an independent motion oracle.
#include <AYEntity/DeterministicCharacterController2D.h>
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
using D=ayt::math::DetFloat32;using V=ayt::math::DetVec2;
using Bytes=std::vector<std::uint8_t>;
using Actors=std::vector<std::pair<SimEntityId,DetActorState>>;
constexpr SimEntityId hero=10,floorId=20,stepId=30;
constexpr std::uint64_t closureTicks=8;
void require(bool ok,const std::string& why){if(!ok)throw std::runtime_error(why);}
D q(int n,int denominator=4){return D::fromInt(n)/D::fromInt(denominator);}
V v(int x,int y){return V::fromInts(x,y);}
DetCharacterController2DConfig policy(){DetCharacterController2DConfig c;
    c.advanced=true;c.collision.oneWayPlatforms=true;c.collision.stepUp=true;c.collision.maxStepHeight=q(2);
    c.maxHorizontalSpeed=D::fromInt(8);c.acceleration=c.deceleration=D::fromInt(32);
    c.gravity={};c.jumpSpeed=D::fromInt(4);c.collision.maxBodies=16;c.collision.maxTriggerPairs=8;return c;}
// Only test-local descriptors of the public typed field IDs. Factory helpers
// author blocks; committed copies are read/mutated by field for precise checks.
DetTypedStateSchema stateSchema(const DetCharacterController2DConfig& c){return {c.stateSchema,3,{
    {DetCharacterEnabled,false},{DetCharacterGrounded,false},{DetCharacterSupport,DetEntityRef{}},
    {DetCharacterVelocity,V{}},{DetCharacterSupportVelocity,V{}},{DetCharacterTargetSpeed,D{}},
    {DetCharacterJumpRequested,false},{DetCharacterAppliedCarry,V{}},{DetCharacterIntentVelocity,V{}},
    {DetCharacterJumped,false},{DetCharacterSnapped,false},{DetCharacterSupportTick,std::uint64_t{0}},
    {DetCharacterCoyoteRemaining,std::uint32_t{0}},{DetCharacterJumpBufferRemaining,std::uint32_t{0}},
    {DetCharacterJumpHeld,false},{DetCharacterDropHeld,false},{DetCharacterCrouchHeld,false},
    {DetCharacterDropStarted,false},{DetCharacterCrouched,false},{DetCharacterStandingCaptured,false},
    {DetCharacterStandingHalf,V{}},{DetCharacterStandingOffset,V{}},{DetCharacterDropRemaining,std::uint32_t{0}},
    {DetCharacterCoyoteSupportY,D{}},{DetCharacterJumpLatch,false},{DetCharacterDropLatch,false},
    {DetCharacterDropTick,std::uint64_t{0}},{DetCharacterStepped,false}}};}
DetTypedStateSchema bodySchema(const DetCharacterController2DConfig& c){return {c.collision.bodySchema,4,{
    {DetBodyMode,std::uint32_t{0}},{DetBodyHalf,V{}},{DetBodyOffset,V{}},{DetBodyVelocity,V{}},
    {DetBodyLayer,std::uint32_t{1}},{DetBodyMask,std::uint32_t{UINT32_MAX}},{DetBodyTrigger,false},
    {DetBodyOneWay,false},{DetBodyIgnoredOneWay,DetEntityRef{}},{DetBodyStepRequested,false},{DetBodyStepped,false}}};}
template<class T>T state(const DetSessionCheckpoint& cp,const DetCharacterController2DConfig& c,std::uint32_t field,SimEntityId id=hero){
    return readDetState<T>(stateSchema(c),cp.actors.at(id).blocks.at(c.stateSchema),field);}
template<class T>T body(const DetSessionCheckpoint& cp,const DetCharacterController2DConfig& c,std::uint32_t field,SimEntityId id=hero){
    return readDetState<T>(bodySchema(c),cp.actors.at(id).blocks.at(c.collision.bodySchema),field);}
template<class T>void setState(DetSessionCheckpoint& cp,const DetCharacterController2DConfig& c,std::uint32_t field,T value,SimEntityId id=hero){
    writeDetState(stateSchema(c),cp.actors.at(id).blocks.at(c.stateSchema),field,value);}
template<class T>void setBody(DetSessionCheckpoint& cp,const DetCharacterController2DConfig& c,std::uint32_t field,T value,SimEntityId id=hero){
    writeDetState(bodySchema(c),cp.actors.at(id).blocks.at(c.collision.bodySchema),field,value);}
DetSessionCheckpoint checkpoint(DeterministicSession& s){auto cp=s.checkpoint();require(bool(cp),s.error());return *cp;}
Bytes bytes(DeterministicSession& s){return encodeDetCheckpoint(checkpoint(s));}
DetActorState actor(const DetCharacterController2DConfig& c,DetBodyMode2D mode,V position,V half,
    V velocity={},bool character=false,bool oneWay=false,bool trigger=false){
    DetActorState a;DetSimTransformComponent pose;require(pose.setPosition({position.x,position.y,{}}),"fixture pose");a.pose=pose.snapshot();
    a.blocks[c.collision.bodySchema]=detCollisionBodyState2D(c.collision,{mode,half,{},velocity,character?1u:(trigger?4u:2u),character?6u:1u,trigger,oneWay});
    if(character){DetCharacterState2D initial;initial.enabled=true;a.blocks[c.stateSchema]=detCharacterState2D(c,initial);}return a;}
Actors scene(const DetCharacterController2DConfig& c,int direction=1){return {
    {hero,actor(c,DetBodyMode2D::Kinematic,{{},q(2)},{q(2),q(2)},{},true)},
    {floorId,actor(c,DetBodyMode2D::Static,{{},-q(2)},{D::fromInt(16),q(2)})},
    {stepId,actor(c,DetBodyMode2D::Static,{q(direction*5),q(1)},{q(2),q(1)})}};}
void addTraceTrigger(Actors& actors,const DetCharacterController2DConfig& c,SimEntityId id=60,D x=-q(3,8)){
    // The vertical trial enters a small trigger above the initial head. Across
    // motion exits it; neither the initial/final box nor the ordinary path overlaps.
    actors.emplace_back(id,actor(c,DetBodyMode2D::Static,{x,q(5)},{q(1,16),q(1,16)},{},false,false,true));}
bool configure(DeterministicSession& s,const DetCharacterController2DConfig& c,Actors actors,bool reverse=false,bool retireStep=false){
    auto input=[c,retireStep](DetTickContext& ctx){for(const auto& cmd:ctx.input().commands){
        if(cmd.source!=1 || cmd.type!=1 || cmd.payload.size()!=4 || cmd.payload[0]>1 || cmd.payload[2]>1 || cmd.payload[3]>1)return false;
        const auto x=cmd.payload[1]<128?int(cmd.payload[1]):int(cmd.payload[1])-256;
        detCharacterInput2D(ctx,hero+cmd.payload[0],c,{q(x),bool(cmd.payload[2]),false,bool(cmd.payload[3])});}
        if(retireStep && ctx.tick()==0)ctx.despawn(stepId);return true;};
    if(reverse){if(!installDetCharacterController2D(s,c) || !s.registerSystem(10,0,input))return false;std::reverse(actors.begin(),actors.end());}
    else if(!s.registerSystem(10,0,input) || !installDetCharacterController2D(s,c))return false;
    for(auto& [id,a]:actors)if(!s.addEntity(id,std::move(a)))return false;return s.seal();}
DetTickInput frame(std::uint64_t tick,int targetQuarters=0,bool jump=false,bool drop=false,bool second=false){
    require(targetQuarters>=-128 && targetQuarters<=127,"fixture command bound");
    DetTickInput in{tick,1,{{1,0,1,{0,std::uint8_t(targetQuarters),std::uint8_t(jump),std::uint8_t(drop)}}}};
    if(second)in.commands.push_back({1,1,1,{1,std::uint8_t(targetQuarters),std::uint8_t(jump),std::uint8_t(drop)}});return in;}
void advance(DeterministicSession& s,int target=0,bool jump=false,bool drop=false,bool second=false){
    require(s.advance(frame(s.nextTick(),target,jump,drop,second)),s.error());}
void expect(DeterministicSession& s,const DetCharacterController2DConfig& c,V position,V velocity,
    bool grounded,SimEntityId support,bool stepped,const std::string& why,SimEntityId id=hero){
    const auto cp=checkpoint(s);const auto& p=cp.actors.at(id).pose.position;
    require(p[0]==position.x.bits() && p[1]==position.y.bits(),why+" pose expectedBits="+std::to_string(position.x.bits())+","+std::to_string(position.y.bits())+
        " actualBits="+std::to_string(p[0])+","+std::to_string(p[1]));
    const auto own=state<V>(cp,c,DetCharacterVelocity,id);
    require(own==velocity,why+" own velocity expectedBits="+std::to_string(velocity.x.bits())+","+std::to_string(velocity.y.bits())+
        " actualBits="+std::to_string(own.x.bits())+","+std::to_string(own.y.bits()));
    require(state<bool>(cp,c,DetCharacterGrounded,id)==grounded && state<DetEntityRef>(cp,c,DetCharacterSupport,id).value==support,why+" support");
    require(state<std::uint64_t>(cp,c,DetCharacterSupportTick,id)==(support?cp.nextTick:0),why+" fresh support stamp");
    require(state<bool>(cp,c,DetCharacterStepped,id)==stepped,why+" last-tick result");
    for(const auto& [actorId,a]:cp.actors){(void)a;require(!body<bool>(cp,c,DetBodyStepRequested,actorId) && !body<bool>(cp,c,DetBodyStepped,actorId),why+" transient protocol zero at commit");}
    require(!state<bool>(cp,c,DetCharacterJumpRequested,id) && !state<bool>(cp,c,DetCharacterJumped,id)
        && !state<bool>(cp,c,DetCharacterSnapped,id) && !state<bool>(cp,c,DetCharacterDropStarted,id)
        && state<V>(cp,c,DetCharacterAppliedCarry,id)==V{} && state<V>(cp,c,DetCharacterIntentVelocity,id)==V{},why+" internal controller state cleared");
}
void directedChecks(){
    const auto c=policy();
    // dt1/4, intended X4: ordinary wall at X3/4 stops a .5-half actor at
    // X1/4. The step path rises .5 in1/16, moves X1 in1/8, and lands on
    // the target top at time0 of the final1/16. Every quantity is dyadic.
    for(int direction:{1,-1}){DeterministicSession s({1,1,1,4,26});require(configure(s,c,scene(c,direction)),s.error());
        advance(s,16*direction);expect(s,c,{D::fromInt(direction),D::fromInt(1)},{D::fromInt(direction*4),{}},true,stepId,true,"hand-computed left/right step");
        advance(s);expect(s,c,{D::fromInt(direction),D::fromInt(1)},{},true,stepId,false,"next tick clears result");}
    auto ordinary=[&](Actors actors,const std::string& why){DeterministicSession s({1,1,1,4,26});require(configure(s,c,std::move(actors)),s.error());
        advance(s,16);expect(s,c,{q(1),q(2)},{},true,floorId,false,why);return checkpoint(s);};
    {auto a=scene(c);a[2].second=actor(c,DetBodyMode2D::Static,{q(5),q(3,8)},{q(2),q(3,8)});
        ordinary(std::move(a),"height3/4 exceeds explicit max1/2");}
    {auto a=scene(c);a.emplace_back(50,actor(c,DetBodyMode2D::Static,{{},q(6)},{D::fromInt(2),q(1)}));
        ordinary(std::move(a),"low static roof rejects complete trial");}
    {auto a=scene(c);a[2].second=actor(c,DetBodyMode2D::Static,{q(7,8),q(1)},{q(1,8),q(1)});
        DeterministicSession s({1,1,1,4,26});require(configure(s,c,std::move(a)),s.error());advance(s,32);
        expect(s,c,{q(1),q(2)},{},true,floorId,false,"no target landing at X2 retains baseline");}
    {auto a=scene(c);a.emplace_back(35,actor(c,DetBodyMode2D::Static,{q(7),D::fromInt(1)},{q(2),D::fromInt(1)}));
        DeterministicSession s({1,1,1,4,26});require(configure(s,c,std::move(a)),s.error());advance(s,16);
        expect(s,c,{q(3),D::fromInt(1)},{},true,stepId,true,"across wall clips X3/4 and preserves zero VX");}
    {auto a=scene(c);a.emplace_back(50,actor(c,DetBodyMode2D::MovingObstacle,{{},q(6)},{D::fromInt(2),q(1)},{q(2),{}}));
        const auto cp=ordinary(std::move(a),"moving roof response rejects trial");
        require(cp.actors.at(50).pose.position[0]==q(1,8).bits() && cp.actors.at(50).pose.position[1]==q(6).bits(),"moving roof retains full dt ordinary motion, not a partial trial");}
    {DeterministicSession s({1,1,1,4,26});require(configure(s,c,scene(c)),s.error());advance(s,16,true);
        expect(s,c,{q(1),q(6)},{{},D::fromInt(4)},false,0,false,"supported jump suppresses step");}
    {auto a=scene(c);DetCharacterState2D initial;initial.enabled=true;initial.velocity.y=D::fromInt(1);
        a[0].second.blocks[c.stateSchema]=detCharacterState2D(c,initial);DeterministicSession s({1,1,1,4,26});require(configure(s,c,std::move(a)),s.error());advance(s,16);
        expect(s,c,{q(1),q(3)},{{},D::fromInt(1)},false,0,false,"initial rising character does not request a step");}
    for(bool moving:{false,true}){auto a=scene(c);a[1].second=actor(c,moving?DetBodyMode2D::MovingObstacle:DetBodyMode2D::Static,{{},-q(2)},{D::fromInt(16),q(2)},{},false,!moving);
        ordinary(std::move(a),moving?"moving support does not request a step":"one-way support does not request a step");}
    {auto a=scene(c);a[2].second=actor(c,DetBodyMode2D::Static,{q(5),q(1)},{q(2),q(1)},{},false,true);
        DeterministicSession s({1,1,1,4,26});require(configure(s,c,std::move(a)),s.error());advance(s,16);
        expect(s,c,{D::fromInt(1),q(2)},{D::fromInt(4),{}},true,floorId,false,"one-way target sides pass; no static step candidate");}
    {auto a=scene(c);a[2].second=actor(c,DetBodyMode2D::MovingObstacle,{q(5),q(1)},{q(2),q(1)});
        ordinary(std::move(a),"MovingObstacle target is excluded even at zero prescribed speed");}
    std::cout<<"PASS dyadic left/right, result reset, height/roof/landing fallback, clipped across velocity, moving world, jump/rising and one-way eligibility\n";
}
Actors twoCandidates(const DetCharacterController2DConfig& c){auto a=scene(c);
    a.emplace_back(11,actor(c,DetBodyMode2D::Kinematic,{D::fromInt(8),q(2)},{q(2),q(2)},{},true));
    a.emplace_back(31,actor(c,DetBodyMode2D::Static,{q(37),q(1)},{q(2),q(1)}));return a;}
void arbitrationChecks(){const auto c=policy();
    for(bool warmup:{false,true}){DeterministicSession a({1,1,1,4,26}),b({1,1,1,4,26});
        require(configure(a,c,twoCandidates(c)) && configure(b,c,twoCandidates(c),true),"two-candidate fixture seal");
        if(warmup){advance(a,0,false,false,true);advance(b,0,false,false,true);}
        advance(a,16,false,false,true);advance(b,16,false,false,true);
        require(bytes(a)==bytes(b),"registration order preserves candidate arbitration full checkpoint");
        expect(a,c,{warmup?q(1):D::fromInt(1),warmup?q(2):D::fromInt(1)},warmup?V{}:V{D::fromInt(4),{}},true,warmup?floorId:stepId,!warmup,"tick modulo chooses first or second actor");
        expect(a,c,{warmup?D::fromInt(9):q(33),warmup?D::fromInt(1):q(2)},warmup?V{D::fromInt(4),{}}:V{},true,warmup?31:floorId,warmup,"other candidate stays ordinary",11);}
    std::cout<<"PASS two stable-ID candidates, tick0/1 modulo choice and reverse registration complete bytes\n";
}
void triggerChecks(){const auto c=policy();auto a=scene(c);addTraceTrigger(a,c);
    DeterministicSession s({1,1,1,4,26});require(configure(s,c,std::move(a)),s.error());advance(s,16);
    expect(s,c,{D::fromInt(1),D::fromInt(1)},{D::fromInt(4),{}},true,stepId,true,"trigger sees chosen step path");
    const auto cp=checkpoint(s);require(cp.pendingEvents.size()==2,"transient trigger enters/exits only along chosen path");
    for(unsigned k=0;k<2;++k){const auto& e=cp.pendingEvents[k];Bytes expected(24);expected[0]=1;expected[4]=std::uint8_t(k?3:1);expected[8]=10;expected[16]=60;
        require(e.source==c.collision.systemId && e.sequence==k && e.type==c.collision.eventType && e.payload==expected,"real trigger identity and hand-authored24byte payload");}
    auto limited=c;limited.collision.maxTriggerPairs=1;auto crowded=scene(limited);addTraceTrigger(crowded,limited);addTraceTrigger(crowded,limited,61,q(3,8));
    DeterministicSession fallback({1,1,1,4,26});require(configure(fallback,limited,std::move(crowded)),fallback.error());advance(fallback,16);
    expect(fallback,limited,{q(1),q(2)},{},true,floorId,false,"trial crossing budget rejects without partial commit");
    require(checkpoint(fallback).pendingEvents.empty(),"discarded trial emits no events");
    auto baselineInvalid=scene(limited);for(auto id:{60u,61u})baselineInvalid.emplace_back(id,actor(limited,DetBodyMode2D::Static,{{},q(2)},{q(1),q(1)},{},false,false,true));
    DeterministicSession fault({1,1,1,4,26});require(configure(fault,limited,std::move(baselineInvalid)),fault.error());const auto initial=checkpoint(fault);
    require(!fault.advance(frame(0,16)) && fault.faulted(),"a valid step cannot conceal invalid ordinary trigger budget");
    require(fault.restore(initial) && bytes(fault)==encodeDetCheckpoint(initial),"explicit restore after baseline budget fault");
    std::cout<<"PASS selected physical trigger segments, scratch budget fallback, baseline budget fault and explicit restore\n";
}
void rejectRestore(DeterministicSession& s,const DetSessionCheckpoint& invalid,const std::string& why){const auto before=bytes(s);
    require(!s.restore(invalid) && !s.faulted() && bytes(s)==before,why+" atomic rejection");}
void contractChecks(){const auto c=policy();
    for(unsigned change=0;change<8;++change){auto bad=c;
        if(change==0)bad.advanced=false;if(change==1)bad.collision.extended=false;if(change==2)bad.collision.oneWayPlatforms=false;
        if(change==3)bad.collision.maxStepHeight={};if(change==4)bad.collision.maxStepHeight=D::fromBits(0x7f800000u);
        if(change==5)bad.collision.maxStepHeight=D::fromInt(1001);if(change==6)bad.collision.maxTriggerPairs=60;
        if(change==7)bad.collision.stepUp=false;
        DeterministicSession s({1,1,1,4,26});require(!installDetCharacterController2D(s,bad),"invalid step policy rejects before installation");}
    {DeterministicSession s({1,1,1,4,26});require(!installDetCollision2D(s,c.collision),"public collision installer rejects facade-private step profile");
        require(configure(s,c,scene(c)),"rejected standalone install did not consume IDs");}
    {DetCharacterState2D authored;authored.enabled=true;authored.stepped=true;bool rejected=false;
        try{(void)detCharacterState2D(c,authored);}catch(const std::invalid_argument&){rejected=true;}require(rejected,"actor factory cannot author step output");}
    for(bool advanced:{false,true}){auto old=c;old.advanced=advanced;old.collision.oneWayPlatforms=advanced;old.collision.stepUp=false;old.collision.maxStepHeight={};
        DeterministicSession previous({1,1,1,4,26});require(configure(previous,old,scene(old)),previous.error());advance(previous,16);const auto cp=checkpoint(previous);
        require(cp.actors.at(hero).pose.position[0]==q(1).bits() && cp.actors.at(hero).pose.position[1]==q(2).bits(),"legacy/advanced default-off path still stops at ordinary wall");
        require(cp.actors.at(hero).blocks.at(old.collision.bodySchema).size()==(advanced?12u:10u),"old body field widths remain unchanged");
        DetSessionCheckpoint decoded;std::string error;const auto encoded=encodeDetCheckpoint(cp);
        require(decodeDetCheckpoint(encoded,decoded,error) && encodeDetCheckpoint(decoded)==encoded,"old profile checkpoint format round-trips unchanged");}
    DeterministicSession s({1,1,1,4,26});require(configure(s,c,scene(c)),s.error());const auto initial=checkpoint(s);
    for(auto field:{DetBodyStepRequested,DetBodyStepped}){auto cp=initial;setBody(cp,c,field,true);rejectRestore(s,cp,"committed internal body flag");}
    {auto cp=initial;setState(cp,c,DetCharacterStepped,true);rejectRestore(s,cp,"initial last-tick step output");}
    advance(s,16);const auto stepped=checkpoint(s);require(state<bool>(stepped,c,DetCharacterStepped),"restore fixture actually stepped");
    {auto cp=stepped;setBody(cp,c,DetBodyVelocity,V{D::fromInt(4),-D::fromInt(2)});rejectRestore(s,cp,"stepped bodyY must be numerical zero");}
    {auto cp=stepped;setState(cp,c,DetCharacterSupportVelocity,V{D::fromInt(1),{}});rejectRestore(s,cp,"stepped cached carrier must be numerical zero");}
    {auto wrong=c;wrong.collision.maxStepHeight=q(1);DeterministicSession other({1,1,1,4,26});require(configure(other,wrong,scene(wrong)),other.error());
        rejectRestore(other,stepped,"different immutable step height identity");}
    // Despawn queues after post. The fresh retired-support boundary is legal,
    // but it cannot bypass the step result's cached-carrier/body-Y invariants.
    DeterministicSession retired({1,1,1,4,26});require(configure(retired,c,scene(c),false,true),retired.error());advance(retired,16);
    const auto retiredCp=checkpoint(retired);require(state<bool>(retiredCp,c,DetCharacterStepped) && !retiredCp.actors.contains(stepId)
        && std::binary_search(retiredCp.retiredIds.begin(),retiredCp.retiredIds.end(),stepId),"actual stepped support retired at committed boundary");
    require(retired.restore(retiredCp),"fresh retired stepped support restores");
    {auto cp=retiredCp;setState(cp,c,DetCharacterSupportVelocity,V{D::fromInt(1),{}});rejectRestore(retired,cp,"retired support cannot hide malformed stepped carrier");}
    {auto cp=retiredCp;setBody(cp,c,DetBodyVelocity,V{D::fromInt(4),-D::fromInt(2)});rejectRestore(retired,cp,"retired support cannot hide malformed stepped bodyY");}
    advance(retired);expect(retired,c,{D::fromInt(1),D::fromInt(1)},{},false,0,false,"next pre releases retired stepped support");
    // Queries capture geometry, not an automatic prediction of a stepped path.
    const DetCollisionQuery2D query(stepped,c.collision);DetCollisionQueryFilter2D filter;filter.layer=1;filter.mask=2;
    const auto hits=query.overlap(ayt::math::DetAabb2::fromCenterHalf({q(5),q(1)},{q(1,16),q(1,16)}),filter);
    require(hits==std::vector<SimEntityId>{stepId},"profile4 query reads exact geometry");
    for(auto field:{DetBodyStepRequested,DetBodyStepped}){auto cp=stepped;setBody(cp,c,field,true);bool rejected=false;
        try{const DetCollisionQuery2D invalid(cp,c.collision);(void)invalid;}catch(const std::invalid_argument&){rejected=true;}require(rejected,"query rejects transient committed flag");}
    {auto wrong=c.collision;wrong.stepUp=false;wrong.maxStepHeight={};bool rejected=false;
        try{const DetCollisionQuery2D mismatch(stepped,wrong);(void)mismatch;}catch(const std::invalid_argument&){rejected=true;}require(rejected,"profile3 query rejects profile4 capture");}
    std::cout<<"PASS explicit policy/private installer, factory output, atomic typed restore, retired-support invariants and pure profile4 queries\n";
}
struct Evidence {std::vector<Bytes> checkpoints;std::vector<DetConfirmedEvent> events;std::vector<std::uint64_t> hashes;};
Actors closureScene(const DetCharacterController2DConfig& c){auto a=scene(c);addTraceTrigger(a,c);return a;}
DetTickInput closureInput(std::uint64_t tick){return frame(tick,tick==1?16:0);}
Evidence closureChecks(){const auto c=policy();DeterministicSession a({1,1,1,4,26}),b({1,1,1,4,26});
    require(configure(a,c,closureScene(c)) && configure(b,c,closureScene(c),true),"closure sessions seal");Evidence evidence;
    evidence.checkpoints.push_back(bytes(a));evidence.hashes.push_back(detCheckpointHash(checkpoint(a)));require(bytes(a)==bytes(b),"initial full bytes reverse registration");
    std::vector<DetSessionCheckpoint> checkpoints{checkpoint(a)};
    for(std::uint64_t tick=0;tick<closureTicks;++tick){auto in=closureInput(tick);require(a.advance(in) && b.advance(in),"short baseline advance");
        require(bytes(a)==bytes(b),"every boundary full checkpoint reverse registration");const auto cp=checkpoint(a);checkpoints.push_back(cp);
        evidence.checkpoints.push_back(encodeDetCheckpoint(cp));evidence.hashes.push_back(detCheckpointHash(cp));
        for(const auto& event:cp.pendingEvents)evidence.events.push_back({1,tick,event});
        expect(a,c,tick==0?V{{},q(2)}:v(1,1),tick==1?V{D::fromInt(4),{}}:V{},true,tick==0?floorId:stepId,tick==1,"short hand-calculated baseline");}
    require(evidence.events.size()==2,"short actual step transient confirmed events");
    require(a.restore(checkpoints[2]) && bytes(a)==evidence.checkpoints[2],"restore boundary with stepped output true");
    for(std::uint64_t tick=2;tick<closureTicks;++tick){require(a.advance(closureInput(tick)),a.error());require(bytes(a)==evidence.checkpoints[tick+1],"restore continuation exact full bytes");}
    std::cout<<"PASS short8-tick hand motion, all complete checkpoint bytes reverse registration and mid-step restore continuation\n";return evidence;
}
std::pair<std::uint64_t,std::uint64_t> rollbackChecks(const Evidence& evidence){const auto c=policy();DeterministicSession s({1,1,1,4,26});require(configure(s,c,closureScene(c),true),s.error());
    DetRollbackConfig rc;rc.members={1};rc.historyTicks=16;rc.maxPredictionTicks=4;rc.prediction={{1,DetPredictionMode::Hold}};DeterministicRollback owner(s,rc);
    std::vector<DetConfirmedEvent> effects;
    for(std::uint64_t tick=0;tick<closureTicks;++tick){if(!tick)require(owner.submit(1,closureInput(0)),owner.error());
        if(tick>=2)require(owner.submit(1,closureInput(tick-2)),owner.error());require(owner.advance(),owner.error());
        const auto cp=owner.checkpointAt(owner.confirmedNextTick());require(cp && encodeDetCheckpoint(*cp)==evidence.checkpoints[owner.confirmedNextTick()],"late input confirmed boundary full bytes");
        const auto released=owner.takeConfirmedEvents();effects.insert(effects.end(),released.begin(),released.end());}
    for(auto tick=closureTicks-2;tick<closureTicks;++tick)require(owner.submit(1,closureInput(tick)),owner.error());
    const auto released=owner.takeConfirmedEvents();effects.insert(effects.end(),released.begin(),released.end());
    require(owner.confirmedNextTick()==closureTicks && bytes(s)==evidence.checkpoints.back(),"late input corrects final complete state");
    require(effects==evidence.events && owner.takeConfirmedEvents().empty(),"late input real event identities/payloads exactly once");
    require(owner.rollbackCount()>0 && owner.replayedTicks()>0,"short fixture genuinely corrects predicted steps");
    std::cout<<"PASS short late-input rollback, corrected step results and exactly-once real trigger bytes\n";return {owner.rollbackCount(),owner.replayedTicks()};
}
void replayChecks(const fs::path& directory,const Evidence& evidence){const auto c=policy();DeterministicSession recording({1,1,1,4,26});require(configure(recording,c,closureScene(c)),recording.error());
    DetReplayWriter writer;require(writer.begin(recording,(directory/"steps").string(),2),writer.error());
    for(std::uint64_t tick=0;tick<closureTicks;++tick){require(writer.advance(closureInput(tick)),writer.error());require(bytes(recording)==evidence.checkpoints[tick+1],"actual recording full checkpoint");}
    require(writer.finish() && fs::is_regular_file(writer.path()),"actual rpl completion seal/file");
    DeterministicSession playback({1,1,1,4,26});require(configure(playback,c,closureScene(c),true),playback.error());DetReplayReader reader;
    require(reader.open(writer.path()) && reader.restoreInitial(playback),reader.error());require(reader.tickCount()==closureTicks,"real recorded8 ticks");
    while(!reader.atEnd()){require(reader.advance(playback),reader.error());require(bytes(playback)==evidence.checkpoints[playback.nextTick()],"actual playback each full checkpoint");}
    for(std::uint64_t target:{2u,1u,5u}){require(reader.seek(playback,target) && bytes(playback)==evidence.checkpoints[target],"real seek restores stepped/initial/ordinary boundary");
        while(!reader.atEnd()){require(reader.advance(playback),reader.error());require(bytes(playback)==evidence.checkpoints[playback.nextTick()],"real seek continuation complete state");}}
    std::cout<<"PASS actual sealed .rpl full-byte replay and3 short checkpoint seeks\n";
}
std::string quote(const std::string& value){std::string result="\"";for(unsigned char b:value){if(b=='\\'||b=='\"'){result+='\\';result+=char(b);}else if(b<32){const char* hex="0123456789abcdef";result+="\\u00";result+=hex[b>>4];result+=hex[b&15];}else result+=char(b);}return result+'\"';}
void write(const fs::path& file,const std::string& text){std::ofstream out(file,std::ios::binary);out<<text;require(bool(out),"artifact write "+file.string());}
fs::path fresh(const fs::path& parent){require(fs::is_directory(parent),"--output must name an existing parent directory");const auto stamp=std::chrono::steady_clock::now().time_since_epoch().count();
    for(unsigned attempt=0;attempt<1024;++attempt){auto dir=parent/("charactersteps-"+std::to_string(stamp)+"-"+std::to_string(attempt));if(fs::create_directory(dir))return dir;}throw std::runtime_error("fresh artifact directory unavailable");}
} // namespace
int main(int argc,char** argv){fs::path directory;try{fs::path parent=fs::temp_directory_path();if(argc==3 && std::string(argv[1])=="--output")parent=argv[2];
    else if(argc!=1){std::cerr<<"usage: [--output EXISTING_PARENT_DIRECTORY]\n";return 2;}directory=fresh(parent);
    write(directory/"progress.json","{\"status\":\"running\",\"phase\":\"directed\"}\n");directedChecks();arbitrationChecks();triggerChecks();contractChecks();
    write(directory/"progress.json","{\"status\":\"running\",\"phase\":\"checkpoint-rollback-replay\"}\n");const auto evidence=closureChecks();const auto [rollbacks,replayed]=rollbackChecks(evidence);replayChecks(directory,evidence);
    std::ostringstream trace;for(std::size_t tick=0;tick<evidence.hashes.size();++tick)trace<<tick<<' '<<evidence.hashes[tick]<<'\n';write(directory/"tick-hashes.txt",trace.str());
    std::ostringstream summary;summary<<"{\"status\":\"passed\",\"ticks\":"<<closureTicks<<",\"checkpointRows\":"<<evidence.checkpoints.size()<<",\"confirmedEvents\":"<<evidence.events.size()
        <<",\"rollbacks\":"<<rollbacks<<",\"replayedTicks\":"<<replayed<<",\"replayFiles\":1,\"controllerProfile\":3,\"collisionProfile\":4,\"finalHash\":\""<<evidence.hashes.back()<<"\",\"artifacts\":"<<quote(directory.string())<<"}\n";
    write(directory/"summary.json",summary.str());write(directory/"progress.json","{\"status\":\"passed\",\"phase\":\"complete\"}\n");std::cout<<summary.str();return 0;
}catch(const std::exception& e){if(!directory.empty())try{write(directory/"summary.json","{\"status\":\"failed\",\"error\":"+quote(e.what())+"}\n");write(directory/"progress.json","{\"status\":\"failed\"}\n");}catch(...){}std::cerr<<e.what()<<'\n';return 1;}}
