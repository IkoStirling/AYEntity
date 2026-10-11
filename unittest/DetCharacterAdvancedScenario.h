#pragma once
// design reference: AYEntity/design.md Stage22; opt-in character windows, one-way and crouch.
// Five separated characters exercise actual advanced actions. The oracle uses
// fixed, hand-derived dyadic flights and event intervals, not controller branches.
#include <AYEntity/DeterministicCharacterController2D.h>
#include <algorithm>
#include <array>
#include <stdexcept>

namespace detcharacter_advanced {
using namespace ayt::entity;
using D=ayt::math::DetFloat32;
using V=ayt::math::DetVec2;
inline constexpr std::uint64_t maxTicks=256;
inline constexpr std::uint32_t statsSchema=8,inputType=1,markerType=2;
inline D ratio(int n,int d){return D::fromInt(n)/D::fromInt(d);}
inline V v(int x,int y){return V::fromInts(x,y);}
inline DetCharacterController2DConfig policy(){
    DetCharacterController2DConfig c;c.advanced=true;c.collision.oneWayPlatforms=true;
    c.coyoteTicks=c.jumpBufferTicks=c.dropThroughTicks=2;c.crouchEnabled=true;
    c.maxHorizontalSpeed=D::fromInt(2);c.acceleration=c.deceleration=D::fromInt(128);
    c.gravity=D::fromInt(4);c.jumpSpeed=D::fromInt(2);
    c.collision.maxBodies=16;c.collision.maxTriggerPairs=8;return c;
}
inline DetTypedStateSchema stateSchema(const DetCharacterController2DConfig& c){
    return {c.stateSchema,c.advanced?2u:1u,{{DetCharacterEnabled,false},{DetCharacterGrounded,false},
        {DetCharacterSupport,DetEntityRef{}},{DetCharacterVelocity,V{}},
        {DetCharacterSupportVelocity,V{}},{DetCharacterTargetSpeed,D{}},
        {DetCharacterJumpRequested,false},{DetCharacterAppliedCarry,V{}},
        {DetCharacterIntentVelocity,V{}},{DetCharacterJumped,false},{DetCharacterSnapped,false},
        {DetCharacterSupportTick,std::uint64_t{0}},{DetCharacterCoyoteRemaining,std::uint32_t{0}},
        {DetCharacterJumpBufferRemaining,std::uint32_t{0}},{DetCharacterJumpHeld,false},
        {DetCharacterDropHeld,false},{DetCharacterCrouchHeld,false},{DetCharacterDropStarted,false},
        {DetCharacterCrouched,false},{DetCharacterStandingCaptured,false},
        {DetCharacterStandingHalf,V{}},{DetCharacterStandingOffset,V{}},
        {DetCharacterDropRemaining,std::uint32_t{0}},{DetCharacterCoyoteSupportY,D{}},
        {DetCharacterJumpLatch,false},{DetCharacterDropLatch,false},{DetCharacterDropTick,std::uint64_t{0}}}};
}
inline DetTypedStateSchema bodySchema(const DetCharacterController2DConfig& c){
    return {c.collision.bodySchema,3,{{DetBodyMode,std::uint32_t{0}},{DetBodyHalf,V{}},
        {DetBodyOffset,V{}},{DetBodyVelocity,V{}},{DetBodyLayer,std::uint32_t{1}},
        {DetBodyMask,std::uint32_t{UINT32_MAX}},{DetBodyTrigger,false},
        {DetBodyOneWay,false},{DetBodyIgnoredOneWay,DetEntityRef{}}}};
}
inline DetActorState actor(const DetCharacterController2DConfig& c,DetBodyMode2D mode,
        V position,V half,V velocity={},std::uint32_t layer=1,std::uint32_t mask=UINT32_MAX,
        bool trigger=false,bool character=false,bool oneWay=false){
    DetActorState a;DetSimTransformComponent pose;
    if(!pose.setPosition({position.x,position.y,{}}))throw std::runtime_error("advanced fixture pose");
    a.pose=pose.snapshot();
    a.blocks[c.collision.bodySchema]=detCollisionBodyState2D(c.collision,{mode,half,{},velocity,layer,mask,trigger,oneWay});
    if(character){DetCharacterState2D s;s.enabled=true;a.blocks[c.stateSchema]=detCharacterState2D(c,s);}return a;
}
inline DetTypedStateSchema statsDescriptor(){
    DetTypedStateSchema schema{statsSchema,1,{}};
    for(std::uint32_t id=10;id<=140;id+=10)schema.fields.push_back({id,std::uint64_t{0}});
    schema.fields.push_back({150,DetEntityRef{}});schema.fields.push_back({160,false});return schema;
}
inline std::vector<std::pair<SimEntityId,DetActorState>> actors(const DetCharacterController2DConfig& c){
    const auto half=ratio(1,2);std::vector<std::pair<SimEntityId,DetActorState>> result;
    const std::array<V,5> positions={v(0,1),v(-8,1),v(8,1),V{D::fromInt(16),ratio(545,512)},v(24,1)};
    for(unsigned i=0;i<5;++i)result.emplace_back(10+i,actor(c,DetBodyMode2D::Kinematic,positions[i],{half,half},{},1,6,false,true));
    result.emplace_back(20,actor(c,DetBodyMode2D::MovingObstacle,{}, {D::fromInt(4),half},{half,{}},2,1,false,false,true));
    result.emplace_back(21,actor(c,DetBodyMode2D::Static,v(-8,0),{D::fromInt(2),half},{},2,1));
    result.emplace_back(22,actor(c,DetBodyMode2D::Static,v(8,0),{D::fromInt(2),half},{},2,1,false,false,true));
    result.emplace_back(23,actor(c,DetBodyMode2D::Static,{D::fromInt(8),ratio(-3081,1024)},{D::fromInt(2),half},{},2,1));
    result.emplace_back(24,actor(c,DetBodyMode2D::Static,v(16,0),{D::fromInt(2),half},{},2,1,false,false,true));
    result.emplace_back(25,actor(c,DetBodyMode2D::Static,v(24,0),{half,half},{},2,1));
    result.emplace_back(26,actor(c,DetBodyMode2D::Static,{D::fromInt(24),ratio(-391,128)},{D::fromInt(4),half},{},2,1));
    result.emplace_back(40,actor(c,DetBodyMode2D::Static,{D::fromInt(8),-half},{ratio(1,4),ratio(1,4)},{},4,1,true));
    return result;
}
inline bool configure(DeterministicSession& session,bool reverse=false){
    const auto c=policy();
    if(!session.registerTypedSchema(statsDescriptor()) || !session.registerGlobalState(statsSchema))return false;
    auto inputSystem=[c](DetTickContext& ctx){
        for(const auto& cmd:ctx.input().commands){
            if(cmd.source==1 && cmd.type==inputType){
                if(cmd.payload.size()!=7 || std::any_of(cmd.payload.begin(),cmd.payload.end(),[](auto b){return b>1;}))return false;
                detCharacterInput2D(ctx,10,c,{D::fromInt(cmd.payload[0]?-1:1),bool(cmd.payload[2]),false,false});
                detCharacterInput2D(ctx,11,c,{{},false,bool(cmd.payload[3]),false});
                detCharacterInput2D(ctx,12,c,{{},false,false,bool(cmd.payload[4])});
                detCharacterInput2D(ctx,13,c,{{},bool(cmd.payload[5]),false,false});
                detCharacterInput2D(ctx,14,c,{D::fromInt(cmd.payload[1]?2:0),bool(cmd.payload[6]),false,false});
            }else if(cmd.source!=2 || cmd.type!=markerType || cmd.payload!=std::vector<std::uint8_t>{7})return false;
        }
        // End-boundary topology changes are registered input-system work, not
        // hidden external state. The roof appears only after the crouched shape.
        if(ctx.tick()==0)ctx.spawn(31,actor(c,DetBodyMode2D::Static,{D::fromInt(-8),ratio(3,2)}, {D::fromInt(2),ratio(1,4)},{},2,1));
        if(ctx.tick()==128)ctx.despawn(31);return true;
    };
    auto observe=[c](DetTickContext& ctx){
        auto add=[&](std::uint32_t field){ctx.writeGlobal(statsSchema,field,ctx.readGlobal<std::uint64_t>(statsSchema,field)+1);};
        for(const auto& cmd:ctx.events()){
            DetTriggerEvent2D event;
            if(cmd.type!=c.collision.eventType || !decodeDetTriggerEvent2D(cmd.payload,event) || event.pair!=DetCollisionPair2D{12,40})return false;
            add(10);add(30+10*static_cast<std::uint32_t>(event.phase));
            auto hash=ctx.readGlobal<std::uint64_t>(statsSchema,20);for(auto b:cmd.payload)hash=hash*33+b;ctx.writeGlobal(statsSchema,20,hash);
        }
        ctx.writeGlobal(statsSchema,30,ctx.tick()+1);
        for(auto [id,field]:{std::pair{10u,70u},std::pair{13u,80u},std::pair{14u,90u}})
            if(ctx.read<V>(id,c.stateSchema,DetCharacterVelocity).y==D::fromInt(2))add(field);
        const auto ignored=ctx.read<DetEntityRef>(12,c.collision.bodySchema,DetBodyIgnoredOneWay);
        const auto oldIgnored=ctx.readGlobal<DetEntityRef>(statsSchema,150);
        if(ignored.value && !oldIgnored.value)add(100);if(!ignored.value && oldIgnored.value)add(110);
        ctx.writeGlobal(statsSchema,150,ignored);
        const auto crouched=ctx.read<bool>(11,c.stateSchema,DetCharacterCrouched);
        if(crouched){add(120);if(!ctx.read<bool>(11,c.stateSchema,DetCharacterCrouchHeld))add(130);}
        if(!crouched && ctx.readGlobal<bool>(statsSchema,160))add(140);
        ctx.writeGlobal(statsSchema,160,crouched);return true;
    };
    if(reverse){if(!session.registerSystem(50,0,observe) || !installDetCharacterController2D(session,c) || !session.registerSystem(10,0,inputSystem))return false;}
    else if(!session.registerSystem(10,0,inputSystem) || !installDetCharacterController2D(session,c) || !session.registerSystem(50,0,observe))return false;
    auto initial=actors(c);if(reverse)std::reverse(initial.begin(),initial.end());
    for(auto& [id,state]:initial)if(!session.addEntity(id,std::move(state)))return false;return session.seal();
}
inline DetTickInput input(std::uint64_t tick,bool reverse=false){
    DetTickInput in{tick,1,{{1,0,inputType,{std::uint8_t(tick%128>=64),std::uint8_t(tick<32),
        std::uint8_t(tick>=16 && tick<100),std::uint8_t(tick==0),1,std::uint8_t(tick>=9),std::uint8_t(tick>=33)}},{2,0,markerType,{7}}}};
    if(reverse)std::reverse(in.commands.begin(),in.commands.end());return in;
}
inline std::array<std::uint8_t,24> eventBytes(unsigned phase){std::array<std::uint8_t,24> b{};b[0]=1;b[4]=std::uint8_t(phase);b[8]=12;b[16]=40;return b;}
struct HeroGolden {int x128=0,y2048=0,vx=0,vy16=0;SimEntityId support=0;unsigned coyote=0,buffer=0;};
inline std::array<HeroGolden,5> heroes(std::uint64_t n){
    const auto phase=int(n%128);const int triangle=phase<=64?phase:128-phase;
    // A releases carry on jump; the65 flight ticks (including landing tick)
    // contribute own X only. Carrier movement resumes on the following pre.
    const int missedCarry=std::clamp(int(n)-16,0,65);
    std::array<HeroGolden,5> g={HeroGolden{int(n)+2*triangle-missedCarry,2048,n?((n-1)%128>=64?-1:1):0,0,n?20u:0u,n?2u:0u,0},
        HeroGolden{-8*128,2048,0,0,n?21u:0u,n?2u:0u,0},HeroGolden{8*128,2048,0,0,n?22u:0u,0,0},
        HeroGolden{16*128,2180,0,0,0,0,0},HeroGolden{24*128+4*int(std::min(n,std::uint64_t{32})),2048,n&&n<=32?2:0,0,n?25u:0u,n?2u:0u,0}};
    if(n>16 && n<81){const int k=int(n)-16;g[0].y2048=2048+k*(65-k);g[0].vy16=33-k;g[0].support=0;g[0].coyote=0;}
    // Each landing is exactly a tick endpoint, not a fractional sweep of the
    // shared solver. D:2180-11*12=2048; C:2048-78*79=-4114;
    // E:2046+118*(65-118)=-4208. Earlier integer rows lie strictly above.
    if(n){g[2].support=0;g[2].y2048=std::max(-4114,2048-int(n*(n+1)));g[2].vy16=-int(n);
        if(n>=78){g[2].vy16=0;g[2].support=23;g[2].coyote=2;}}
    if(n<=10){g[3].y2048=2180-int(n*(n+1));g[3].vy16=-int(n);if(n==10)g[3].buffer=2;}
    else if(n==11){g[3].y2048=2048;g[3].support=24;g[3].coyote=2;g[3].buffer=1;}
    else if(n<76){const int k=int(n)-11;g[3].y2048=2048+k*(65-k);g[3].vy16=33-k;}
    else {g[3].y2048=2048;g[3].support=24;g[3].coyote=2;}
    if(n==32)g[4].support=0;
    if(n==33){g[4].y2048=2046;g[4].vy16=-1;g[4].support=0;g[4].coyote=1;}
    if(n>=34){const int k=int(n)-33;g[4].y2048=std::max(-4208,2046+k*(65-k));g[4].vy16=33-k;g[4].support=0;g[4].coyote=0;
        if(n>=151){g[4].vy16=0;g[4].support=26;g[4].coyote=2;}}
    return g;
}
struct Golden {std::array<std::uint64_t,14> stats{};unsigned pending=0;};
inline const std::vector<Golden>& golden(){static const auto rows=[](){std::vector<Golden> result(1);
    for(std::uint64_t n=1;n<=maxTicks;++n){auto row=result.back();if(row.pending){++row.stats[0];++row.stats[2+row.pending];for(auto b:eventBytes(row.pending))row.stats[1]=row.stats[1]*33+b;}
        row.stats[2]=n;row.stats[6]=(n>=17);row.stats[7]=(n>=12);row.stats[8]=(n>=34);
        row.stats[9]=1;row.stats[10]=(n>=65);row.stats[11]=std::min(n,std::uint64_t{129});
        row.stats[12]=std::min(n-1,std::uint64_t{128});row.stats[13]=(n>=130);
        row.pending=n==39?1u:(n>39 && n<=67?2u:(n==68?3u:0u));result.push_back(row);}return result;}();return rows;}
inline bool matchesGolden(const DetSessionCheckpoint& cp){
    const auto n=cp.nextTick;if(n>maxTicks || cp.actors.size()!=(n>0 && n<129?14u:13u)
        || cp.retiredIds!=(n>=129?std::vector<SimEntityId>{31}:std::vector<SimEntityId>{}))return false;
    const auto c=policy();const auto schema=stateSchema(c),body=bodySchema(c);const auto expected=heroes(n);
    for(unsigned i=0;i<5;++i){const auto id=10+i;const auto& actor=cp.actors.at(id);const auto& w=actor.blocks.at(c.stateSchema);const auto& b=actor.blocks.at(c.collision.bodySchema);const auto& g=expected[i];
        const auto get=[&]<class T>(std::uint32_t f){return readDetState<T>(schema,w,f);};
        if(actor.pose.position[0]!=ratio(g.x128,128).bits() || actor.pose.position[1]!=ratio(g.y2048,2048).bits()
            || !get.template operator()<bool>(DetCharacterEnabled) || get.template operator()<bool>(DetCharacterGrounded)!=bool(g.support)
            || get.template operator()<DetEntityRef>(DetCharacterSupport).value!=g.support
            || get.template operator()<std::uint64_t>(DetCharacterSupportTick)!=(g.support?n:0)
            || get.template operator()<V>(DetCharacterVelocity)!=V{D::fromInt(g.vx),ratio(g.vy16,16)}
            || get.template operator()<V>(DetCharacterSupportVelocity)!=V{i==0 && g.support?ratio(1,2):D{},D{}}
            || get.template operator()<D>(DetCharacterTargetSpeed)!=D::fromInt(g.vx)
            || get.template operator()<std::uint32_t>(DetCharacterCoyoteRemaining)!=g.coyote
            || get.template operator()<std::uint32_t>(DetCharacterJumpBufferRemaining)!=g.buffer)return false;
        const bool jump=(i==0 && n>=17 && n<=100) || (i==3 && n>=10) || (i==4 && n>=34);
        const bool drop=i==2 && n>0,crouch=i==1 && n==1,isCrouched=i==1 && n>0 && n<=129;
        if(get.template operator()<bool>(DetCharacterJumpHeld)!=jump || get.template operator()<bool>(DetCharacterJumpLatch)!=jump
            || get.template operator()<bool>(DetCharacterDropHeld)!=drop || get.template operator()<bool>(DetCharacterDropLatch)!=drop
            || get.template operator()<bool>(DetCharacterCrouchHeld)!=crouch || get.template operator()<bool>(DetCharacterCrouched)!=isCrouched
            || get.template operator()<bool>(DetCharacterStandingCaptured)!=(n>0)
            || get.template operator()<V>(DetCharacterStandingHalf)!=(n?V{ratio(1,2),ratio(1,2)}:V{})
            || get.template operator()<V>(DetCharacterStandingOffset)!=V{} || !get.template operator()<D>(DetCharacterCoyoteSupportY).isZero())return false;
        const bool ignored=i==2 && n>0 && n<=64;
        if(readDetState<DetEntityRef>(body,b,DetBodyIgnoredOneWay).value!=(ignored?22u:0u)
            || get.template operator()<std::uint32_t>(DetCharacterDropRemaining)!=(i==2 && n==1?1u:0u)
            || get.template operator()<std::uint64_t>(DetCharacterDropTick)!=(ignored?n:0)
            || readDetState<V>(body,b,DetBodyHalf)!=V{ratio(1,2),isCrouched?ratio(1,4):ratio(1,2)}
            || readDetState<V>(body,b,DetBodyOffset)!=V{D{},isCrouched?-ratio(1,4):D{}})return false;
        for(auto f:{DetCharacterJumpRequested,DetCharacterJumped,DetCharacterSnapped,DetCharacterDropStarted})if(get.template operator()<bool>(f))return false;
        for(auto f:{DetCharacterAppliedCarry,DetCharacterIntentVelocity})if(get.template operator()<V>(f)!=V{})return false;
    }
    if(cp.actors.at(20).pose.position[0]!=ratio(int(n),128).bits())return false;
    const auto& row=golden()[n];const auto& stats=cp.globals.at(statsSchema);
    for(unsigned i=0;i<14;++i)if(stats[i]!=row.stats[i])return false;
    if(stats[14]!=(n>0 && n<=64?22u:0u) || stats[15]!=(n>0 && n<=129?1u:0u) || cp.pendingEvents.size()!=bool(row.pending))return false;
    if(row.pending){const auto& e=cp.pendingEvents[0];const auto b=eventBytes(row.pending);
        if(e.source!=c.collision.systemId || e.sequence || e.type!=c.collision.eventType || e.payload.size()!=b.size() || !std::equal(e.payload.begin(),e.payload.end(),b.begin()))return false;}
    return true;
}
} // namespace detcharacter_advanced
