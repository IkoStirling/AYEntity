// design reference: AYEntity/design.md Stage21; registered controller/support and real replay.
// Independent dyadic movement tables, complete checkpoints and real file replay.
#include "../DetCharacterControllerScenario.h"
#include <AYEntity/DeterministicReplay.h>
#include <AYEntity/DeterministicRollbackReplay.h>
#include <chrono>
#include <cfenv>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#if defined(_M_X64) || defined(__x86_64__)
#include <xmmintrin.h>
#endif

using namespace ayt::entity;
namespace fs=std::filesystem;
namespace {
using D=detcharacter_scenario::D;
using V=detcharacter_scenario::V;
using Bytes=std::vector<std::uint8_t>;
using Actors=std::vector<std::pair<SimEntityId,DetActorState>>;
constexpr SimEntityId hero=10,platform=20;
void require(bool condition,const std::string& message){if(!condition)throw std::runtime_error(message);}
D q(int numerator,int denominator=4){return detcharacter_scenario::ratio(numerator,denominator);}
V vec(int x,int y){return V::fromInts(x,y);}
DetSessionCheckpoint checkpoint(DeterministicSession& s){auto cp=s.checkpoint();require(cp.has_value(),s.error());return *cp;}
Bytes bytes(DeterministicSession& s){return encodeDetCheckpoint(checkpoint(s));}
template<class T>T state(const DetSessionCheckpoint& cp,const DetCharacterController2DConfig& c,
        std::uint32_t field){return readDetState<T>(detcharacter_scenario::stateSchema(c),cp.actors.at(hero).blocks.at(c.stateSchema),field);}
template<class T>void setState(DetSessionCheckpoint& cp,const DetCharacterController2DConfig& c,
        std::uint32_t field,T value){writeDetState(detcharacter_scenario::stateSchema(c),cp.actors.at(hero).blocks.at(c.stateSchema),field,value);}
std::size_t lane(const DetTypedStateSchema& schema,std::uint32_t id){
    std::size_t offset=0;for(const auto& f:schema.fields){if(f.id==id)return offset;offset+=encodeDetStateValue(f.initial).size();}
    throw std::runtime_error("fixture field not found");
}
void expect(DeterministicSession& s,const DetCharacterController2DConfig& c,D x,D y,D vx,D vy,
        bool grounded,SimEntityId support,const std::string& label){
    const auto cp=checkpoint(s);const auto& position=cp.actors.at(hero).pose.position;
    require(D::fromBits(position[0])==x && D::fromBits(position[1])==y,label+" position expectedBits="
        +std::to_string(x.bits())+","+std::to_string(y.bits())+" actualBits="
        +std::to_string(position[0])+","+std::to_string(position[1]));
    const auto own=state<V>(cp,c,DetCharacterVelocity);
    require(own.x==vx && own.y==vy,label+" own velocity expectedBits="+std::to_string(vx.bits())
        +","+std::to_string(vy.bits())+" actualBits="+std::to_string(own.x.bits())+","+std::to_string(own.y.bits()));
    require(state<bool>(cp,c,DetCharacterGrounded)==grounded
        && state<DetEntityRef>(cp,c,DetCharacterSupport).value==support,label+" support");
    require(state<std::uint64_t>(cp,c,DetCharacterSupportTick)==(support?cp.nextTick:0),label+" support stamp");
    for(auto field:{DetCharacterJumpRequested,DetCharacterJumped,DetCharacterSnapped})
        require(!state<bool>(cp,c,field),label+" consumed transient flags");
    for(auto field:{DetCharacterAppliedCarry,DetCharacterIntentVelocity}){
        const auto v=state<V>(cp,c,field);require(v.x.isZero() && v.y.isZero(),label+" cleared transient vectors");}
}
DetCharacterController2DConfig smallPolicy(){
    auto c=detcharacter_scenario::policy();c.acceleration=D::fromInt(4);c.deceleration=D::fromInt(2);
    c.jumpSpeed=D::fromInt(4);return c;
}
Actors floorActors(const DetCharacterController2DConfig& c,V carrierVelocity={},D halfWidth=D::fromInt(8),
        V position=vec(0,1),bool moving=false){
    return {{hero,detcharacter_scenario::actor(c,DetBodyMode2D::Kinematic,position,{q(2),q(2)},{},1,2,false,true)},
        {platform,detcharacter_scenario::actor(c,moving?DetBodyMode2D::MovingObstacle:DetBodyMode2D::Static,
            {},{halfWidth,q(2)},carrierVelocity,2,1)}};
}
int signedByte(std::uint8_t byte){return byte<128?byte:int(byte)-256;}
std::uint8_t byte(int value){require(value>=-128 && value<=127,"fixture byte range");return static_cast<std::uint8_t>(value);}
// Test commands use registered input. All callback captures are immutable.
// 1 target quarters+jump; 2 carrier velocity quarters; 3 despawn.
// 4 is an internal geometry-gap regression stimulus (carrierY/64), not a gameplay
// movement recipe or a claim of continuous collision paths for teleported platforms.
// Normal prescribed platform movement only sets velocity and uses the sole solver.
bool configureSmall(DeterministicSession& s,const DetCharacterController2DConfig& c,Actors actors,bool reverse=false){
    auto control=[c](DetTickContext& ctx){
        for(const auto& in:ctx.input().commands){
            if(in.source!=1)return false;
            if(in.type==1 && in.payload.size()==2 && in.payload[1]<=1)
                detCharacterInput2D(ctx,hero,c,{q(signedByte(in.payload[0])),in.payload[1]!=0});
            else if(in.type==2 && in.payload.size()==2)
                ctx.write(platform,c.collision.bodySchema,DetBodyVelocity,V{q(signedByte(in.payload[0])),q(signedByte(in.payload[1]))});
            else if(in.type==3 && in.payload.empty())ctx.despawn(platform);
            else if(in.type==4 && in.payload.size()==1){auto p=ctx.pose(platform).position;
                p.y=q(signedByte(in.payload[0]),64);if(!ctx.pose(platform).setPosition(p))return false;}
            else return false;
        }return true;
    };
    if(reverse){if(!installDetCharacterController2D(s,c) || !s.registerSystem(10,0,control))return false;}
    else if(!s.registerSystem(10,0,control) || !installDetCharacterController2D(s,c))return false;
    if(reverse)std::reverse(actors.begin(),actors.end());
    for(auto& [id,a]:actors)if(!s.addEntity(id,std::move(a)))return false;return s.seal();
}
DetTickInput frame(DeterministicSession& s,int target=0,bool jump=false){return {s.nextTick(),1,{{1,0,1,{byte(target),std::uint8_t(jump)}}}};}
void advance(DeterministicSession& s,DetTickInput in,const std::string& label){require(s.advance(std::move(in)),label+": "+s.error());}
void carrier(DetTickInput& in,int x,int y){in.commands.push_back({1,1,2,{byte(x),byte(y)}});}

void movementChecks(){
    const auto c=smallPolicy();DeterministicSession s({1,1,1,4,0});
    require(configureSmall(s,c,floorActors(c)),s.error());
    // accel4*1/4=1. Cap2. Decel2*1/4=1/2. Positions in eighths.
    const int accelX[]={2,6,10},accelV[]={4,8,8};
    for(unsigned i=0;i<3;++i){advance(s,frame(s,8),"acceleration");
        expect(s,c,q(accelX[i],8),D::fromInt(1),q(accelV[i]),{},true,platform,"acceleration row"+std::to_string(i));}
    const int stopX[]={13,15,16,16},stopV[]={6,4,2,0};
    for(unsigned i=0;i<4;++i){advance(s,frame(s),"deceleration");
        expect(s,c,q(stopX[i],8),D::fromInt(1),q(stopV[i]),{},true,platform,"deceleration row"+std::to_string(i));}
    advance(s,{s.nextTick(),1,{}} ,"persistent neutral");expect(s,c,D::fromInt(2),D::fromInt(1),{},{},true,platform,"stopped");

    DeterministicSession jump({1,1,1,4,0});require(configureSmall(jump,c,floorActors(c)),jump.error());
    // The supported jump replaces this tick's gravity velocity with4. Subsequent
    // velocities3,2,1,0,-1,-2,-3 reach floor on the ninth tick.
    const int jumpY4[]={8,11,13,14,14,13,11,8,4};const int jumpV[]={4,3,2,1,0,-1,-2,-3,0};
    for(unsigned i=0;i<9;++i){advance(jump,frame(jump,0,i<2),"jump");
        expect(jump,c,{},q(jumpY4[i]),D{},D::fromInt(jumpV[i]),i==8,i==8?platform:0,"jump row"+std::to_string(i));}
    advance(jump,frame(jump),"post landing");expect(jump,c,{},D::fromInt(1),{},{},true,platform,"air jump request consumed");
    auto cap=c;cap.maxFallSpeed=D::fromInt(2);
    DeterministicSession fall({1,1,1,4,0});auto actors=floorActors(cap,{},D::fromInt(8),vec(0,4));actors.pop_back();
    require(configureSmall(fall,cap,std::move(actors)),fall.error());
    for(unsigned i=0;i<3;++i){advance(fall,frame(fall,0,true),"fall");
        const int y4[]={15,13,11},vy[]={-1,-2,-2};
        expect(fall,cap,{},q(y4[i]),{},D::fromInt(vy[i]),false,0,"fall clamp/air jump");}
    auto zero=c;zero.gravity=zero.jumpSpeed=zero.maxFallSpeed={};
    DeterministicSession idle({1,1,1,4,0});require(configureSmall(idle,zero,floorActors(zero,{q(2),{}},D::fromInt(8),vec(0,1),true)),idle.error());
    advance(idle,frame(idle,0,true),"zero jump");expect(idle,zero,q(1,8),D::fromInt(1),{},{},true,platform,"zero jump retains carry");
    // Probe is discovery, not contact: positive gap1/32 exceeds tolerance1/1024.
    auto gapPolicy=zero;gapPolicy.jumpSpeed=D::fromInt(4);
    DeterministicSession gap({1,1,1,4,0});require(configureSmall(gap,gapPolicy,floorActors(gapPolicy,{},D::fromInt(8),V{{},q(33,32)})),gap.error());
    advance(gap,frame(gap,0,true),"positive gap");expect(gap,gapPolicy,{},q(33,32),{},{},false,0,"probe gap not ground/air jump ignored");
    DeterministicSession roof({1,1,1,4,0});auto roofActors=floorActors(c);
    roofActors.push_back({30,detcharacter_scenario::actor(c,DetBodyMode2D::Static,vec(0,3),
        {D::fromInt(8),q(2)},{},2,1)});
    require(configureSmall(roof,c,std::move(roofActors)),roof.error());
    advance(roof,frame(roof,0,true),"head contact");
    expect(roof,c,{},D::fromInt(2),{},{},false,0,"roof clips own upward velocity without ground");
    advance(roof,frame(roof),"roof fall");expect(roof,c,{},q(7),{},D::fromInt(-1),false,0,"gravity after roof");
    std::cout<<"PASS hand-calculated acceleration, deceleration, gravity clamp, jump consumption and contact gap\n";
}

void platformChecks(){
    auto c=smallPolicy();c.gravity={};c.deceleration=q(1); //1/16 per tick, exposes accidental reverse velocity.
    DeterministicSession s({1,1,1,4,0});require(configureSmall(s,c,floorActors(c,{q(2),{}},D::fromInt(8),vec(0,1),true)),s.error());
    for(unsigned i=1;i<=4;++i){advance(s,frame(s),"horizontal platform");
        expect(s,c,q(i,8),D::fromInt(1),{},{},true,platform,"horizontal carry once");}
    auto in=frame(s);carrier(in,-2,0);advance(s,in,"carrier reversal");expect(s,c,q(3,8),D::fromInt(1),{},{},true,platform,"current reversed carrier");
    in=frame(s);carrier(in,0,2);advance(s,in,"carrier vertical");expect(s,c,q(3,8),q(9,8),{},{},true,platform,"vertical carry");
    in=frame(s);carrier(in,0,-2);advance(s,in,"carrier vertical reversal");expect(s,c,q(3,8),D::fromInt(1),{},{},true,platform,"downward current carry");
    in=frame(s);carrier(in,0,0);advance(s,in,"carrier stop");expect(s,c,q(3,8),D::fromInt(1),{},{},true,platform,"no stale carry");
    const auto support=state<V>(checkpoint(s),c,DetCharacterSupportVelocity);require(support.x.isZero()&&support.y.isZero(),"support velocity updated to zero");
    // Internal registered-state boundary stimulus: move support pose downward
    // before pre to form a legal nonpenetrating gap. This is not gameplay movement
    // or platform teleport path coverage. Character adhesion must pass through
    // the solver; no test system touches the character pose.
    in=frame(s);in.commands.push_back({1,1,4,{byte(-4)}});advance(s,in,"snap down1/16");
    expect(s,c,q(3,8),q(15,16),{},{},true,platform,"snap within distance");
    in=frame(s);in.commands.push_back({1,1,4,{byte(-20)}});advance(s,in,"beyond snap");
    expect(s,c,q(3,8),q(15,16),{},{},false,0,"snap bound no teleport");

    for(auto leave:{DetCharacterLeaveVelocity2D::Discard,DetCharacterLeaveVelocity2D::Inherit}){
        auto p=c;p.leaveVelocity=leave;DeterministicSession gone({1,1,1,4,0});
        require(configureSmall(gone,p,floorActors(p,{q(2),q(2)},D::fromInt(8),vec(0,1),true)),gone.error());
        advance(gone,frame(gone),"support warmup");
        in=frame(gone);in.commands.push_back({1,1,3,{}});advance(gone,in,"queued support despawn");
        const auto transition=checkpoint(gone);require(transition.retiredIds==std::vector<SimEntityId>{platform}
            && state<DetEntityRef>(transition,p,DetCharacterSupport).value==platform,"fresh issued-retired transition");
        require(gone.restore(transition),gone.error());advance(gone,frame(gone),"release after restored despawn");
        const bool inherit=leave==DetCharacterLeaveVelocity2D::Inherit;
        // InheritedX.5 decelerates once by1/16. InheritedY.5 persists with gravity0.
        expect(gone,p,inherit?q(23,64):q(1),inherit?q(11,8):q(5,4),inherit?q(7,16):D{},inherit?q(2):D{},false,0,"despawn leave policy");
        advance(gone,frame(gone),"release only once");
        expect(gone,p,inherit?q(29,64):q(1),inherit?q(3,2):q(5,4),inherit?q(3,8):D{},inherit?q(2):D{},false,0,"leave inheritance not accumulated");
        // Exact edge touch is not positive support overlap.
        auto edgePolicy=p;edgePolicy.acceleration=D::fromInt(8);
        DeterministicSession edge({1,1,1,4,0});auto edgeActors=floorActors(edgePolicy,{q(2),{}},q(2),vec(0,1),true);
        require(configureSmall(edge,edgePolicy,std::move(edgeActors)),edge.error());
        advance(edge,frame(edge,8),"walk toward edge");
        require(state<bool>(checkpoint(edge),edgePolicy,DetCharacterGrounded),"still positive edge overlap");
        advance(edge,frame(edge,8),"leave edge");
        require(!state<bool>(checkpoint(edge),edgePolicy,DetCharacterGrounded)
            && state<DetEntityRef>(checkpoint(edge),edgePolicy,DetCharacterSupport).value==0,"exact-touch edge release");
        const auto edgeOwn=state<V>(checkpoint(edge),edgePolicy,DetCharacterVelocity);
        require(edgeOwn.x==(inherit?q(10):q(8)),"edge inheritance once");
    }
    // Downward worldY does not mean motion toward a faster descending support.
    // A1/16 jump from a -.5 carrier has worldY=-7/16, relativeY=+1/16.
    // After its first tick the gap is exactly the1/1024 contact tolerance.
    auto down=c;down.jumpSpeed=q(1,16);down.leaveVelocity=DetCharacterLeaveVelocity2D::Inherit;
    DeterministicSession downward({1,1,1,64,0});
    auto downActors=floorActors(down,{D{},q(-2)},D::fromInt(8),vec(0,1),true);
    // A grounded stamp0 is a valid initial boundary. An ungrounded stationary
    // actor above a descending support is relatively ascending and cannot jump.
    DetCharacterState2D standing;standing.enabled=standing.grounded=true;standing.support={platform};
    standing.supportVelocity={D{},q(-2)};
    downActors[0].second.blocks[down.stateSchema]=detCharacterState2D(down,standing);
    require(configureSmall(downward,down,std::move(downActors)),downward.error());
    advance(downward,frame(downward,0,true),"small jump from descending support");
    expect(downward,down,{},q(1017,1024),{},q(-7,16),false,0,"relative upward descending-platform jump");
    advance(downward,frame(downward),"descending support must not recapture rising actor");
    expect(downward,down,{},q(1010,1024),{},q(-7,16),false,0,"no relative-upward re-ground");
    // Already inherited ownX may legitimately reach maxHorizontal+maxPlatform.
    // A further jump inheritance is capped once instead of faulting/accumulating.
    auto limit=c;limit.maxHorizontalSpeed=D::fromInt(1);limit.maxPlatformSpeed=D::fromInt(2);
    limit.acceleration=limit.deceleration={};limit.leaveVelocity=DetCharacterLeaveVelocity2D::Inherit;
    auto limitedActors=floorActors(limit,{D::fromInt(2),{}},D::fromInt(8),vec(0,1),true);
    DetCharacterState2D inherited;inherited.enabled=inherited.grounded=true;inherited.support={platform};
    inherited.velocity={D::fromInt(3),{}};inherited.supportVelocity={D::fromInt(2),{}};
    limitedActors[0].second.blocks[limit.stateSchema]=detCharacterState2D(limit,inherited);
    DeterministicSession cap({1,1,1,4,0});require(configureSmall(cap,limit,std::move(limitedActors)),cap.error());
    advance(cap,frame(cap,4,true),"repeated inherited momentum cap");
    expect(cap,limit,q(3),D::fromInt(2),D::fromInt(3),D::fromInt(4),false,0,"inherited ownX capped at3");
    advance(cap,frame(cap,4),"capped inherited continuation");
    expect(cap,limit,q(3,2),D::fromInt(3),D::fromInt(3),D::fromInt(4),false,0,"capped momentum no repeated inheritance");
    // Opposite carriers separate at equal top height. Walk to exact old edge on
    // tick1: support20 ->30, but Inherit must not add the old-.5 kick.
    auto switching=c;switching.acceleration=D::fromInt(8);switching.leaveVelocity=DetCharacterLeaveVelocity2D::Inherit;
    Actors switchActors={
        {hero,detcharacter_scenario::actor(switching,DetBodyMode2D::Kinematic,vec(0,1),{q(2),q(2)},{},1,2,false,true)},
        {20,detcharacter_scenario::actor(switching,DetBodyMode2D::MovingObstacle,{q(-2),{}},{q(2),q(2)},{q(-2),{}},2,1)},
        {30,detcharacter_scenario::actor(switching,DetBodyMode2D::MovingObstacle,{q(2),{}},{q(2),q(2)},{q(2),{}},2,1)}};
    DeterministicSession switched({1,1,1,4,0});require(configureSmall(switched,switching,std::move(switchActors)),switched.error());
    advance(switched,frame(switched,8),"direct carrier switch");
    expect(switched,switching,q(3,8),D::fromInt(1),D::fromInt(2),{},true,30,"direct switch no old kick");
    require(state<V>(checkpoint(switched),switching,DetCharacterSupportVelocity).x==q(2),"new carrier captured");
    advance(switched,frame(switched,8),"new carrier movement");
    expect(switched,switching,D::fromInt(1),D::fromInt(1),D::fromInt(2),{},true,30,"new carry added once");
    auto maskActors=floorActors(c);writeDetState(detcharacter_scenario::bodySchema(c),
        maskActors[1].second.blocks[c.collision.bodySchema],DetBodyMask,std::uint32_t{0});
    DeterministicSession masked({1,1,1,4,0});require(configureSmall(masked,c,std::move(maskActors)),masked.error());
    advance(masked,frame(masked),"nonreciprocal support mask");
    expect(masked,c,{},D::fromInt(1),{},{},false,0,"one-way mask cannot create support");
    // Wall blocks only the character; platform passes underneath. Clipped carry
    // cannot manufacture negative ownX, including after the carrier is destroyed.
    DeterministicSession wall({1,1,1,4,0});auto actors=floorActors(c,{q(2),{}},D::fromInt(8),vec(0,1),true);
    actors.push_back({30,detcharacter_scenario::actor(c,DetBodyMode2D::Static,{D::fromInt(1),D::fromInt(2)},
        {q(2),D::fromInt(1)},{},2,1)});
    require(configureSmall(wall,c,std::move(actors)),wall.error());
    for(unsigned i=0;i<4;++i){advance(wall,frame(wall),"wall carry clip");expect(wall,c,{},D::fromInt(1),{},{},true,platform,"wall no reverse own velocity");}
    in=frame(wall);in.commands.push_back({1,1,3,{}});advance(wall,in,"wall support despawn");
    for(unsigned i=0;i<3;++i){advance(wall,frame(wall),"wall released");expect(wall,c,{},D::fromInt(1),{},{},false,0,"released no left drift");}
    std::cout<<"PASS horizontal/vertical/current platform velocity, bounded snap, edge/despawn policies and clipped carry\n";
}

void tieAndRestoreChecks(){
    auto c=smallPolicy();c.gravity={};
    auto tiled=[c](){return Actors{
        {hero,detcharacter_scenario::actor(c,DetBodyMode2D::Kinematic,vec(0,1),{q(2),q(2)},{},1,2,false,true)},
        {20,detcharacter_scenario::actor(c,DetBodyMode2D::Static,{q(-2),{}},{q(2),q(2)},{},2,1)},
        {30,detcharacter_scenario::actor(c,DetBodyMode2D::Static,{q(2),{}},{q(2),q(2)},{},2,1)}};};
    DeterministicSession a({1,1,1,4,0}),b({1,1,1,4,0});require(configureSmall(a,c,tiled())&&configureSmall(b,c,tiled(),true),"tie setup");
    for(unsigned i=0;i<4;++i){advance(a,frame(a),"tie forward");advance(b,frame(b),"tie reverse");
        expect(a,c,{},D::fromInt(1),{},{},true,20,"stable ID support tie");require(bytes(a)==bytes(b),"tie full checkpoint registration/insertion order");}
    auto original=checkpoint(a);const auto unchanged=bytes(a);
    auto reject=[&](const std::string& label,auto mutate){auto bad=original;mutate(bad);
        require(!a.restore(bad),label+" restore rejected");require(bytes(a)==unchanged,label+" restore atomic");};
    reject("unknown support",[&](auto& cp){setState(cp,c,DetCharacterSupport,DetEntityRef{999});});
    reject("support stamp future",[&](auto& cp){setState(cp,c,DetCharacterSupportTick,cp.nextTick+1);});
    reject("support stamp stale",[&](auto& cp){setState(cp,c,DetCharacterSupportTick,cp.nextTick-1);});
    reject("disabled noncanonical",[&](auto& cp){setState(cp,c,DetCharacterEnabled,false);});
    reject("grounded missing support",[&](auto& cp){setState(cp,c,DetCharacterSupport,DetEntityRef{});});
    reject("grounded own vertical",[&](auto& cp){setState(cp,c,DetCharacterVelocity,V{{},q(1)});});
    reject("wrong carrier velocity",[&](auto& cp){setState(cp,c,DetCharacterSupportVelocity,V{q(1),{}});});
    reject("committed carry",[&](auto& cp){setState(cp,c,DetCharacterAppliedCarry,V{q(1),{}});});
    reject("committed intent",[&](auto& cp){setState(cp,c,DetCharacterIntentVelocity,V{q(1),{}});});
    reject("unconsumed jump",[&](auto& cp){setState(cp,c,DetCharacterJumpRequested,true);});
    reject("committed jumped",[&](auto& cp){setState(cp,c,DetCharacterJumped,true);});
    reject("committed snapped",[&](auto& cp){setState(cp,c,DetCharacterSnapped,true);});
    reject("target above policy",[&](auto& cp){setState(cp,c,DetCharacterTargetSpeed,D::fromInt(3));});
    reject("nonfinite pose",[](auto& cp){cp.actors.at(hero).pose.position[0]=0x7f800000u;});
    reject("noncanonical Boolean",[&](auto& cp){auto& words=cp.actors.at(hero).blocks.at(c.stateSchema);
        words[lane(detcharacter_scenario::stateSchema(c),DetCharacterEnabled)]=2;});
    reject("nonfinite own lane",[&](auto& cp){auto& words=cp.actors.at(hero).blocks.at(c.stateSchema);
        words[lane(detcharacter_scenario::stateSchema(c),DetCharacterVelocity)]=0x7fc00000u;});
    reject("enabled static body",[&](auto& cp){writeDetState(detcharacter_scenario::bodySchema(c),cp.actors.at(hero).blocks.at(c.collision.bodySchema),DetBodyMode,std::uint32_t(DetBodyMode2D::Static));});
    // A nearby geometric relationship alone cannot bypass issued identity/stamp.
    reject("stale retired support",[&](auto& cp){cp.actors.erase(20);cp.retiredIds.push_back(20);++cp.nextTick;});
    auto changed=c;changed.leaveVelocity=DetCharacterLeaveVelocity2D::Inherit;
    DeterministicSession incompatible({1,1,1,4,0});require(configureSmall(incompatible,changed,tiled()),incompatible.error());
    const auto other=bytes(incompatible);require(a.manifest()!=incompatible.manifest()
        && !incompatible.restore(original) && bytes(incompatible)==other,"immutable controller policy manifest mismatch");
    require(a.restore(original),a.error());advance(a,frame(a),"valid restore continuation");advance(b,frame(b),"baseline continuation");
    require(bytes(a)==bytes(b),"restored tie continues exact full state");
    std::cout<<"PASS stable support tie and malformed/foreign restore rejection with atomic full-state preservation\n";
}

void registrationChecks(){
    for(unsigned conflict=0;conflict<4;++conflict){DeterministicSession s({1,1,1,4,0});auto c=smallPolicy();
        if(conflict==0)require(s.registerSchema({c.stateSchema,1,{1}}),"existing schema");
        if(conflict==1)require(s.registerSystem(c.preSystemId,0,[](auto&){return true;}),"existing system");
        if(conflict==2)require(s.registerValidator(c.validatorId,1,[](const auto&,auto&){return true;}),"existing validator");
        if(conflict==3)require(s.registerLogicProfile(c.logicProfileId,1,17),"existing logic");
        require(!installDetCharacterController2D(s,c),"existing registration conflict rejected before mutations");
        if(conflict==0)c.stateSchema=16;
        if(conflict==1)c.preSystemId=19;
        if(conflict==2)c.validatorId=41;
        if(conflict==3)c.logicProfileId=41;
        require(installDetCharacterController2D(s,c)&&s.seal(),"nonconflicting config succeeds after rejected install");
        advance(s,{0,1,{}},"nonconflicting empty pipeline");
    }
    // At/near the public64-entry boundary, compare an independently configured
    // control to expose partial schema/global/system registration on refusal.
    for(auto [schemas,count]:{std::pair{true,61u},std::pair{true,64u},std::pair{false,62u},std::pair{false,64u}}){
        DeterministicSession attempted({1,1,1,4,0}),control({1,1,1,4,0});
        for(auto* s:{&attempted,&control})for(unsigned i=0;i<count;++i){
            if(schemas)require(s->registerSchema({100+i,1,{1}}),"capacity schema");
            else require(s->registerSystem(100+i,0,[](auto&){return true;}),"capacity system");}
        require(!installDetCharacterController2D(attempted,smallPolicy()),"insufficient registration capacity rejected");
        require(attempted.seal()&&control.seal(),"capacity remains sealable");
        require(attempted.manifest()==control.manifest()&&bytes(attempted)==bytes(control),"capacity refusal leaves exact configuration/state");
        advance(attempted,{0,1,{}},"capacity attempted");advance(control,{0,1,{}},"capacity control");
        require(bytes(attempted)==bytes(control),"capacity refusal leaves callbacks intact");
    }
    DeterministicSession existing({1,1,1,4,0});const auto c=smallPolicy();
    require(installDetCollision2D(existing,c.collision),"existing collision facade");
    require(!installDetCharacterController2D(existing,c)&&existing.seal(),"existing collision cannot be adopted implicitly");
    advance(existing,{0,1,{}},"existing collision unaffected");
}
void configAndInputChecks(){
    registrationChecks();
    using Change=std::function<void(DetCharacterController2DConfig&)>;
    const std::vector<std::pair<std::string,Change>> invalid={
        {"NaN speed",[](auto& c){c.maxHorizontalSpeed=D::fromBits(0x7fc00000u);}},
        {"infinite gravity",[](auto& c){c.gravity=D::fromBits(0x7f800000u);}},
        {"negative acceleration",[](auto& c){c.acceleration=-D::fromInt(1);}},
        {"speed above bound",[](auto& c){c.maxHorizontalSpeed=D::fromInt(1000001);}},
        {"zero platform bound",[](auto& c){c.maxPlatformSpeed={};}},
        {"probe beyond snap",[](auto& c){c.groundProbeDistance=D::fromInt(1);}},
        {"tolerance beyond probe",[](auto& c){c.groundContactTolerance=D::fromInt(1);}},
        {"distance above bound",[](auto& c){c.snapDistance=D::fromInt(1001);}},
        {"wrong policy enum",[](auto& c){c.leaveVelocity=static_cast<DetCharacterLeaveVelocity2D>(2);}},
        {"collision profile1",[](auto& c){c.collision.extended=false;}},
        {"same state schema",[](auto& c){c.stateSchema=c.collision.bodySchema;}},
        {"same policy schema",[](auto& c){c.policySchema=c.stateSchema;}},
        {"pre after collision",[](auto& c){c.preSystemId=31;}},
        {"post before collision",[](auto& c){c.postSystemId=29;}},
        {"zero logic profile",[](auto& c){c.logicProfileId=0;}},
        {"body capacity zero",[](auto& c){c.collision.maxBodies=0;}}};
    for(const auto& [label,mutate]:invalid){DeterministicSession s({1,1,1,4,0});auto bad=smallPolicy();mutate(bad);
        require(!installDetCharacterController2D(s,bad),label+" config rejection");
        require(installDetCharacterController2D(s,smallPolicy()),label+" preflight leaves registration untouched");}
    auto c=smallPolicy();c.groundContactTolerance=c.groundProbeDistance=c.snapDistance={};
    DeterministicSession zero({1,1,1,4,0});require(configureSmall(zero,c,floorActors(c)),zero.error());
    advance(zero,frame(zero),"zero distances");expect(zero,c,{},D::fromInt(1),{},{},true,platform,"zero distance exact contact");
    c=smallPolicy();c.maxHorizontalSpeed=c.acceleration=c.deceleration=c.gravity=c.jumpSpeed=c.maxFallSpeed=c.maxPlatformSpeed=D::fromInt(1000000);
    DeterministicSession bound({1,1,1,4,0});auto actor=floorActors(c,{},D::fromInt(8),vec(0,100));actor.pop_back();
    require(configureSmall(bound,c,std::move(actor)),bound.error());advance(bound,{0,1,{}},"finite policy endpoint");
    const auto end=checkpoint(bound);require(D::fromBits(end.actors.at(hero).pose.position[1]).isFinite()
        && state<V>(end,c,DetCharacterVelocity).y==D::fromInt(-250000),"finite inclusive policy bound");
    auto enabledStatic=detcharacter_scenario::actor(c,DetBodyMode2D::Static,vec(0,100),{q(2),q(2)},{},1,2,false,true);
    DeterministicSession invalidActor({1,1,1,4,0});require(!configureSmall(invalidActor,c,{{hero,std::move(enabledStatic)}}),"enabled controller requires solid Kinematic");
    // Catch only the public helper's pure argument error. Registered typed access
    // faults are separately covered by Session; the bad input changes no fields.
    c=smallPolicy();DeterministicSession caught({1,1,1,4,0});
    require(caught.registerTypedSchema({8,1,{{10,std::uint64_t{0}}}})&&caught.registerGlobalState(8),"caught input schema");
    require(caught.registerSystem(10,0,[c](DetTickContext& ctx){unsigned rejects=0;
        for(auto target:{D::fromBits(0x7fc00000u),D::fromBits(0x7f800000u),D::fromInt(3)}){
            bool threw=false;try{detCharacterInput2D(ctx,hero,c,{target,true});}catch(const std::exception&){threw=true;}
            require(threw,"invalid helper target throws");++rejects;
            require(ctx.read<D>(hero,c.stateSchema,DetCharacterTargetSpeed).isZero()
                && !ctx.read<bool>(hero,c.stateSchema,DetCharacterJumpRequested),"invalid helper input atomic");}
        ctx.writeGlobal(8,10,std::uint64_t(rejects));return true;
    }) && installDetCharacterController2D(caught,c),"caught helper setup");
    for(auto& [id,a]:floorActors(c))require(caught.addEntity(id,std::move(a)),"caught helper actor");
    require(caught.seal(),caught.error());advance(caught,{0,1,{}},"caught pure argument errors");
    require(checkpoint(caught).globals.at(8)==std::vector<std::uint64_t>{3}&&!caught.faulted(),"pure argument failures not hidden input or sticky context failure");
    DeterministicSession failed({1,1,1,4,0});require(configureSmall(failed,c,floorActors(c)),failed.error());
    const auto initial=checkpoint(failed);require(!failed.advance(frame(failed,12,true))&&failed.faulted(),"uncaught invalid helper faults execution");
    require(failed.restore(initial)&&!failed.faulted(),"explicit restore recovers helper failure");
    advance(failed,frame(failed),"restored after helper fault");expect(failed,c,{},D::fromInt(1),{},{},true,platform,"fault recovery exact contact");
    // A disabled controller retains canonical zero controller words while normal
    // profile2 body velocity still performs collision motion.
    auto disabledActors=floorActors(c);disabledActors[0].second.blocks[c.stateSchema]=detCharacterState2D(c);
    writeDetState(detcharacter_scenario::bodySchema(c),disabledActors[0].second.blocks[c.collision.bodySchema],DetBodyVelocity,V{q(2),{}});
    DeterministicSession disabled({1,1,1,4,0});require(configureSmall(disabled,c,std::move(disabledActors)),disabled.error());
    advance(disabled,{0,1,{}},"disabled ordinary collision");const auto ordinary=checkpoint(disabled);
    require(ordinary.actors.at(hero).pose.position[0]==q(1,8).bits()
        && ordinary.actors.at(hero).blocks.at(c.stateSchema)==detCharacterState2D(c),"disabled state zero, collision body motion retained");
    // Both authoring inputs are finite, but their box endpoint sum overflows.
    // Geometry validation must reject this before any authoritative tick.
    DeterministicSession overflow({1,1,1,4,0});const auto huge=D::fromBits(0x7f7fffffu);
    auto enormous=detcharacter_scenario::actor(c,DetBodyMode2D::Kinematic,{huge,D::fromInt(1)},
        {huge,q(2)},{},1,2,false,true);
    require(!configureSmall(overflow,c,{{hero,std::move(enormous)}}),"finite box endpoint overflow rejects seal");
    std::cout<<"PASS invalid preflight, finite endpoints, zero-distance contact and pure input argument atomicity\n";
}

struct ScenarioEvidence{std::vector<Bytes> states;std::vector<std::uint64_t> hashes;std::vector<DetConfirmedEvent> events;};
ScenarioEvidence scenarioChecks(){
    DeterministicSession a({1,1,1,64,0}),b({1,1,1,64,0});
    require(detcharacter_scenario::configure(a)&&detcharacter_scenario::configure(b,true),"shared fixture setup");
    ScenarioEvidence evidence;auto remember=[&](const DetSessionCheckpoint& cp){evidence.states.push_back(encodeDetCheckpoint(cp));evidence.hashes.push_back(detCheckpointHash(cp));};
    require(a.manifest()==b.manifest()&&detcharacter_scenario::matchesGolden(checkpoint(a)),"initial full fixture");remember(checkpoint(a));
    DetSessionCheckpoint midpoint,late;
    for(std::uint64_t tick=0;tick<detcharacter_scenario::maxTicks;++tick){
        advance(a,detcharacter_scenario::input(tick),"forward shared fixture");advance(b,detcharacter_scenario::input(tick,true),"reversed shared fixture");
        const auto cp=checkpoint(a);require(detcharacter_scenario::matchesGolden(cp),"independent lattice/event oracle tick"+std::to_string(cp.nextTick));
        require(encodeDetCheckpoint(cp)==bytes(b),"complete checkpoint stable insertion/input order");remember(cp);
        for(const auto& event:cp.pendingEvents)evidence.events.push_back({1,tick,event});
        if(cp.nextTick==1024)midpoint=cp;if(cp.nextTick==1536)late=cp;
    }
    require(!evidence.events.empty(),"trigger event oracle actually exercised");
    require(b.restore(late)&&b.restore(midpoint),b.error());
    for(auto tick=midpoint.nextTick;tick<detcharacter_scenario::maxTicks;++tick){advance(b,detcharacter_scenario::input(tick,true),"restored fixture");
        require(bytes(b)==evidence.states[tick+1],"restore1024 continues all fields exactly");}
    std::cout<<"PASS 2048-tick independent lattice/events, every complete checkpoint in two insertion orders, restore1536->1024\n";
    return evidence;
}
DetTickInput memberInput(DetTickInput input,std::uint32_t member){std::erase_if(input.commands,[member](const auto& c){return c.source!=member;});return input;}
std::pair<std::uint64_t,std::uint64_t> rollbackChecks(const ScenarioEvidence& evidence){
    DeterministicSession s({1,1,1,64,0});require(detcharacter_scenario::configure(s,true),"late rollback fixture");
    DetRollbackConfig c;c.members={1,2};c.historyTicks=32;c.maxPredictionTicks=8;
    c.prediction={{1,DetPredictionMode::Hold},{2,DetPredictionMode::Hold}};DeterministicRollback owner(s,c);
    std::vector<DetConfirmedEvent> effects;
    for(std::uint64_t tick=0;tick<detcharacter_scenario::maxTicks;++tick){auto in=detcharacter_scenario::input(tick,true);
        require(owner.submit(2,memberInput(in,2)),owner.error());
        if(!tick)require(owner.submit(1,memberInput(in,1)),owner.error());
        if(tick>=3)require(owner.submit(1,memberInput(detcharacter_scenario::input(tick-3),1)),owner.error());
        require(owner.advance(),owner.error());const auto frontier=owner.confirmedNextTick();const auto cp=owner.checkpointAt(frontier);
        require(cp && encodeDetCheckpoint(*cp)==evidence.states[frontier],"late input confirmed full checkpoint vs real baseline");
        auto e=owner.takeConfirmedEvents();effects.insert(effects.end(),e.begin(),e.end());
        if(tick==1535){const auto before=bytes(s);require(owner.replayFrom(s.nextTick()-16),owner.error());
            require(bytes(s)==before&&owner.takeConfirmedEvents().empty(),"explicit retained replay no duplicated confirmed effect");}
    }
    for(auto tick=detcharacter_scenario::maxTicks-3;tick<detcharacter_scenario::maxTicks;++tick)
        require(owner.submit(1,memberInput(detcharacter_scenario::input(tick),1)),owner.error());
    auto e=owner.takeConfirmedEvents();effects.insert(effects.end(),e.begin(),e.end());
    require(owner.confirmedNextTick()==detcharacter_scenario::maxTicks && bytes(s)==evidence.states.back(),"late input final state");
    require(effects==evidence.events && owner.takeConfirmedEvents().empty(),"real confirmed events identities/payloads equal once");
    require(owner.rollbackCount()>1&&owner.replayedTicks()>0&&owner.oldestTick()>1900,"actual corrections and bounded history");
    std::cout<<"PASS actual 3-tick late movement inputs, corrected full checkpoints and exactly-once confirmed effects\n";
    return {owner.rollbackCount(),owner.replayedTicks()};
}

void writeText(const fs::path& path,const std::string& text){std::ofstream out(path,std::ios::binary);out<<text;require(bool(out),"artifact write "+path.string());}
std::string jsonString(const std::string& value){std::string out="\"";for(unsigned char c:value){
    if(c=='\\'||c=='\"'){out+='\\';out+=char(c);}else if(c<32){const char* h="0123456789abcdef";out+="\\u00";out+=h[c>>4];out+=h[c&15];}else out+=char(c);}return out+'\"';}
fs::path fresh(const fs::path& parent){require(fs::exists(parent)&&fs::is_directory(parent),"--output must name an existing parent directory");
    const auto stamp=std::chrono::steady_clock::now().time_since_epoch().count();
    for(unsigned attempt=0;attempt<1024;++attempt){auto path=parent/("charactercontroller-"+std::to_string(stamp)+"-"+std::to_string(attempt));
        if(fs::create_directory(path))return path;}throw std::runtime_error("unable to reserve fresh artifact directory");}
std::uint32_t replayChecks(const fs::path& directory,const ScenarioEvidence& evidence){
    DeterministicSession recording({1,1,1,64,0});require(detcharacter_scenario::configure(recording),"rpl fixture");
    DetReplayWriter writer;require(writer.begin(recording,(directory/"character-single").string(),128),writer.error());
    for(std::uint64_t tick=0;tick<detcharacter_scenario::maxTicks;++tick){require(writer.advance(detcharacter_scenario::input(tick)),writer.error());
        require(bytes(recording)==evidence.states[tick+1],"actual single file recording full state");}
    require(writer.finish(),writer.error());require(fs::is_regular_file(writer.path()),"actual .rpl file exists");
    DeterministicSession playback({1,1,1,64,0});require(detcharacter_scenario::configure(playback,true),"rpl playback fixture");
    DetReplayReader reader;require(reader.open(writer.path())&&reader.restoreInitial(playback),reader.error());
    require(reader.tickCount()==detcharacter_scenario::maxTicks,"sealed rpl tick count");
    while(!reader.atEnd()){require(reader.advance(playback),reader.error());require(bytes(playback)==evidence.states[playback.nextTick()],"actual rpl tick full checkpoint");}
    require(reader.seek(playback,1024)&&bytes(playback)==evidence.states[1024],"actual rpl seek1024");
    while(!reader.atEnd()){require(reader.advance(playback),reader.error());require(bytes(playback)==evidence.states[playback.nextTick()],"actual rpl seek continuation");}

    DeterministicSession archiveSession({1,1,1,64,0});require(detcharacter_scenario::configure(archiveSession),"archive fixture");
    DetRollbackNetworkConfig nc;nc.sessionId=210;nc.localMember=nc.recoveryMember=1;nc.rollback.members={1};
    nc.rollback.historyTicks=32;nc.rollback.maxPredictionTicks=4;DeterministicRollbackNetwork network(archiveSession,nc);
    DetRollbackReplayWriter archive;DetRollbackReplayArchiveOptions options;options.maxSegmentRecords=257;
    require(archive.begin(network,(directory/"character-archive.rpl").string(),options,128),archive.error());
    std::vector<DetConfirmedEvent> recorded;
    for(std::uint64_t tick=0;tick<detcharacter_scenario::maxTicks;++tick){
        require(network.submitLocal(memberInput(detcharacter_scenario::input(tick),1))&&network.advance(),network.error());
        require(archive.sync(network),archive.error());require(bytes(archiveSession)==evidence.states[tick+1],"archive real confirmed checkpoint");
        auto e=network.takeConfirmedEvents();recorded.insert(recorded.end(),e.begin(),e.end());}
    require(archive.finish(network),archive.error());require(recorded==evidence.events,"archive emitted real event baseline");
    const auto inspected=DetRollbackReplayArchive::inspect(archive.path());
    require(inspected.valid&&!inspected.recovery&&inspected.files.size()>1&&fs::is_regular_file(archive.path()),"actual sealed indexed multi-file recording");
    DeterministicSession archivePlayback({1,1,1,64,0});require(detcharacter_scenario::configure(archivePlayback,true),"archive replay fixture");
    DetRollbackReplayReader ar;require(ar.open(archive.path())&&ar.restoreInitial(archivePlayback),ar.error());
    std::vector<DetConfirmedEvent> replayed;
    while(!ar.atEnd()){require(ar.advance(archivePlayback),ar.error());
        require(bytes(archivePlayback)==evidence.states[archivePlayback.nextTick()],"indexed replay every full checkpoint");
        auto e=ar.takeConfirmedEvents();replayed.insert(replayed.end(),e.begin(),e.end());}
    require(replayed==evidence.events,"indexed replay exact confirmed event identities/payloads");
    for(auto tick:{1024u,128u,1536u}){require(ar.seek(archivePlayback,1,tick),ar.error());
        require(bytes(archivePlayback)==evidence.states[tick]&&ar.takeConfirmedEvents().empty(),"cross-segment seek checkpoint and silent effects");
        while(!ar.atEnd()){require(ar.advance(archivePlayback),ar.error());require(bytes(archivePlayback)==evidence.states[archivePlayback.nextTick()],"cross-segment seek continues full state");(void)ar.takeConfirmedEvents();}}
    std::ostringstream trace;for(std::size_t tick=0;tick<evidence.hashes.size();++tick)trace<<tick<<' '<<evidence.hashes[tick]<<'\n';
    writeText(directory/"tick-hashes.txt",trace.str());
    std::cout<<"PASS actual .rpl and segmented .rpi recording/replay, per-tick full checkpoints, events and cross-segment seek\n";
    return static_cast<std::uint32_t>(inspected.files.size());
}
} // namespace

int main(int argc,char** argv){fs::path directory;try{
    fs::path parent=fs::temp_directory_path();if(argc==3&&std::string(argv[1])=="--output")parent=argv[2];
    else if(argc!=1){std::cerr<<"usage: [--output EXISTING_PARENT_DIRECTORY]\n";return 2;}
    directory=fresh(parent);writeText(directory/"progress.json","{\"status\":\"running\",\"phase\":\"directed\"}\n");
    std::fesetround(FE_UPWARD);
#if defined(_M_X64) || defined(__x86_64__)
    _mm_setcsr(_mm_getcsr()|0x8040u);
#endif
    movementChecks();platformChecks();tieAndRestoreChecks();configAndInputChecks();
    writeText(directory/"progress.json","{\"status\":\"running\",\"phase\":\"checkpoint-rollback-replay\"}\n");
    const auto evidence=scenarioChecks();const auto [rollbacks,replayed]=rollbackChecks(evidence);const auto files=replayChecks(directory,evidence);
    std::ostringstream summary;summary<<"{\"status\":\"passed\",\"ticks\":2048,\"checkpointRows\":"<<evidence.states.size()
        <<",\"confirmedEvents\":"<<evidence.events.size()<<",\"rollbacks\":"<<rollbacks<<",\"replayedTicks\":"<<replayed
        <<",\"archiveFiles\":"<<files<<",\"finalHash\":"<<evidence.hashes.back()<<",\"artifacts\":"<<jsonString(directory.string())<<"}\n";
    writeText(directory/"summary.json",summary.str());writeText(directory/"progress.json","{\"status\":\"passed\",\"phase\":\"complete\"}\n");
    std::cout<<summary.str();return 0;
}catch(const std::exception& e){if(!directory.empty()){
    try{writeText(directory/"summary.json","{\"status\":\"failed\",\"error\":"+jsonString(e.what())+"}\n");
        writeText(directory/"progress.json","{\"status\":\"failed\"}\n");}catch(...){}}
    std::cerr<<e.what()<<'\n';return 1;}}
