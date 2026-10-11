// design reference: AYEntity/design.md Stage22; opt-in windows, top planes, registered shape.
// Short hand-calculated fixtures plus complete-state rollback and real file replay.
#include "../DetCharacterAdvancedScenario.h"
#include <AYEntity/DeterministicReplay.h>
#include <AYEntity/DeterministicRollbackReplay.h>
#include <AYEntity/DeterministicCollisionQuery2D.h>
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
using D=detcharacter_advanced::D;using V=detcharacter_advanced::V;
using Bytes=std::vector<std::uint8_t>;
using Actors=std::vector<std::pair<SimEntityId,DetActorState>>;
constexpr SimEntityId hero=10,platform=20;
void require(bool ok,const std::string& why){if(!ok)throw std::runtime_error(why);}
D q(int n,int d=4){return detcharacter_advanced::ratio(n,d);}
V vec(int x,int y){return V::fromInts(x,y);}
DetSessionCheckpoint checkpoint(DeterministicSession& s){auto cp=s.checkpoint();require(bool(cp),s.error());return *cp;}
Bytes bytes(DeterministicSession& s){return encodeDetCheckpoint(checkpoint(s));}
template<class T>T state(const DetSessionCheckpoint& cp,const DetCharacterController2DConfig& c,std::uint32_t f){
    return readDetState<T>(detcharacter_advanced::stateSchema(c),cp.actors.at(hero).blocks.at(c.stateSchema),f);}
template<class T>void setState(DetSessionCheckpoint& cp,const DetCharacterController2DConfig& c,std::uint32_t f,T value){
    writeDetState(detcharacter_advanced::stateSchema(c),cp.actors.at(hero).blocks.at(c.stateSchema),f,value);}
template<class T>T body(const DetSessionCheckpoint& cp,const DetCharacterController2DConfig& c,std::uint32_t f,SimEntityId id=hero){
    return readDetState<T>(detcharacter_advanced::bodySchema(c),cp.actors.at(id).blocks.at(c.collision.bodySchema),f);}
template<class T>void setBody(DetSessionCheckpoint& cp,const DetCharacterController2DConfig& c,std::uint32_t f,T value,SimEntityId id=hero){
    writeDetState(detcharacter_advanced::bodySchema(c),cp.actors.at(id).blocks.at(c.collision.bodySchema),f,value);}
std::size_t lane(const DetTypedStateSchema& schema,std::uint32_t id){std::size_t offset=0;
    for(const auto& f:schema.fields){if(f.id==id)return offset;offset+=encodeDetStateValue(f.initial).size();}
    throw std::runtime_error("fixture field missing");}
DetCharacterController2DConfig smallPolicy(){auto c=detcharacter_advanced::policy();
    c.coyoteTicks=c.jumpBufferTicks=c.dropThroughTicks=0;c.gravity={};c.jumpSpeed=D::fromInt(4);return c;}
Actors floorActors(const DetCharacterController2DConfig& c,bool oneWay=true,D width=D::fromInt(8),
        V position=vec(0,1),V carrierVelocity={},SimEntityId id=platform){
    return {{hero,detcharacter_advanced::actor(c,DetBodyMode2D::Kinematic,position,{q(2),q(2)},{},1,2,false,true)},
        {id,detcharacter_advanced::actor(c,carrierVelocity==V{}?DetBodyMode2D::Static:DetBodyMode2D::MovingObstacle,
            {},{width,q(2)},carrierVelocity,2,1,false,false,oneWay)}};}
int signedByte(std::uint8_t b){return b<128?b:int(b)-256;}
std::uint8_t byte(int n){require(n>=-128 && n<=127,"fixture byte bound");return std::uint8_t(n);}
// Commands:1 character target quarters + held buttons;2 platform velocity;
// 3 queued platform despawn;4 ordinary body velocity. All captures immutable.
bool configureSmall(DeterministicSession& s,const DetCharacterController2DConfig& c,Actors actors,
        SimEntityId platformId=platform,bool reverse=false,bool controller=true){
    auto control=[c,platformId](DetTickContext& ctx){for(const auto& in:ctx.input().commands){
        if(in.source!=1)return false;
        if(in.type==1 && in.payload.size()==4 && in.payload[1]<=1 && in.payload[2]<=1 && in.payload[3]<=1)
            detCharacterInput2D(ctx,hero,c,{q(signedByte(in.payload[0])),bool(in.payload[1]),bool(in.payload[2]),bool(in.payload[3])});
        else if(in.type==2 && in.payload.size()==2)
            ctx.write(platformId,c.collision.bodySchema,DetBodyVelocity,V{q(signedByte(in.payload[0])),q(signedByte(in.payload[1]))});
        else if(in.type==3 && in.payload.empty())ctx.despawn(platformId);
        else if(in.type==4 && in.payload.size()==2)
            ctx.write(hero,c.collision.bodySchema,DetBodyVelocity,V{q(signedByte(in.payload[0])),q(signedByte(in.payload[1]))});
        else return false;}return true;};
    auto install=[&](){return controller?installDetCharacterController2D(s,c):installDetCollision2D(s,c.collision);};
    if(reverse){if(!install() || !s.registerSystem(10,0,control))return false;}
    else if(!s.registerSystem(10,0,control) || !install())return false;
    if(reverse)std::reverse(actors.begin(),actors.end());
    for(auto& [id,a]:actors)if(!s.addEntity(id,std::move(a)))return false;return s.seal();
}
DetTickInput frame(DeterministicSession& s,int target=0,bool jump=false,bool crouch=false,bool drop=false){
    return {s.nextTick(),1,{{1,0,1,{byte(target),std::uint8_t(jump),std::uint8_t(crouch),std::uint8_t(drop)}}}};}
DetTickInput raw(DeterministicSession& s,int x,int y){return {s.nextTick(),1,{{1,0,4,{byte(x),byte(y)}}}};}
void advance(DeterministicSession& s,DetTickInput in,const std::string& why){require(s.advance(std::move(in)),why+": "+s.error());}
void expect(DeterministicSession& s,const DetCharacterController2DConfig& c,D x,D y,D vx,D vy,bool ground,SimEntityId support,const std::string& why){
    const auto cp=checkpoint(s);const auto& p=cp.actors.at(hero).pose.position;const auto own=state<V>(cp,c,DetCharacterVelocity);
    require(p[0]==x.bits() && p[1]==y.bits(),why+" position expectedBits="+std::to_string(x.bits())+","+std::to_string(y.bits())+" actualBits="+std::to_string(p[0])+","+std::to_string(p[1]));
    require(own==V{vx,vy},why+" own velocity expectedBits="+std::to_string(vx.bits())+","+std::to_string(vy.bits())+" actualBits="+std::to_string(own.x.bits())+","+std::to_string(own.y.bits()));
    require(state<bool>(cp,c,DetCharacterGrounded)==ground && state<DetEntityRef>(cp,c,DetCharacterSupport).value==support,why+" support");
    require(state<std::uint64_t>(cp,c,DetCharacterSupportTick)==(support?cp.nextTick:0),why+" support stamp");
    for(auto f:{DetCharacterJumpRequested,DetCharacterJumped,DetCharacterSnapped,DetCharacterDropStarted})require(!state<bool>(cp,c,f),why+" internal flags clear");
    for(auto f:{DetCharacterAppliedCarry,DetCharacterIntentVelocity})require(state<V>(cp,c,f)==V{},why+" internal intent clear");
}

void windowChecks(){
    // Half-width.5 + actor half.5. Two speed2/step1/4 ticks reach X1:
    // strict X overlap becomes zero on the second boundary, before air-pre1.
    for(auto n:{0u,1u,3u})for(auto opportunity:{std::max(1u,n),n+1}){
        auto c=smallPolicy();c.coyoteTicks=n;DeterministicSession s({1,1,1,4,0});
        require(configureSmall(s,c,floorActors(c,false,q(2))),s.error());
        advance(s,frame(s,8),"coyote walk1");advance(s,frame(s,8),"coyote exact edge");
        expect(s,c,D::fromInt(1),D::fromInt(1),D::fromInt(2),{},false,0,"coyote leaves strict edge");
        require(state<std::uint32_t>(checkpoint(s),c,DetCharacterCoyoteRemaining)==n,"last contact grants N opportunities");
        for(unsigned k=1;k<=opportunity;++k)advance(s,frame(s,0,k==opportunity),"coyote opportunity"+std::to_string(k));
        const bool accepted=opportunity<=n;
        expect(s,c,D::fromInt(1),D::fromInt(accepted?2:1),{},D::fromInt(accepted?4:0),false,0,"coyote N="+std::to_string(n)+" opportunity="+std::to_string(opportunity));
        require(state<std::uint32_t>(checkpoint(s),c,DetCharacterCoyoteRemaining)==0,"coyote consumed/expired once");
    }
    for(auto n:{0u,1u,3u})for(bool expired:{false,true}){
        auto c=smallPolicy();c.jumpBufferTicks=n;const unsigned landing=std::max(1u,n+(expired?1u:0u));
        auto a=floorActors(c,true,D::fromInt(8),V{{},D::fromInt(1)+q(int(landing))});
        DetCharacterState2D initial;initial.enabled=true;initial.velocity.y=-D::fromInt(1);
        a[0].second.blocks[c.stateSchema]=detCharacterState2D(c,initial);
        DeterministicSession s({1,1,1,4,0});require(configureSmall(s,c,std::move(a)),s.error());
        // Initial gap=landing/4, ownY=-1, zero gravity: landing is exactly
        // boundary 'landing'. Press-pre0 plus N future opportunities is the API.
        for(unsigned k=0;k<landing;++k){advance(s,frame(s,0,true),"buffer falling held input");
            require(checkpoint(s).actors.at(hero).pose.position[1]==(D::fromInt(1)+q(int(landing-k-1))).bits(),"buffer hand-counted descent");}
        expect(s,c,{},D::fromInt(1),{},{},true,platform,"buffer landing");
        advance(s,{s.nextTick(),1,{}},"buffer held persists without input");
        const bool accepted=landing<=n;
        expect(s,c,{},D::fromInt(accepted?2:1),{},D::fromInt(accepted?4:0),!accepted,accepted?0:platform,"buffer N="+std::to_string(n)+" landing="+std::to_string(landing));
        require(state<std::uint32_t>(checkpoint(s),c,DetCharacterJumpBufferRemaining)==0,"buffer consumes/expires once");
        require(state<bool>(checkpoint(s),c,DetCharacterJumpHeld) && state<bool>(checkpoint(s),c,DetCharacterJumpLatch),"missing input preserves held latch");
    }
    auto c=smallPolicy();c.coyoteTicks=1;c.leaveVelocity=DetCharacterLeaveVelocity2D::Inherit;c.acceleration=c.deceleration={};
    auto a=floorActors(c,true,q(2),vec(0,1),{q(2),q(2)});DetCharacterState2D initial;initial.enabled=true;initial.velocity.x=D::fromInt(2);
    a[0].second.blocks[c.stateSchema]=detCharacterState2D(c,initial);
    DeterministicSession inherit({1,1,1,4,0});require(configureSmall(inherit,c,std::move(a)),inherit.error());
    advance(inherit,frame(inherit,8),"coyote carrier1");advance(inherit,frame(inherit,8),"coyote carrier leave");
    advance(inherit,frame(inherit,8,true),"coyote inherited jump");
    expect(inherit,c,q(15,8),q(19,8),q(10),q(18),false,0,"coyote cached carrier Y, X inherited only once");
    std::cout<<"PASS coyote and buffer0/1/N last/expired opportunities, held/no-input latches and inherited Y without a second X kick\n";
}

void topPlaneChecks(){
    // Raw collision tests exclude controller support/carry decisions. Both ID
    // orientations exercise canonical pair ordering independently of semantics.
    for(auto floorId:{SimEntityId{5},SimEntityId{20}}){
        auto c=smallPolicy();auto a=floorActors(c,true,D::fromInt(4),vec(0,-1),{},floorId);
        a[0].second.blocks.erase(c.stateSchema);DeterministicSession up({1,1,1,4,0});
        require(configureSmall(up,c,std::move(a),floorId,false,false),up.error());
        advance(up,raw(up,0,16),"one-way below-to-inside");require(checkpoint(up).actors.at(hero).pose.position[1]==D{}.bits(),"one-way underside passes into thickness");
        auto inside=checkpoint(up);require(up.restore(inside),"one-way interior checkpoint can restore");
        advance(up,raw(up,0,16),"one-way inside-to-above");require(checkpoint(up).actors.at(hero).pose.position[1]==D::fromInt(1).bits(),"one-way upward top crossing passes");
        advance(up,raw(up,0,-16),"one-way downward zero-time contact");
        require(checkpoint(up).actors.at(hero).pose.position[1]==D::fromInt(1).bits() && body<V>(checkpoint(up),c,DetBodyVelocity).y.isZero(),"one-way downward top contact blocks");
        auto sideActors=floorActors(c,true,q(2),vec(-2,0),{},floorId);sideActors[0].second.blocks.erase(c.stateSchema);
        DeterministicSession side({1,1,1,4,0});require(configureSmall(side,c,std::move(sideActors),floorId,false,false),side.error());
        for(int x=-1;x<=2;++x){advance(side,raw(side,16,0),"one-way side passage");
            require(checkpoint(side).actors.at(hero).pose.position[0]==D::fromInt(x).bits(),"one-way sides never block");}
        // At impactt=.5, platform right=.5. Hero centerX1 has minX=.5:
        // grazing zero overlap passes. A center at3/4 gives1/4 overlap and lands.
        for(bool positive:{false,true}){auto graze=floorActors(c,true,q(2),{positive?q(3):D::fromInt(1),D::fromInt(2)}, {},floorId);
            graze[0].second.blocks.erase(c.stateSchema);DeterministicSession s({1,1,1,4,0});
            require(configureSmall(s,c,std::move(graze),floorId,false,false),s.error());advance(s,raw(s,0,-32),"strict impact overlap");
            require(checkpoint(s).actors.at(hero).pose.position[1]==D::fromInt(positive?1:0).bits(),"strict X overlap at top impact");}
        for(auto velocity:{V{{},-D::fromInt(4)},V{{},D::fromInt(4)}}){
            auto relative=floorActors(c,true,D::fromInt(8),vec(0,2),velocity,floorId);relative[0].second.blocks.erase(c.stateSchema);
            DeterministicSession s({1,1,1,4,0});require(configureSmall(s,c,std::move(relative),floorId,false,false),s.error());
            advance(s,raw(s,0,-16),"relative top plane");
            const auto cp=checkpoint(s);require(cp.actors.at(hero).pose.position[1]==D::fromInt(velocity.y<D{}?1:2).bits(),"relative horizontal/equal vs downward crossing");
            require(body<V>(cp,c,DetBodyVelocity).y==(velocity.y<D{}?-D::fromInt(4):D::fromInt(4)),"moving top normal follows prescribed velocity");}
    }
    auto c=smallPolicy();DeterministicSession ordinary({1,1,1,4,0});auto a=floorActors(c,false,D::fromInt(8),vec(0,0));a[0].second.blocks.erase(c.stateSchema);
    require(!configureSmall(ordinary,c,std::move(a),platform,false,false),"ordinary solid interior still rejects initial penetration");
    DeterministicSession crossing({1,1,1,4,0});auto oneWay=floorActors(c,true,D::fromInt(8),vec(0,0));oneWay[0].second.blocks.erase(c.stateSchema);
    require(configureSmall(crossing,c,std::move(oneWay),platform,false,false),"one-way intentional initial interior accepted");
    const auto inside=checkpoint(crossing);require(crossing.restore(inside),"one-way intentional interior restores");
    std::cout<<"PASS independent top-plane two-ID orientations, underside/interior/side passage, strict edge grazing and relative moving contact\n";
}

void queryProfileChecks(){
    auto c=smallPolicy();c.collision.maxBodies=2;
    auto actors=floorActors(c,true,D::fromInt(1),vec(0,1),vec(1,0));
    actors[0].second.blocks.erase(c.stateSchema);
    writeDetState(detcharacter_advanced::bodySchema(c),actors[0].second.blocks[c.collision.bodySchema],DetBodyIgnoredOneWay,DetEntityRef{platform});
    DeterministicSession s({1,1,1,4,0});
    require(s.registerSystem(40,0,[collision=c.collision](DetTickContext& ctx){
        const DetCollisionQuery2D query(ctx,collision);DetCollisionQueryFilter2D filter;filter.layer=1;filter.mask=2;
        require(ctx.read<bool>(platform,collision.bodySchema,DetBodyOneWay)
            && ctx.read<DetEntityRef>(hero,collision.bodySchema,DetBodyIgnoredOneWay).value==platform,"callback profile3 flags retained");
        require(query.size()==2 && query.overlap(ayt::math::DetAabb2::fromCenterHalf(V{-D::fromInt(1),{}},{}),filter).empty(),"callback query sees current moved profile3 geometry");
        return true;}),"profile3 callback query registration");
    require(configureSmall(s,c,std::move(actors),platform,false,false),s.error());
    const auto initial=checkpoint(s);const auto initialBytes=encodeDetCheckpoint(initial);
    require(body<bool>(initial,c,DetBodyOneWay,platform) && body<DetEntityRef>(initial,c,DetBodyIgnoredOneWay).value==platform,"query fixture contains both profile3 flags");
    const DetCollisionQuery2D old(initial,c.collision);DetCollisionQueryFilter2D filter;filter.layer=1;filter.mask=2;
    const auto center=ayt::math::DetAabb2::fromCenterHalf(V{},V{q(1),q(1)});
    require(old.size()==2 && old.overlap(center,filter)==std::vector<SimEntityId>{platform},"geometric query returns ignored one-way platform");
    // Geometry sees the lower face: ray (-2,+4) enters at3/8; a .5-high
    // sweep centered at-1 with displacement+2 enters at1/8. Collision response
    // intentionally treats that same one-way lower face as traversable.
    const auto ray=old.raycast(vec(0,-2),vec(0,4),filter);
    const auto sweep=old.sweep(ayt::math::DetAabb2::fromCenterHalf(vec(0,-1),V{q(1),q(1)}),vec(0,2),filter);
    require(ray.size()==1 && ray[0].entity==platform && ray[0].hit.fraction.bits()==q(3,8).bits()
        && ray[0].hit.axis==1 && ray[0].hit.normal.y==-D::fromInt(1) && !ray[0].hit.initialOverlap,"one-way query lower ray face remains geometric");
    require(sweep.size()==1 && sweep[0].entity==platform && sweep[0].hit.fraction.bits()==q(1,8).bits()
        && sweep[0].hit.axis==1 && sweep[0].hit.normal.y==-D::fromInt(1) && !sweep[0].hit.initialOverlap,"one-way query lower sweep face remains geometric");
    for(bool extended:{false,true}){auto wrong=c.collision;wrong.oneWayPlatforms=false;wrong.extended=extended;bool rejected=false;
        try{const DetCollisionQuery2D mismatch(initial,wrong);(void)mismatch;}catch(const std::invalid_argument&){rejected=true;}
        require(rejected,"query rejects profile1/2 policy for profile3 checkpoint");}
    require(!s.faulted() && bytes(s)==initialBytes,"pure query and caught profile mismatch leave Session unchanged");
    advance(s,raw(s,0,-16),"ignored top response with geometric query");const auto moved=checkpoint(s);
    require(moved.actors.at(hero).pose.position[1]==D{}.bits() && moved.actors.at(platform).pose.position[0]==q(1).bits()
        && body<DetEntityRef>(moved,c,DetBodyIgnoredOneWay).value==platform,"ignored platform bypass belongs to solver response only");
    const auto left=ayt::math::DetAabb2::fromCenterHalf(V{-D::fromInt(1),{}},{});
    const DetCollisionQuery2D current(moved,c.collision);const auto movedBytes=encodeDetCheckpoint(moved);
    require(old.overlap(left,filter)==std::vector<SimEntityId>{platform} && current.overlap(left,filter).empty(),"old query owns original values; reacquisition sees moved bounds");
    require(bytes(s)==movedBytes && s.restore(initial),"query observation is pure and source checkpoint restores");
    const DetCollisionQuery2D restored(checkpoint(s),c.collision);
    require(restored.overlap(left,filter)==std::vector<SimEntityId>{platform} && bytes(s)==initialBytes,"restore requires fresh query acquisition");
    std::cout<<"PASS profile3 geometric overlap/ray/sweep, ignored/lower-face response distinction, immutable capture and wrong-profile rejection\n";
}

void roofCrushingChecks(){
    for(auto floorId:{SimEntityId{5},SimEntityId{20}}){
        auto c=smallPolicy();c.collision.maxBodies=3;c.collision.maxContactIterations=4;
        auto make=[&](D roofY,bool ignore=false){
            auto actors=floorActors(c,true,D::fromInt(2),vec(0,1),vec(0,1),floorId);
            actors[0].second.blocks.erase(c.stateSchema);
            if(ignore)writeDetState(detcharacter_advanced::bodySchema(c),actors[0].second.blocks[c.collision.bodySchema],DetBodyIgnoredOneWay,DetEntityRef{floorId});
            actors.push_back({30,detcharacter_advanced::actor(c,DetBodyMode2D::Static,V{{},roofY},{D::fromInt(2),q(2)},{},2,1)});
            return actors;
        };
        auto faultAndRecover=[&](DeterministicSession& s,const DetSessionCheckpoint& before,const std::string& why){
            const auto saved=encodeDetCheckpoint(before);const auto ok=s.advance(raw(s,0,0));
            require(!ok && s.faulted() && s.nextTick()==before.nextTick
                && s.error().find("contact iteration limit / crushing")!=std::string::npos,why+" exhausts the bounded contact solver");
            require(s.restore(before) && !s.faulted() && bytes(s)==saved,why+" explicit restore recovers exact registered bytes");
            // A downward prescribed platform removes the incompatible forces;
            // this verifies healthy execution after restore, not implicit retry.
            auto in=raw(s,0,0);in.commands.push_back({1,1,2,{0,byte(-4)}});advance(s,in,why+" restored non-crushing continuation");
            require(checkpoint(s).actors.at(hero).pose.position==before.actors.at(hero).pose.position,why+" recovered character stays at its contact plane");
        };
        // Feet.5 == platform top.5 and head1.5 == roof bottom1.5.
        // Upward platform motion and the static roof alternate t=0 constraints;
        // no finite contact budget can satisfy both prescribed normal velocities.
        DeterministicSession crush({1,1,1,4,0});require(configureSmall(crush,c,make(D::fromInt(2)),floorId,floorId==5,false),crush.error());
        faultAndRecover(crush,checkpoint(crush),"zero-clearance rising one-way");
        // Clearance.5 exceeds the .25 platform step: hero and platform both
        // rise.25; there is no roof impact, and the hero's solved Vy is1.
        DeterministicSession clear({1,1,1,4,0});require(configureSmall(clear,c,make(q(10)),floorId,floorId==5,false),clear.error());
        advance(clear,raw(clear,0,0),"rising one-way roof clearance");const auto clearCp=checkpoint(clear);
        require(clearCp.actors.at(hero).pose.position[1]==q(5).bits() && clearCp.actors.at(floorId).pose.position[1]==q(1).bits()
            && body<V>(clearCp,c,DetBodyVelocity).y==D::fromInt(1),"clearance hand-calculated .25 rise and imposed Vy1");
        // Clearance exactly.25: tick1 reaches the roof at fraction1 and stops
        // the hero's normal velocity. Tick2 attempts further prescribed rise.
        DeterministicSession endpoint({1,1,1,4,0});require(configureSmall(endpoint,c,make(q(9)),floorId,floorId==5,false),endpoint.error());
        advance(endpoint,raw(endpoint,0,0),"rising one-way terminal roof contact");const auto terminal=checkpoint(endpoint);
        require(terminal.actors.at(hero).pose.position[1]==q(5).bits() && terminal.actors.at(floorId).pose.position[1]==q(1).bits()
            && body<V>(terminal,c,DetBodyVelocity).y.isZero(),"terminal hand-calculated .25 rise with roof Vy0");
        faultAndRecover(endpoint,terminal,"next-tick terminal-contact crush");
        DeterministicSession ignored({1,1,1,4,0});require(configureSmall(ignored,c,make(D::fromInt(2),true),floorId,floorId==5,false),ignored.error());
        advance(ignored,raw(ignored,0,0),"ignored rising one-way under roof");const auto bypass=checkpoint(ignored);
        require(bypass.actors.at(hero).pose.position[1]==D::fromInt(1).bits() && bypass.actors.at(floorId).pose.position[1]==q(1).bits()
            && body<V>(bypass,c,DetBodyVelocity).y.isZero() && body<DetEntityRef>(bypass,c,DetBodyIgnoredOneWay).value==floorId,"ignored prescribed platform may pass through without roof crush");
        require(ignored.restore(bypass),"ignored interior checkpoint restores");
    }
    std::cout<<"PASS moving one-way roof crushing/clearance/terminal/ignored cases in both ID orientations, bounded failure and explicit recovery\n";
}

void dropAndCarrierChecks(){
    for(auto n:{0u,1u,3u,8u}){
        auto c=smallPolicy();c.gravity=D::fromInt(4);c.dropThroughTicks=n;c.coyoteTicks=c.jumpBufferTicks=3;
        auto a=floorActors(c);a.push_back({30,detcharacter_advanced::actor(c,DetBodyMode2D::Static,vec(0,-3),{D::fromInt(8),q(2)},{},2,1,false,false,true)});
        DeterministicSession s({1,1,1,4,0});require(configureSmall(s,c,std::move(a)),s.error());
        const int y4[]={3,1,-2,-6,-8};const int vy[]={-1,-2,-3,-4,0};
        for(unsigned k=0;k<10;++k){advance(s,frame(s,0,true,false,true),"drop held wins over jump");
            const auto cp=checkpoint(s);expect(s,c,{},q(k<5?y4[k]:-8),{},D::fromInt(k<5?vy[k]:0),k>=4,k>=4?30:0,"drop dyadic path");
            const auto clearAt=std::max(5u,n+1);const bool ignored=k+1<clearAt;
            require(body<DetEntityRef>(cp,c,DetBodyIgnoredOneWay).value==(ignored?platform:0),"drop geometric plus tick lifetime");
            const auto remaining=k+1<std::max(1u,n)?std::max(1u,n)-1-(k):0;
            require(state<std::uint32_t>(cp,c,DetCharacterDropRemaining)==remaining && state<std::uint64_t>(cp,c,DetCharacterDropTick)==(ignored?cp.nextTick:0),"drop remaining/stamp");
            require(state<std::uint32_t>(cp,c,DetCharacterJumpBufferRemaining)==0,"drop priority clears jump buffer; held does not re-request");
        }
    }
    auto c=smallPolicy();c.gravity=D::fromInt(4);c.dropThroughTicks=3;
    DeterministicSession retired({1,1,1,4,0});require(configureSmall(retired,c,floorActors(c)),retired.error());
    auto in=frame(retired,0,false,false,true);in.commands.push_back({1,1,3,{}});advance(retired,in,"drop then target retires");
    const auto fresh=checkpoint(retired);require(fresh.retiredIds==std::vector<SimEntityId>{platform} && body<DetEntityRef>(fresh,c,DetBodyIgnoredOneWay).value==platform,"fresh retired ignore identity retained");
    require(retired.restore(fresh),"fresh retired ignore restores");advance(retired,frame(retired,0,false,false,true),"retired drop cleanup");
    const auto clean=checkpoint(retired);require(!body<DetEntityRef>(clean,c,DetBodyIgnoredOneWay).value && !state<std::uint32_t>(clean,c,DetCharacterDropRemaining) && !state<std::uint64_t>(clean,c,DetCharacterDropTick),"retired target clears all authority next pre");
    c=smallPolicy();DeterministicSession carry({1,1,1,4,0});require(configureSmall(carry,c,floorActors(c,true,D::fromInt(8),vec(0,1),{q(2),{}})),carry.error());
    for(unsigned k=1;k<=3;++k){advance(carry,frame(carry),"moving one-way carry");expect(carry,c,q(int(k),8),D::fromInt(1),{},{},true,platform,"one-way horizontal carry once");}
    in=frame(carry);in.commands.push_back({1,1,2,{byte(-2),0}});advance(carry,in,"one-way velocity reverses");expect(carry,c,q(2,8),D::fromInt(1),{},{},true,platform,"one-way current carry no accumulation");
    auto tied=[&](){auto result=floorActors(c,true,q(2));result[1].second.pose.position[0]=q(-2).bits();
        result.push_back({5,detcharacter_advanced::actor(c,DetBodyMode2D::Static,{q(2),{}},{q(2),q(2)},{},2,1,false,false,true)});return result;};
    DeterministicSession a({1,1,1,4,0}),b({1,1,1,4,0});require(configureSmall(a,c,tied())&&configureSmall(b,c,tied(),platform,true),"one-way support ties setup");
    advance(a,frame(a),"tie forward");advance(b,frame(b),"tie reverse");expect(a,c,{},D::fromInt(1),{},{},true,5,"one-way stable lower ID tie");require(bytes(a)==bytes(b),"one-way ties entire checkpoint stable");
    auto masked=floorActors(c);writeDetState(detcharacter_advanced::bodySchema(c),masked[1].second.blocks[c.collision.bodySchema],DetBodyMask,std::uint32_t{0});
    DeterministicSession noSupport({1,1,1,4,0});require(configureSmall(noSupport,c,std::move(masked)),noSupport.error());advance(noSupport,frame(noSupport,0,true),"nonreciprocal one-way mask");
    expect(noSupport,c,{},D::fromInt(1),{},{},false,0,"nonreciprocal one-way never supports jump");
    std::cout<<"PASS drop0/1/N priority/lifetime/held deduplication, retired cleanup, current moving carry, stable ties and reciprocal masks\n";
}

void crouchChecks(){
    auto c=smallPolicy();c.crouchEnabled=true;
    auto a=floorActors(c,false);writeDetState(detcharacter_advanced::bodySchema(c),a[0].second.blocks[c.collision.bodySchema],DetBodyOffset,V{{},q(1)});
    // Center .75+offset .25 puts the same standing bottom at.5. Crouching
    // halfY .25/offsetY0 leaves feet .5 and poseY .75 without a pose write.
    a[0].second.pose.position[1]=q(3).bits();
    DeterministicSession s({1,1,1,4,0});require(configureSmall(s,c,std::move(a)),s.error());advance(s,frame(s,0,false,true),"offset crouch");
    const auto crouched=checkpoint(s);require(crouched.actors.at(hero).pose.position[1]==q(3).bits() && body<V>(crouched,c,DetBodyHalf)==V{q(2),q(1)} && body<V>(crouched,c,DetBodyOffset)==V{},"offset crouch exact feet and unchanged pose");
    require(state<V>(crouched,c,DetCharacterStandingHalf)==V{q(2),q(2)} && state<V>(crouched,c,DetCharacterStandingOffset)==V{{},q(1)},"standing shape captured as registered authority");
    advance(s,frame(s),"offset standing");require(body<V>(checkpoint(s),c,DetBodyHalf)==V{q(2),q(2)} && body<V>(checkpoint(s),c,DetBodyOffset)==V{{},q(1)},"standing restores original shape");
    require(s.restore(crouched),"crouched state restore");advance(s,frame(s),"restored standing");require(!state<bool>(checkpoint(s),c,DetCharacterCrouched),"restored crouch can stand");
    // Deliberate authored crouched checkpoint under a one-way roof. Ignoring
    // the roof for top-plane collision must not ignore it for standing clearance.
    auto roofActors=floorActors(c);DetCharacterState2D authored;authored.enabled=true;authored.crouched=authored.standingCaptured=true;authored.standingHalf={q(2),q(2)};
    roofActors[0].second.blocks[c.stateSchema]=detCharacterState2D(c,authored);
    auto& bw=roofActors[0].second.blocks[c.collision.bodySchema];writeDetState(detcharacter_advanced::bodySchema(c),bw,DetBodyHalf,V{q(2),q(1)});writeDetState(detcharacter_advanced::bodySchema(c),bw,DetBodyOffset,V{{},-q(1)});
    // Initial ignored stamp is forbidden: establish a valid captured tick first,
    // then construct a semantic checkpoint using the known existing one-way roof.
    roofActors.push_back({30,detcharacter_advanced::actor(c,DetBodyMode2D::Static,{D::fromInt(0),q(6)}, {D::fromInt(2),q(1)},{},2,1,false,false,true)});
    DeterministicSession roof({1,1,1,4,0});require(configureSmall(roof,c,std::move(roofActors),30),roof.error());advance(roof,frame(roof),"blocked one-way ceiling");
    auto ignored=checkpoint(roof);setBody(ignored,c,DetBodyIgnoredOneWay,DetEntityRef{30});setState(ignored,c,DetCharacterDropTick,ignored.nextTick);
    require(roof.restore(ignored),"authored captured ignore checkpoint");advance(roof,frame(roof),"ignored roof still blocks stand");require(state<bool>(checkpoint(roof),c,DetCharacterCrouched),"standing clearance includes ignored one-way solid");
    auto in=frame(roof);in.commands.push_back({1,1,3,{}});advance(roof,in,"retire low roof");require(state<bool>(checkpoint(roof),c,DetCharacterCrouched),"queued roof still blocks this tick");
    advance(roof,frame(roof),"roof gone can stand");require(!state<bool>(checkpoint(roof),c,DetCharacterCrouched) && body<V>(checkpoint(roof),c,DetBodyHalf)==V{q(2),q(2)},"next pre restores standing exact shape");
    for(bool startCrouched:{false,true}){
        // Binary32 .3 => standing foot bits3f333333; scaled .15 with
        // offset-.15 => foot bits3f333334. Both geometries are individually
        // valid, but changing stance would move the represented foot plane.
        const auto h=D::fromBits(0x3e99999au),ch=D::fromBits(0x3e19999au);
        auto a=detcharacter_advanced::actor(c,DetBodyMode2D::Kinematic,vec(0,1),{q(2),startCrouched?ch:h},{},1,2,false,true);
        if(startCrouched){DetCharacterState2D initial;initial.enabled=true;initial.crouched=initial.standingCaptured=true;initial.standingHalf={q(2),h};
            a.blocks[c.stateSchema]=detCharacterState2D(c,initial);
            writeDetState(detcharacter_advanced::bodySchema(c),a.blocks[c.collision.bodySchema],DetBodyOffset,V{{},-ch});}
        DeterministicSession blocked({1,1,1,4,0});require(configureSmall(blocked,c,{{hero,std::move(a)}}),blocked.error());
        const auto before=checkpoint(blocked);const auto oldHalf=body<V>(before,c,DetBodyHalf),oldOffset=body<V>(before,c,DetBodyOffset);
        const auto foot=D::fromInt(1)+oldOffset.y-oldHalf.y;
        require(foot.bits()==(startCrouched?0x3f333334u:0x3f333333u),"non-dyadic fixed represented foot witness");
        advance(blocked,frame(blocked,0,false,!startCrouched),"represented foot blocked stance");
        const auto after=checkpoint(blocked);const auto& oldPose=before.actors.at(hero).pose;const auto& newPose=after.actors.at(hero).pose;
        require(!blocked.faulted() && newPose.position==oldPose.position && newPose.rotation==oldPose.rotation
            && body<V>(after,c,DetBodyHalf)==oldHalf && body<V>(after,c,DetBodyOffset)==oldOffset
            && state<bool>(after,c,DetCharacterCrouched)==startCrouched,"representation-blocked stance retains complete old physical geometry/pose");
        // A successful stationary tick still advances interpolation history and
        // collision's one position write. Blocking posture does not freeze those.
        auto expectedPose=oldPose;expectedPose.previousPosition=oldPose.position;
        expectedPose.hasPreviousPosition=true;expectedPose.revision=oldPose.revision+1;
        require(newPose==expectedPose,"blocked posture preserves current fields with normal tick history/revision");
    }
    std::cout<<"PASS foot-preserving offset crouch, registered capture/restore and standing blocked by ignored one-way roof until removal\n";
}

void invalidChecks(){
    using Change=std::function<void(DetCharacterController2DConfig&)>;
    const std::vector<std::pair<std::string,Change>> bad={
        {"coyote overflow",[](auto& c){c.coyoteTicks=65536;}},{"buffer overflow",[](auto& c){c.jumpBufferTicks=65536;}},{"drop overflow",[](auto& c){c.dropThroughTicks=65536;}},
        {"advanced missing collision",[](auto& c){c.collision.oneWayPlatforms=false;}},{"collision missing advanced",[](auto& c){c.advanced=false;}},
        {"no extended",[](auto& c){c.collision.extended=false;}},{"scale zero",[](auto& c){c.crouchHeightScale={};}},{"scale one",[](auto& c){c.crouchHeightScale=D::fromInt(1);}},
        {"scale NaN",[](auto& c){c.crouchHeightScale=D::fromBits(0x7fc00000u);}},{"scale infinity",[](auto& c){c.crouchHeightScale=D::fromBits(0x7f800000u);}}};
    for(const auto& [why,mutate]:bad){DeterministicSession s({1,1,1,4,0});auto c=smallPolicy();mutate(c);require(!installDetCharacterController2D(s,c),why+" refuses preflight");require(installDetCharacterController2D(s,smallPolicy()),why+" registration still clean");}
    for(unsigned option=0;option<5;++option){DeterministicSession s({1,1,1,4,0});DetCharacterController2DConfig c;
        if(option==0)c.coyoteTicks=1;if(option==1)c.jumpBufferTicks=1;if(option==2)c.dropThroughTicks=1;if(option==3)c.crouchEnabled=true;if(option==4)c.crouchHeightScale=q(1);
        require(!installDetCharacterController2D(s,c),"legacy rejects advanced policy option");}
    auto c=smallPolicy();c.coyoteTicks=c.jumpBufferTicks=c.dropThroughTicks=65535;
    DeterministicSession max({1,1,1,4,0});require(configureSmall(max,c,floorActors(c)),max.error());advance(max,frame(max),"maximum tick policy");require(state<std::uint32_t>(checkpoint(max),c,DetCharacterCoyoteRemaining)==65535,"inclusive u16 timer boundary");
    c=smallPolicy();c.coyoteTicks=c.jumpBufferTicks=c.dropThroughTicks=3;
    DeterministicSession s({1,1,1,4,0});require(configureSmall(s,c,floorActors(c)),s.error());advance(s,frame(s),"restore source capture");
    const auto original=checkpoint(s);const auto unchanged=bytes(s);
    auto reject=[&](const std::string& why,auto mutate){auto cp=original;mutate(cp);require(!s.restore(cp),why+" rejected");require(bytes(s)==unchanged,why+" full-state atomic");};
    reject("uncaptured at nonzero tick",[&](auto& cp){setState(cp,c,DetCharacterStandingCaptured,false);setState(cp,c,DetCharacterStandingHalf,V{});});
    reject("standing shape disagrees",[&](auto& cp){setState(cp,c,DetCharacterStandingHalf,V{q(2),q(1)});});
    reject("coyote exceeds policy",[&](auto& cp){setState(cp,c,DetCharacterCoyoteRemaining,std::uint32_t{4});});
    reject("buffer exceeds policy",[&](auto& cp){setState(cp,c,DetCharacterJumpBufferRemaining,std::uint32_t{4});});
    reject("drop exceeds future budget",[&](auto& cp){setState(cp,c,DetCharacterDropRemaining,std::uint32_t{3});});
    reject("jump held/latch mismatch",[&](auto& cp){setState(cp,c,DetCharacterJumpHeld,true);});
    reject("drop held/latch mismatch",[&](auto& cp){setState(cp,c,DetCharacterDropHeld,true);});
    reject("committed dropstarted",[&](auto& cp){setState(cp,c,DetCharacterDropStarted,true);});
    reject("ignore unknown",[&](auto& cp){setBody(cp,c,DetBodyIgnoredOneWay,DetEntityRef{999});setState(cp,c,DetCharacterDropTick,cp.nextTick);});
    reject("ignore self",[&](auto& cp){setBody(cp,c,DetBodyIgnoredOneWay,DetEntityRef{hero});setState(cp,c,DetCharacterDropTick,cp.nextTick);});
    reject("ignore active support",[&](auto& cp){setBody(cp,c,DetBodyIgnoredOneWay,DetEntityRef{platform});setState(cp,c,DetCharacterDropTick,cp.nextTick);});
    reject("stamp without ignore",[&](auto& cp){setState(cp,c,DetCharacterDropTick,cp.nextTick);});
    reject("counter without ignore",[&](auto& cp){setState(cp,c,DetCharacterDropRemaining,std::uint32_t{1});});
    reject("ground coyote cache differs",[&](auto& cp){setState(cp,c,DetCharacterCoyoteSupportY,q(1));});
    reject("Boolean noncanonical",[&](auto& cp){cp.actors.at(hero).blocks.at(c.stateSchema)[lane(detcharacter_advanced::stateSchema(c),DetCharacterJumpLatch)]=2;});
    reject("shape NaN",[&](auto& cp){cp.actors.at(hero).blocks.at(c.stateSchema)[lane(detcharacter_advanced::stateSchema(c),DetCharacterStandingHalf)]=0x7fc00000u;});
    reject("ordinary active penetration",[&](auto& cp){setBody(cp,c,DetBodyOneWay,false,platform);cp.actors.at(hero).pose.position[1]=D{}.bits();});
    require(s.restore(original),"valid advanced restore");
    auto in=frame(s,12,true,true,true);require(!s.advance(in)&&s.faulted(),"invalid target throws/faults before applying helper input");require(s.restore(original),"invalid input explicit recovery");require(bytes(s)==unchanged,"input fault restored exact state");
    DeterministicSession legacy({1,1,1,4,0});DetCharacterController2DConfig old;require(configureSmall(legacy,old,floorActors(old,false)),legacy.error());
    const auto oldcp=checkpoint(legacy);const auto oldbytes=bytes(legacy);require(legacy.manifest()!=s.manifest() && !legacy.restore(original)&&bytes(legacy)==oldbytes && !s.restore(oldcp)&&bytes(s)==unchanged,"new/old schema+logic profiles reject both ways atomically");
    require(!legacy.advance(frame(legacy,0,false,true))&&legacy.faulted() && legacy.restore(oldcp),"legacy rejects crouch input");
    require(!legacy.advance(frame(legacy,0,false,false,true))&&legacy.faulted() && legacy.restore(oldcp),"legacy rejects drop input");
    auto incompatible=c;incompatible.jumpBufferTicks=2;DeterministicSession other({1,1,1,4,0});require(configureSmall(other,incompatible,floorActors(incompatible)),other.error());
    const auto otherbytes=bytes(other);require(!other.restore(original)&&bytes(other)==otherbytes,"advanced immutable timer policy manifest mismatch");
    // Standing +/- the smallest subnormal has represented area at zero. Its
    // hypothetical half-height rounds to zero: request is blocked, state healthy.
    auto tiny=smallPolicy();tiny.crouchEnabled=true;auto a=floorActors(tiny);writeDetState(detcharacter_advanced::bodySchema(tiny),a[0].second.blocks[tiny.collision.bodySchema],DetBodyHalf,V{q(2),D::fromBits(1)});
    a[0].second.pose.position[1]=D{}.bits();a.pop_back();DeterministicSession underflow({1,1,1,4,0});require(configureSmall(underflow,tiny,std::move(a)),underflow.error());
    advance(underflow,frame(underflow,0,false,true),"crouch hypothetical underflow");require(!underflow.faulted() && !state<bool>(checkpoint(underflow),tiny,DetCharacterCrouched)
        && body<V>(checkpoint(underflow),tiny,DetBodyHalf).y.bits()==1,"unrepresentable crouch candidate blocked without geometry/state fault");
    std::cout<<"PASS advanced policy endpoints, illegal state/input/shape, atomic restore, and legacy/new identity rejection\n";
}

void setEnvironment(int rounding,bool ftz){require(std::fesetround(rounding)==0,"host rounding mode");
#if defined(_M_X64) || defined(__x86_64__)
    const auto csr=_mm_getcsr();_mm_setcsr(ftz?(csr|0x8040u):(csr&~0x8040u));
#else
    (void)ftz;
#endif
}
struct ScenarioEvidence{Bytes manifest;std::vector<Bytes> states;std::vector<std::uint64_t> hashes;std::vector<DetConfirmedEvent> events;};
ScenarioEvidence scenarioChecks(){
    DeterministicSession a({1,1,1,64,0}),b({1,1,1,64,0});
    require(detcharacter_advanced::configure(a)&&detcharacter_advanced::configure(b,true),"shared fixture setup");
    ScenarioEvidence evidence;auto remember=[&](const DetSessionCheckpoint& cp){
        if(evidence.states.empty())evidence.manifest=cp.manifest;
        else require(cp.manifest==evidence.manifest,"scenario manifest remains exactly initial registered bytes");
        evidence.states.push_back(encodeDetCheckpoint(cp));evidence.hashes.push_back(detCheckpointHash(cp));};
    require(a.manifest()==b.manifest()&&detcharacter_advanced::matchesGolden(checkpoint(a)),"initial full fixture");remember(checkpoint(a));
    DetSessionCheckpoint midpoint,late;
    for(std::uint64_t tick=0;tick<detcharacter_advanced::maxTicks;++tick){
        setEnvironment(FE_UPWARD,true);advance(a,detcharacter_advanced::input(tick),"forward shared fixture");
        setEnvironment(FE_DOWNWARD,false);advance(b,detcharacter_advanced::input(tick,true),"reversed shared fixture");
        const auto cp=checkpoint(a);require(detcharacter_advanced::matchesGolden(cp),"independent advanced flight/shape/event oracle tick"+std::to_string(cp.nextTick));
        require(encodeDetCheckpoint(cp)==bytes(b),"complete checkpoint stable insertion/input order");remember(cp);
        for(const auto& event:cp.pendingEvents)evidence.events.push_back({1,tick,event});
        if(cp.nextTick==128)midpoint=cp;if(cp.nextTick==192)late=cp;
    }
    require(evidence.events.size()==30,"independent trigger interval produces30 actual events");
    const auto stats=checkpoint(a).globals.at(detcharacter_advanced::statsSchema);
    require(stats[6]==1 && stats[7]==1 && stats[8]==1 && stats[9]==1 && stats[10]==1 && stats[11]==129 && stats[12]==128 && stats[13]==1,"advanced actions actually occurred once; crouch/blocked stand real durations");
    require(b.restore(late)&&b.restore(midpoint),b.error());
    for(auto tick=midpoint.nextTick;tick<detcharacter_advanced::maxTicks;++tick){advance(b,detcharacter_advanced::input(tick,true),"restored fixture");
        require(bytes(b)==evidence.states[tick+1],"restore128 continues all fields exactly");}
    std::cout<<"PASS 256-tick independent lattice/events, every complete checkpoint in two insertion orders, restore192->128\n";
    return evidence;
}
DetTickInput memberInput(DetTickInput input,std::uint32_t member){std::erase_if(input.commands,[member](const auto& c){return c.source!=member;});return input;}
std::pair<std::uint64_t,std::uint64_t> rollbackChecks(const ScenarioEvidence& evidence){
    DeterministicSession s({1,1,1,64,0});require(detcharacter_advanced::configure(s,true),"late rollback fixture");
    DetRollbackConfig c;c.members={1,2};c.historyTicks=32;c.maxPredictionTicks=8;
    c.prediction={{1,DetPredictionMode::Hold},{2,DetPredictionMode::Hold}};DeterministicRollback owner(s,c);
    std::vector<DetConfirmedEvent> effects;
    for(std::uint64_t tick=0;tick<detcharacter_advanced::maxTicks;++tick){auto in=detcharacter_advanced::input(tick,true);
        require(owner.submit(2,memberInput(in,2)),owner.error());
        if(!tick)require(owner.submit(1,memberInput(in,1)),owner.error());
        if(tick>=3)require(owner.submit(1,memberInput(detcharacter_advanced::input(tick-3),1)),owner.error());
        require(owner.advance(),owner.error());const auto frontier=owner.confirmedNextTick();const auto cp=owner.checkpointAt(frontier);
        require(cp && encodeDetCheckpoint(*cp)==evidence.states[frontier],"late input confirmed full checkpoint vs real baseline");
        auto e=owner.takeConfirmedEvents();effects.insert(effects.end(),e.begin(),e.end());
        if(tick==191){const auto before=bytes(s);require(owner.replayFrom(s.nextTick()-16),owner.error());
            require(bytes(s)==before&&owner.takeConfirmedEvents().empty(),"explicit retained replay no duplicated confirmed effect");}
    }
    for(auto tick=detcharacter_advanced::maxTicks-3;tick<detcharacter_advanced::maxTicks;++tick)
        require(owner.submit(1,memberInput(detcharacter_advanced::input(tick),1)),owner.error());
    auto e=owner.takeConfirmedEvents();effects.insert(effects.end(),e.begin(),e.end());
    require(owner.confirmedNextTick()==detcharacter_advanced::maxTicks && bytes(s)==evidence.states.back(),"late input final state");
    require(effects==evidence.events && owner.takeConfirmedEvents().empty(),"real confirmed events identities/payloads equal once");
    require(owner.rollbackCount()>1&&owner.replayedTicks()>0&&owner.oldestTick()>220,"actual corrections and bounded history");
    std::cout<<"PASS actual 3-tick late movement inputs, corrected full checkpoints and exactly-once confirmed effects\n";
    return {owner.rollbackCount(),owner.replayedTicks()};
}

void writeText(const fs::path& path,const std::string& text){std::ofstream out(path,std::ios::binary);out<<text;require(bool(out),"artifact write "+path.string());}
std::uint64_t writeCheckpointStream(const fs::path& directory,const ScenarioEvidence& evidence){
    require(evidence.states.size()==detcharacter_advanced::maxTicks+1 && evidence.states.size()<=UINT32_MAX,"complete canonical checkpoint stream row count");
    const auto path=directory/"canonical-checkpoints.bin";std::ofstream out(path,std::ios::binary);
    // Test artifact only, independent of engine checkpoint/replay envelopes:
    // magic8 AYCCPS01, LEu32 artifact version1, LEu32 row count, then repeated
    // LEu32 byte length + original canonical checkpoint bytes, in tick0..256 order.
    constexpr std::array<char,8> magic{'A','Y','C','C','P','S','0','1'};
    out.write(magic.data(),magic.size());
    auto put32=[&](std::uint32_t value){std::array<char,4> encoded{};
        for(unsigned i=0;i<4;++i)encoded[i]=static_cast<char>((value>>(8*i))&255u);
        out.write(encoded.data(),encoded.size());};
    put32(1);put32(static_cast<std::uint32_t>(evidence.states.size()));std::uint64_t size=16;
    for(const auto& row:evidence.states){require(!row.empty() && row.size()<=UINT32_MAX,"canonical checkpoint stream record bound");
        put32(static_cast<std::uint32_t>(row.size()));out.write(reinterpret_cast<const char*>(row.data()),static_cast<std::streamsize>(row.size()));
        size+=4+row.size();}
    out.flush();require(bool(out),"canonical checkpoint stream flush");out.close();require(bool(out),"canonical checkpoint stream close");
    require(fs::is_regular_file(path) && fs::file_size(path)==size,"canonical checkpoint stream complete physical size");return size;
}
std::uint64_t writeEventStream(const fs::path& directory,const ScenarioEvidence& evidence){
    require(evidence.events.size()==30 && evidence.events.size()<=UINT32_MAX,"complete actual confirmed event stream count");
    const auto path=directory/"canonical-events.bin";std::ofstream out(path,std::ios::binary);
    // Artifact version1: magic8 AYCCEV01, LEu32 version/count. Each actual event
    // is epoch/u32, tick/u64, source/u32, sequence/u32, type/u32, payloadLength/u32
    // then original payload bytes. Never substitute the hand-coded event oracle.
    constexpr std::array<char,8> magic{'A','Y','C','C','E','V','0','1'};out.write(magic.data(),magic.size());
    auto put=[&](std::uint64_t value,unsigned width){std::array<char,8> encoded{};
        for(unsigned i=0;i<width;++i)encoded[i]=static_cast<char>((value>>(8*i))&255u);
        out.write(encoded.data(),width);};
    put(1,4);put(evidence.events.size(),4);std::uint64_t size=16;
    for(const auto& event:evidence.events){const auto& command=event.command;
        require(command.payload.size()<=UINT32_MAX,"canonical event stream payload bound");
        put(event.epoch,4);put(event.tick,8);put(command.source,4);put(command.sequence,4);put(command.type,4);put(command.payload.size(),4);
        out.write(reinterpret_cast<const char*>(command.payload.data()),static_cast<std::streamsize>(command.payload.size()));size+=28+command.payload.size();}
    out.flush();require(bool(out),"canonical event stream flush");out.close();require(bool(out),"canonical event stream close");
    require(fs::is_regular_file(path) && fs::file_size(path)==size,"canonical event stream complete physical size");return size;
}
void writeScenarioManifest(const fs::path& directory,const ScenarioEvidence& evidence){
    require(!evidence.manifest.empty(),"scenario initial manifest present");const auto path=directory/"scenario.manifest";
    std::ofstream out(path,std::ios::binary);out.write(reinterpret_cast<const char*>(evidence.manifest.data()),static_cast<std::streamsize>(evidence.manifest.size()));
    out.flush();require(bool(out),"scenario manifest flush");out.close();require(bool(out),"scenario manifest close");
    require(fs::is_regular_file(path) && fs::file_size(path)==evidence.manifest.size(),"scenario manifest original byte count");
}
std::string jsonString(const std::string& value){std::string out="\"";for(unsigned char c:value){
    if(c=='\\'||c=='\"'){out+='\\';out+=char(c);}else if(c<32){const char* h="0123456789abcdef";out+="\\u00";out+=h[c>>4];out+=h[c&15];}else out+=char(c);}return out+'\"';}
fs::path fresh(const fs::path& parent){require(fs::exists(parent)&&fs::is_directory(parent),"--output must name an existing parent directory");
    const auto stamp=std::chrono::steady_clock::now().time_since_epoch().count();
    for(unsigned attempt=0;attempt<128;++attempt){auto path=parent/("characteradvanced-"+std::to_string(stamp)+"-"+std::to_string(attempt));
        if(fs::create_directory(path))return path;}throw std::runtime_error("unable to reserve fresh artifact directory");}
std::uint32_t replayChecks(const fs::path& directory,const ScenarioEvidence& evidence){
    DeterministicSession recording({1,1,1,64,0});require(detcharacter_advanced::configure(recording),"rpl fixture");
    DetReplayWriter writer;require(writer.begin(recording,(directory/"advanced-single").string(),32),writer.error());
    for(std::uint64_t tick=0;tick<detcharacter_advanced::maxTicks;++tick){require(writer.advance(detcharacter_advanced::input(tick)),writer.error());
        require(bytes(recording)==evidence.states[tick+1],"actual single file recording full state");}
    require(writer.finish(),writer.error());require(fs::is_regular_file(writer.path()),"actual .rpl file exists");
    DeterministicSession playback({1,1,1,64,0});require(detcharacter_advanced::configure(playback,true),"rpl playback fixture");
    DetReplayReader reader;require(reader.open(writer.path())&&reader.restoreInitial(playback),reader.error());
    require(reader.tickCount()==detcharacter_advanced::maxTicks,"sealed rpl tick count");
    while(!reader.atEnd()){require(reader.advance(playback),reader.error());require(bytes(playback)==evidence.states[playback.nextTick()],"actual rpl tick full checkpoint");}
    require(reader.seek(playback,128)&&bytes(playback)==evidence.states[128],"actual rpl seek128");
    while(!reader.atEnd()){require(reader.advance(playback),reader.error());require(bytes(playback)==evidence.states[playback.nextTick()],"actual rpl seek continuation");}

    DeterministicSession archiveSession({1,1,1,64,0});require(detcharacter_advanced::configure(archiveSession),"archive fixture");
    DetRollbackNetworkConfig nc;nc.sessionId=220;nc.localMember=nc.recoveryMember=1;nc.rollback.members={1};
    nc.rollback.historyTicks=32;nc.rollback.maxPredictionTicks=4;DeterministicRollbackNetwork network(archiveSession,nc);
    DetRollbackReplayWriter archive;DetRollbackReplayArchiveOptions options;options.maxSegmentRecords=33;
    require(archive.begin(network,(directory/"advanced-archive.rpl").string(),options,32),archive.error());
    std::vector<DetConfirmedEvent> recorded;
    for(std::uint64_t tick=0;tick<detcharacter_advanced::maxTicks;++tick){
        require(network.submitLocal(memberInput(detcharacter_advanced::input(tick),1))&&network.advance(),network.error());
        require(archive.sync(network),archive.error());require(bytes(archiveSession)==evidence.states[tick+1],"archive real confirmed checkpoint");
        auto e=network.takeConfirmedEvents();recorded.insert(recorded.end(),e.begin(),e.end());}
    require(archive.finish(network),archive.error());require(recorded==evidence.events,"archive emitted real event baseline");
    const auto inspected=DetRollbackReplayArchive::inspect(archive.path());
    require(inspected.valid&&!inspected.recovery&&inspected.files.size()>1&&fs::is_regular_file(archive.path()),"actual sealed indexed multi-file recording");
    DeterministicSession archivePlayback({1,1,1,64,0});require(detcharacter_advanced::configure(archivePlayback,true),"archive replay fixture");
    DetRollbackReplayReader ar;require(ar.open(archive.path())&&ar.restoreInitial(archivePlayback),ar.error());
    std::vector<DetConfirmedEvent> replayed;
    while(!ar.atEnd()){require(ar.advance(archivePlayback),ar.error());
        require(bytes(archivePlayback)==evidence.states[archivePlayback.nextTick()],"indexed replay every full checkpoint");
        auto e=ar.takeConfirmedEvents();replayed.insert(replayed.end(),e.begin(),e.end());}
    require(replayed==evidence.events,"indexed replay exact confirmed event identities/payloads");
    for(auto tick:{128u,32u,192u}){require(ar.seek(archivePlayback,1,tick),ar.error());
        require(bytes(archivePlayback)==evidence.states[tick]&&ar.takeConfirmedEvents().empty(),"cross-segment seek checkpoint and silent effects");
        while(!ar.atEnd()){require(ar.advance(archivePlayback),ar.error());require(bytes(archivePlayback)==evidence.states[archivePlayback.nextTick()],"cross-segment seek continues full state");(void)ar.takeConfirmedEvents();}}
    std::ostringstream trace;for(std::size_t tick=0;tick<evidence.hashes.size();++tick)trace<<tick<<' '<<evidence.hashes[tick]<<'\n';
    writeText(directory/"tick-hashes.txt",trace.str());
    std::cout<<"PASS actual .rpl and segmented .rpi recording/replay, per-tick full checkpoints, events and cross-segment seek\n";
    return static_cast<std::uint32_t>(inspected.files.size());
}
} // namespace

int main(int argc,char** argv){fs::path directory;try{
    fs::path parent=fs::temp_directory_path();if(argc==3 && std::string(argv[1])=="--output")parent=argv[2];
    else if(argc!=1){std::cerr<<"usage: [--output EXISTING_PARENT_DIRECTORY]\n";return 2;}
    directory=fresh(parent);writeText(directory/"progress.json","{\"status\":\"running\",\"phase\":\"directed\"}\n");
    setEnvironment(FE_UPWARD,true);
    windowChecks();topPlaneChecks();queryProfileChecks();roofCrushingChecks();dropAndCarrierChecks();crouchChecks();invalidChecks();
    writeText(directory/"progress.json","{\"status\":\"running\",\"phase\":\"checkpoint-rollback-replay\"}\n");
    const auto evidence=scenarioChecks();const auto [rollbacks,replayed]=rollbackChecks(evidence);const auto files=replayChecks(directory,evidence);
    const auto streamBytes=writeCheckpointStream(directory,evidence);
    const auto eventBytes=writeEventStream(directory,evidence);writeScenarioManifest(directory,evidence);
    std::ostringstream summary;summary<<"{\"status\":\"passed\",\"ticks\":256,\"checkpointRows\":"<<evidence.states.size()
        <<",\"confirmedEvents\":"<<evidence.events.size()<<",\"rollbacks\":"<<rollbacks<<",\"replayedTicks\":"<<replayed
        <<",\"archiveFiles\":"<<files<<",\"finalHash\":"<<evidence.hashes.back()
        <<",\"checkpointStream\":{\"file\":\"canonical-checkpoints.bin\",\"artifactVersion\":1,\"magic\":\"AYCCPS01\",\"rows\":"<<evidence.states.size()<<",\"bytes\":"<<streamBytes<<'}'
        <<",\"eventStream\":{\"file\":\"canonical-events.bin\",\"artifactVersion\":1,\"magic\":\"AYCCEV01\",\"rows\":"<<evidence.events.size()<<",\"bytes\":"<<eventBytes<<'}'
        <<",\"scenarioManifest\":{\"file\":\"scenario.manifest\",\"bytes\":"<<evidence.manifest.size()<<'}'
        <<",\"features\":{\"heldJumpActions\":1,\"bufferedJumpActions\":1,\"coyoteJumpActions\":1,\"dropActions\":1,\"ignoredPlatformReleases\":1,\"crouchedTicks\":129,\"blockedStandTicks\":128,\"standTransitions\":1}"
        <<",\"artifacts\":"<<jsonString(directory.string())<<"}\n";
    writeText(directory/"summary.json",summary.str());writeText(directory/"progress.json","{\"status\":\"passed\",\"phase\":\"complete\"}\n");
    std::cout<<summary.str();return 0;
}catch(const std::exception& e){if(!directory.empty()){try{
    writeText(directory/"summary.json","{\"status\":\"failed\",\"error\":"+jsonString(e.what())+"}\n");
    writeText(directory/"progress.json","{\"status\":\"failed\"}\n");}catch(...){}}
    std::cerr<<e.what()<<'\n';return 1;}}
