#pragma once
// design reference: AYEntity/design.md Stage23; bounded seeded advanced-character campaigns.
// Public typed descriptors are shared with Stage22. The numerical oracle below
// is an integer lattice with scheduled exact-endpoint flights; no solver formula
// or hidden controller state is called to compute expected positions/actions.
// It independently overwrites the documented motion/controller/stats/event
// channels, not every checkpoint byte. Metadata/closure and terminal body-Y
// alternatives are additionally compared byte-for-byte across actual lanes.
#include "DetCharacterAdvancedScenario.h"
#include <AYEntity/DeterministicRollback.h>
#include <cfenv>
#include <functional>
#include <string_view>
#if defined(_M_X64) || defined(__x86_64__)
#include <xmmintrin.h>
#endif

namespace detcharacter_campaign {
using namespace ayt::entity;
using D=ayt::math::DetFloat32;using V=ayt::math::DetVec2;using Bytes=std::vector<std::uint8_t>;
using Pulse=std::function<void(std::uint64_t,std::string_view)>;
inline std::uint64_t mix(std::uint64_t n){n+=0x9e3779b97f4a7c15ull;n=(n^(n>>30))*0xbf58476d1ce4e5b9ull;n=(n^(n>>27))*0x94d049bb133111ebull;return n^(n>>31);}
inline D q(int n,int d=2048){return D::fromInt(n)/D::fromInt(d);}
inline constexpr std::uint32_t statsSchema=8;
inline int signedByte(std::uint8_t n){return n<128?n:int(n)-256;}
struct Policy {
    std::uint64_t seed=0;std::uint32_t history=32,delay=3,jumpStart=8,landing=11,
        spawnTick=24,despawnTick=36,dropRelease=4,heldLength=75;
    int shift=0,carrier=1;SimEntityId platform=20,dynamic=1000;
    DetCharacterController2DConfig controller;
};
inline Policy policy(std::uint64_t seed){Policy p;p.seed=seed;const auto bits=mix(seed);
    p.controller=detcharacter_advanced::policy();auto& c=p.controller;
    c.coyoteTicks=1+std::uint32_t(bits%4);c.jumpBufferTicks=1+std::uint32_t((bits>>3)%4);
    c.dropThroughTicks=std::uint32_t((bits>>7)%4);c.collision.maxTriggerPairs=2;
    p.history=16+std::uint32_t((bits>>10)%33);p.delay=2+std::uint32_t((bits>>16)%4);
    p.jumpStart=8+std::uint32_t((bits>>20)%8);p.landing=8+std::uint32_t((bits>>24)%5);
    p.spawnTick=20+std::uint32_t((bits>>29)%16);p.despawnTick=p.spawnTick+8+std::uint32_t((bits>>34)%8);
    p.dropRelease=1+std::uint32_t((bits>>39)%8);p.heldLength=70+std::uint32_t((bits>>43)%11);
    p.shift=int((bits>>48)%17)-8;p.carrier=(bits>>54)&1?1:-1;p.platform=(bits>>55)&1?5:20;
    p.dynamic=1000+mix(seed+19)%100000;return p;
}
inline DetSessionConfig sessionConfig(const Policy& p){return {1,1,1,64,p.seed};}
inline bool heldA(const Policy& p,std::uint64_t t){return t>=p.jumpStart && (t-p.jumpStart)%96<p.heldLength;}
inline bool crouch(const Policy& p,std::uint64_t t,unsigned id){return (mix(p.seed^mix(t/8)^mix(id))&1)!=0;}
inline int targetA(const Policy& p,std::uint64_t t){return int(mix(p.seed^mix(t/4+9))%3)-1;}
inline DetTickInput input(const Policy& p,std::uint64_t t,bool reverse=false){const auto& c=p.controller;
    const auto dPress=p.landing-c.jumpBufferTicks,ePress=31+c.coyoteTicks;
    DetTickInput in{t,1,{{1,0,1,{std::uint8_t(targetA(p,t)),std::uint8_t(heldA(p,t)),std::uint8_t(crouch(p,t,10))}},
        {1,1,2,{std::uint8_t(t<p.dropRelease)}},
        {2,0,3,{std::uint8_t(t>=dPress && t<dPress+p.heldLength),std::uint8_t(crouch(p,t,12))}},
        {2,1,4,{std::uint8_t(t<32?2:0),std::uint8_t(t>=ePress && t<ePress+110),std::uint8_t(crouch(p,t,13))}},
        {2,2,5,{std::uint8_t(1+mix(p.seed^mix(t+101))%31)}}}};
    if(t==p.spawnTick || t==p.despawnTick)in.commands.push_back({1,2,6,{std::uint8_t(t==p.spawnTick?1:2)}});
    if(reverse)std::reverse(in.commands.begin(),in.commands.end());return in;
}
inline DetTickInput memberInput(DetTickInput in,std::uint32_t member){std::erase_if(in.commands,[member](const auto& c){return c.source!=member;});return in;}
inline Bytes canonicalInput(DetTickInput in){std::string error;if(!canonicalizeDetInput(in,error))throw std::runtime_error(error);return encodeDetInput(in);}
inline void appendCanonicalFrame(Bytes& frames,const DetTickInput& in){const auto bytes=canonicalInput(in);const auto n=std::uint32_t(bytes.size());for(unsigned i=0;i<4;++i)frames.push_back(std::uint8_t(n>>(8*i)));frames.insert(frames.end(),bytes.begin(),bytes.end());}
// Archive representative uses one logical member. Source/sequence are envelope
// identity only in this fixture; roles are command types and checkpoint authority
// is identical. This is explicitly not a real transport or two-peer acceptance.
inline DetTickInput archiveInput(const Policy& p,std::uint64_t t){auto in=input(p,t);std::string error;
    if(!canonicalizeDetInput(in,error))throw std::runtime_error(error);
    std::uint32_t seq=0;for(auto& c:in.commands){c.source=1;c.sequence=seq++;}return in;}
inline DetTypedStateSchema statsDescriptor(){DetTypedStateSchema s{statsSchema,1,{}};
    for(std::uint32_t f=1;f<=14;++f)s.fields.push_back({f,std::uint64_t{0}});return s;}
inline DetActorState actor(const Policy& p,DetBodyMode2D mode,V pos,V half,V velocity,
        std::uint32_t layer,std::uint32_t mask,bool trigger=false,bool character=false,bool oneWay=false,bool captured=false){
    auto a=detcharacter_advanced::actor(p.controller,mode,pos,half,velocity,layer,mask,trigger,character,oneWay);
    if(character && captured){DetCharacterState2D st;st.enabled=st.standingCaptured=true;st.standingHalf=half;
        a.blocks[p.controller.stateSchema]=detCharacterState2D(p.controller,st);}return a;}
inline DetActorState dynamicActor(const Policy& p){return actor(p,DetBodyMode2D::Kinematic,
    {D::fromInt(p.shift+256),D::fromInt(16)},{q(1,2),q(1,2)},{},512,0,false,true,false,true);}
inline bool configure(DeterministicSession& s,const Policy& p,bool reverse=false){const auto c=p.controller;
    if(!s.registerTypedSchema(statsDescriptor()) || !s.registerGlobalState(statsSchema)
        || !s.registerLogicProfile(90,1,0x4348415243414d31ull))return false;
    auto sample=[p,c](DetTickContext& ctx){for(const auto& in:ctx.input().commands){
        if(in.source!=1 && in.source!=2)return false;const auto& b=in.payload;
        if(in.type==1 && b.size()==3 && b[1]<=1 && b[2]<=1)
            detCharacterInput2D(ctx,10,c,{D::fromInt(signedByte(b[0])),bool(b[1]),bool(b[2]),false});
        else if(in.type==2 && b.size()==1 && b[0]<=1)detCharacterInput2D(ctx,11,c,{{},false,false,bool(b[0])});
        else if(in.type==3 && b.size()==2 && b[0]<=1 && b[1]<=1)detCharacterInput2D(ctx,12,c,{{},bool(b[0]),bool(b[1]),false});
        else if(in.type==4 && b.size()==3 && b[0]<=2 && b[1]<=1 && b[2]<=1)
            detCharacterInput2D(ctx,13,c,{D::fromInt(b[0]),bool(b[1]),bool(b[2]),false});
        else if(in.type==5 && b.size()==1)ctx.writeGlobal(statsSchema,2,ctx.readGlobal<std::uint64_t>(statsSchema,2)+b[0]);
        else if(in.type==6 && b.size()==1 && b[0]==1){ctx.spawn(p.dynamic,dynamicActor(p));ctx.writeGlobal(statsSchema,13,ctx.readGlobal<std::uint64_t>(statsSchema,13)+1);}
        else if(in.type==6 && b.size()==1 && b[0]==2){ctx.despawn(p.dynamic);ctx.writeGlobal(statsSchema,14,ctx.readGlobal<std::uint64_t>(statsSchema,14)+1);}
        else return false;}return true;};
    auto observe=[c](DetTickContext& ctx){auto add=[&](std::uint32_t f){ctx.writeGlobal(statsSchema,f,ctx.readGlobal<std::uint64_t>(statsSchema,f)+1);};
        for(const auto& cmd:ctx.events()){DetTriggerEvent2D event;
            if(cmd.type!=c.collision.eventType || !decodeDetTriggerEvent2D(cmd.payload,event) || event.pair!=DetCollisionPair2D{11,80})return false;
            add(3);auto h=ctx.readGlobal<std::uint64_t>(statsSchema,4);for(auto b:cmd.payload)h=h*33+b;ctx.writeGlobal(statsSchema,4,h);}
        ctx.writeGlobal(statsSchema,1,ctx.tick()+1);
        for(auto [id,f]:{std::pair{10u,5u},std::pair{12u,6u},std::pair{13u,7u}})if(ctx.read<V>(id,c.stateSchema,DetCharacterVelocity).y==D::fromInt(2))add(f);
        const auto ignored=ctx.read<DetEntityRef>(11,c.collision.bodySchema,DetBodyIgnoredOneWay).value,old=ctx.readGlobal<std::uint64_t>(statsSchema,11);
        if(ignored && !old)add(8);if(!ignored && old)add(12);ctx.writeGlobal(statsSchema,11,ignored);
        std::uint64_t mask=0;for(auto [id,bit]:{std::pair{10u,1u},std::pair{12u,2u},std::pair{13u,4u}})if(ctx.read<bool>(id,c.stateSchema,DetCharacterCrouched))mask|=bit;
        auto diff=mask^ctx.readGlobal<std::uint64_t>(statsSchema,10);for(unsigned bit=0;bit<3;++bit)if(diff&(1u<<bit))add(9);ctx.writeGlobal(statsSchema,10,mask);return true;};
    if(reverse){if(!s.registerSystem(50,0,observe) || !installDetCharacterController2D(s,c) || !s.registerSystem(10,0,sample))return false;}
    else if(!s.registerSystem(10,0,sample) || !installDetCharacterController2D(s,c) || !s.registerSystem(50,0,observe))return false;
    const auto half=q(1,2);const int eBase=2048-int((c.coyoteTicks-1)*c.coyoteTicks),eFloor=eBase-2976;
    std::vector<std::pair<SimEntityId,DetActorState>> all={
        {10,actor(p,DetBodyMode2D::Kinematic,{D::fromInt(p.shift),D::fromInt(1)},{half,half},{},1,2,false,true)},
        {11,actor(p,DetBodyMode2D::Kinematic,{D::fromInt(p.shift+16),D::fromInt(1)},{half,half},{},4,72,false,true)},
        {12,actor(p,DetBodyMode2D::Kinematic,{D::fromInt(p.shift+32),q(2048+int(p.landing*(p.landing+1)))},{half,half},{},16,32,false,true)},
        {13,actor(p,DetBodyMode2D::Kinematic,{D::fromInt(p.shift+56),D::fromInt(1)},{half,half},{},128,256,false,true)},
        {p.platform,actor(p,DetBodyMode2D::MovingObstacle,{D::fromInt(p.shift),{}},{D::fromInt(128),half},{q(p.carrier,2),{}},2,1,false,false,true)},
        {21,actor(p,DetBodyMode2D::Static,{D::fromInt(p.shift+16),{}},{D::fromInt(128),half},{},8,4,false,false,true)},
        {22,actor(p,DetBodyMode2D::Static,{D::fromInt(p.shift+16),q(-65,32)},{D::fromInt(128),half},{},8,4,false,false,true)},
        {30,actor(p,DetBodyMode2D::Static,{D::fromInt(p.shift+32),{}},{D::fromInt(128),half},{},32,16,false,false,true)},
        {40,actor(p,DetBodyMode2D::Static,{D::fromInt(p.shift+56),{}},{half,half},{},256,128)},
        {41,actor(p,DetBodyMode2D::Static,{D::fromInt(p.shift+56),q(eFloor-2048)},{D::fromInt(128),half},{},256,128)},
        {80,actor(p,DetBodyMode2D::Static,{D::fromInt(p.shift+16),{}},{q(1,4),q(1,4)},{},64,4,true)}};
    if(reverse)std::reverse(all.begin(),all.end());for(auto& [id,a]:all)if(!s.addEntity(id,std::move(a)))return false;return s.seal();
}
inline std::array<std::uint8_t,24> eventBytes(unsigned phase){std::array<std::uint8_t,24> b{};b[0]=1;b[4]=std::uint8_t(phase);b[8]=11;b[16]=80;return b;}
struct Oracle {std::uint64_t n=0;int aX128=0;std::array<std::uint64_t,14> stats{};unsigned pending=0;
    explicit Oracle(const Policy& p):aX128(p.shift*128){}
    void step(const Policy& p){++n;const auto t=n-1;const bool flight=t>=p.jumpStart && (t-p.jumpStart)%96<65;
        aX128+=2*targetA(p,t)+(flight?0:p.carrier);
        if(pending){++stats[2];for(auto b:eventBytes(pending))stats[3]=stats[3]*33+b;}
        stats[0]=n;stats[1]+=1+mix(p.seed^mix(t+101))%31;
        if(t>=p.jumpStart && (t-p.jumpStart)%96==0)++stats[4];
        stats[5]=(n>=p.landing+1);stats[6]=(n>=32+p.controller.coyoteTicks);stats[7]=1;
        std::uint64_t mask=0;if(crouch(p,t,10))mask|=1;if(crouch(p,t,12))mask|=2;if(crouch(p,t,13))mask|=4;
        auto difference=mask^stats[9];for(unsigned bit=0;bit<3;++bit)if(difference&(1u<<bit))++stats[8];stats[9]=mask;
        stats[10]=n<=64?21:0;stats[11]=(n>=65);stats[12]=(n>p.spawnTick);stats[13]=(n>p.despawnTick);
        pending=n==23?1u:(n>23 && n<=59?2u:(n==60?3u:0u));
    }
};
struct HeroExpected {int x128=0,y2048=2048,vx=0,vy16=0;SimEntityId support=0;std::uint32_t coyote=0,buffer=0;bool jump=false,drop=false,crouch=false;};
inline std::array<HeroExpected,4> heroes(const Policy& p,const Oracle& o){const auto n=o.n;const auto& c=p.controller;
    std::array<HeroExpected,4> e={HeroExpected{o.aX128},HeroExpected{(p.shift+16)*128},
        HeroExpected{(p.shift+32)*128},HeroExpected{(p.shift+56)*128+4*int(std::min(n,std::uint64_t{32}))}};
    if(!n){e[2].y2048=2048+int(p.landing*(p.landing+1));return e;}
    const auto t=n-1;for(unsigned i=0;i<4;++i){e[i].crouch=i!=1 && crouch(p,t,10+i);e[i].coyote=c.coyoteTicks;}
    auto& a=e[0];a.vx=targetA(p,t);a.support=p.platform;a.jump=heldA(p,t);
    if(n>p.jumpStart){const auto k=(n-p.jumpStart-1)%96+1;if(k<=64){a.y2048=2048+int(k*(65-k));a.vy16=33-int(k);a.support=0;a.coyote=0;}}
    auto& b=e[1];b.y2048=std::max(-2112,2048-int(n*(n+1)));b.drop=t<p.dropRelease;
    if(n<64){b.vy16=-int(n);b.coyote=0;}else b.support=22;
    auto& d=e[2];const auto press=p.landing-c.jumpBufferTicks;d.jump=t>=press && t<press+p.heldLength;
    if(n<p.landing){d.y2048=2048+int(p.landing*(p.landing+1))-int(n*(n+1));d.vy16=-int(n);d.coyote=0;}
    else if(n==p.landing)d.support=30;
    else if(n<p.landing+65){const int k=int(n)-int(p.landing);d.y2048=2048+k*(65-k);d.vy16=33-k;d.coyote=0;}
    else d.support=30;
    if(n>press && n<=p.landing)d.buffer=std::uint32_t(p.landing-n+1);
    auto& f=e[3];f.vx=n<=32?2:0;const auto wait=c.coyoteTicks;const auto pressE=31+wait;f.jump=t>=pressE && t<pressE+110;
    if(n<32)f.support=40;
    else if(n<32+wait){const int r=int(n)-32;f.y2048=2048-r*(r+1);f.vy16=-r;f.coyote=wait-std::uint32_t(r);}
    else {const int k=int(n)-int(31+wait),base=2048-int((wait-1)*wait),floor=base-2976;
        f.y2048=std::max(floor,base+k*(65-k));f.vy16=33-k;f.coyote=0;if(k>=96){f.vy16=0;f.support=41;f.coyote=wait;}}
    return e;
}
// These are ONLY the first exact-endpoint arrivals in the hand-derived flights.
// If simultaneous contacts end the common motion step, another stable-ID pair
// can consume its last response before this body's incoming Y is cut. Controller
// post then grounds and clears OWN Y, without rewriting the collision body Y.
// Thus this one channel has the independent finite set {incoming,+0}; neither a
// generic grounded state nor an arbitrary downward value qualifies here.
inline std::array<std::optional<D>,4> terminalBodyIncoming(const Policy& p,std::uint64_t n){
    std::array<std::optional<D>,4> result;
    if(n>p.jumpStart && (n-p.jumpStart)%96==65)result[0]=-D::fromInt(2);
    if(n==64)result[1]=-D::fromInt(4);
    if(n==p.landing)result[2]=q(-int(p.landing),16);
    else if(n==p.landing+65)result[2]=-D::fromInt(2);
    if(n==127+p.controller.coyoteTicks)result[3]=q(-63,16);
    return result;
}
inline DetSessionCheckpoint oracleCheckpoint(const DetSessionCheckpoint& actual,const Policy& p,const Oracle& o){
    auto expected=actual;const auto n=o.n;const auto schema=detcharacter_advanced::stateSchema(p.controller),body=detcharacter_advanced::bodySchema(p.controller);const auto e=heroes(p,o);const auto& c=p.controller;const auto terminal=terminalBodyIncoming(p,n);
    for(unsigned i=0;i<4;++i){const auto id=10+i;auto& a=expected.actors.at(id);const auto& g=e[i];a.pose.position[0]=q(g.x128,128).bits();a.pose.position[1]=q(g.y2048).bits();
        auto& w=a.blocks.at(c.stateSchema);auto set=[&](std::uint32_t f,auto value){writeDetState(schema,w,f,value);};
        set(DetCharacterEnabled,true);set(DetCharacterGrounded,bool(g.support));set(DetCharacterSupport,DetEntityRef{g.support});set(DetCharacterSupportTick,g.support?n:0);
        set(DetCharacterVelocity,V{D::fromInt(g.vx),q(g.vy16,16)});set(DetCharacterSupportVelocity,V{i==0 && g.support?q(p.carrier,2):D{},D{}});set(DetCharacterTargetSpeed,D::fromInt(g.vx));
        set(DetCharacterCoyoteRemaining,g.coyote);set(DetCharacterJumpBufferRemaining,g.buffer);set(DetCharacterCoyoteSupportY,D{});
        set(DetCharacterJumpHeld,g.jump);set(DetCharacterJumpLatch,g.jump);set(DetCharacterDropHeld,g.drop);set(DetCharacterDropLatch,g.drop);set(DetCharacterCrouchHeld,g.crouch);set(DetCharacterCrouched,g.crouch);
        set(DetCharacterStandingCaptured,n>0);set(DetCharacterStandingHalf,n?V{q(1,2),q(1,2)}:V{});set(DetCharacterStandingOffset,V{});
        const bool ignored=i==1 && n>0 && n<=64;const auto lifetime=std::max(1u,c.dropThroughTicks);
        set(DetCharacterDropRemaining,i==1 && n>0 && n<lifetime?std::uint32_t(lifetime-n):std::uint32_t{0});set(DetCharacterDropTick,ignored?n:0);
        for(auto field:{DetCharacterJumpRequested,DetCharacterJumped,DetCharacterSnapped,DetCharacterDropStarted})set(field,false);
        for(auto field:{DetCharacterAppliedCarry,DetCharacterIntentVelocity})set(field,V{});
        auto& b=a.blocks.at(c.collision.bodySchema);writeDetState(body,b,DetBodyHalf,V{q(1,2),g.crouch?q(1,4):q(1,2)});writeDetState(body,b,DetBodyOffset,V{D{},g.crouch?-q(1,4):D{}});writeDetState(body,b,DetBodyIgnoredOneWay,DetEntityRef{ignored?21u:0u});
        const bool releasedFlight=n && n-1>=p.jumpStart && (n-1-p.jumpStart)%96<65;
        auto velocity=readDetState<V>(body,b,DetBodyVelocity);
        velocity.x=D::fromInt(g.vx)+(i==0 && n && !releasedFlight?q(p.carrier,2):D{});
        // The exact terminal Y set is checked separately; copying that excluded
        // channel here does not claim an independently generated complete CP.
        if(!terminal[i])velocity.y=q(g.vy16,16);
        writeDetState(body,b,DetBodyVelocity,velocity);
    }
    expected.actors.at(p.platform).pose.position[0]=q(p.shift*128+p.carrier*int(n),128).bits();
    const bool alive=n>p.spawnTick && n<=p.despawnTick;
    if(alive){if(!expected.actors.contains(p.dynamic)){auto authored=dynamicActor(p);auto block=expected.actors.at(10);
            block.pose=authored.pose;for(auto& [id,w]:authored.blocks)block.blocks[id]=std::move(w);expected.actors.emplace(p.dynamic,std::move(block));}
        auto& a=expected.actors.at(p.dynamic);const auto k=int(n-p.spawnTick-1);
        a.pose.position[0]=D::fromInt(p.shift+256).bits();a.pose.position[1]=q(16*2048-k*(k+1)).bits();
        DetCharacterState2D state;state.enabled=state.standingCaptured=true;state.standingHalf={q(1,2),q(1,2)};state.velocity.y=q(-k,16);
        a.blocks[c.stateSchema]=detCharacterState2D(c,state);
        a.blocks[c.collision.bodySchema]=detCollisionBodyState2D(c.collision,{DetBodyMode2D::Kinematic,{q(1,2),q(1,2)},{},{D{},q(-k,16)},512,0});
    }else expected.actors.erase(p.dynamic);
    expected.retiredIds=n>p.despawnTick?std::vector<SimEntityId>{p.dynamic}:std::vector<SimEntityId>{};
    expected.globals.at(statsSchema)=std::vector<std::uint64_t>(o.stats.begin(),o.stats.end());expected.pendingEvents.clear();
    if(o.pending){const auto b=eventBytes(o.pending);expected.pendingEvents.push_back({c.collision.systemId,0,c.collision.eventType,Bytes(b.begin(),b.end())});}
    return expected;
}
inline void environment(int rounding,bool ftz){if(std::fesetround(rounding)!=0)throw std::runtime_error("host rounding mode unavailable");
#if defined(_M_X64) || defined(__x86_64__)
    const auto csr=_mm_getcsr();_mm_setcsr(ftz?csr|0x8040u:csr&~0x8040u);
#else
    (void)ftz;
#endif
}
struct CaseResult {std::uint64_t seed=0,ticks=0,checkpointsChecked=0,eventsChecked=0,restoredTicks=0,
    rollbacks=0,replayedTicks=0,duplicates=0,finalHash=0,jumps=0,buffered=0,coyote=0,drops=0,crouchChanges=0,spawns=0,despawns=0,terminalBodyChecks=0;};
struct CaseFailure:std::runtime_error {
    std::uint64_t seed=0,tick=0;std::uint32_t requestedTicks=0;std::string phase;
    std::optional<DetStateDifference> difference;std::optional<std::uint32_t> inputMember;Bytes before,expected,actual,failingInput,submittedOriginalInputs,submittedVariantInputs;
    CaseFailure(std::uint64_t s,std::uint64_t t,std::uint32_t n,std::string lane,std::string what):std::runtime_error(std::move(what)),seed(s),tick(t),requestedTicks(n),phase(std::move(lane)){}
};
// Both the live baseline and actual original-input recorder call this check
// before the partial numerical oracle excludes terminal body Y. The returned
// count is accumulated ONLY for the live baseline; recording is separate work.
inline std::uint64_t validateTerminalBodySet(const DetSessionCheckpoint& actual,const Policy& p,const Oracle& oracle,
        std::uint32_t count,const std::string& phase,const DetSessionCheckpoint* before=nullptr,const Bytes& frame={}){
    const auto terminal=terminalBodyIncoming(p,oracle.n);const auto body=detcharacter_advanced::bodySchema(p.controller);std::uint64_t checked=0;
    for(unsigned i=0;i<terminal.size();++i)if(terminal[i]){++checked;const auto id=SimEntityId{10+i};
        const auto velocity=readDetState<V>(body,actual.actors.at(id).blocks.at(p.controller.collision.bodySchema),DetBodyVelocity);
        if(velocity.y.bits()==D{}.bits() || velocity.y.bits()==terminal[i]->bits())continue;
        const auto tick=actual.nextTick?actual.nextTick-1:0;CaseFailure e(p.seed,tick,count,phase,
            "terminal bodyY expected incoming bits "+std::to_string(terminal[i]->bits())+" or +0 bits0, actual "+std::to_string(velocity.y.bits()));
        auto expected=actual;auto permitted=velocity;permitted.y=D{};
        writeDetState(body,expected.actors.at(id).blocks.at(p.controller.collision.bodySchema),DetBodyVelocity,permitted);
        e.expected=encodeDetCheckpoint(expected);e.actual=encodeDetCheckpoint(actual);e.difference=firstDetDifference(expected,actual);
        if(before)e.before=encodeDetCheckpoint(*before);e.failingInput=frame.empty()?canonicalInput(input(p,tick)):frame;throw e;
    }return checked;
}
inline CaseResult runCase(std::uint64_t seed,std::uint32_t requestedTicks=0,const Pulse& pulse={});
struct BoundaryResult {std::uint64_t ticks=0,checkpoints=0,templates=0,lastHash=0;};
inline BoundaryResult longBoundaries(const Pulse& pulse={});
inline CaseResult runCase(std::uint64_t seed,std::uint32_t requestedTicks,const Pulse& pulse){
    const auto p=policy(seed);const auto count=requestedTicks?requestedTicks:256+static_cast<std::uint32_t>(mix(seed+1)%257);
    CaseResult result;result.seed=seed;result.ticks=count;
    std::uint64_t current=0;std::string phase="configuration";
    std::optional<DetSessionCheckpoint> witnessBefore,witnessExpected,submitBefore;DeterministicSession* active=nullptr;Bytes originalSubmitted,submitInput;
    std::optional<std::uint64_t> submitTick;std::optional<std::uint32_t> submitMember;
    auto fail=[&](const std::string& text)->void{CaseFailure e(seed,submitTick.value_or(current),count,phase,text);
        if(submitTick){if(submitBefore)e.before=encodeDetCheckpoint(*submitBefore);e.inputMember=submitMember;e.failingInput=submitInput;}
        else {if(witnessBefore)e.before=encodeDetCheckpoint(*witnessBefore);if(witnessExpected)e.expected=encodeDetCheckpoint(*witnessExpected);
            e.failingInput=canonicalInput(input(p,std::min(current,std::uint64_t(count-1))));}
        if(active)if(auto actual=active->checkpoint()){e.actual=encodeDetCheckpoint(*actual);if(!submitTick && witnessExpected)e.difference=firstDetDifference(*witnessExpected,*actual);}
        e.submittedOriginalInputs=originalSubmitted;throw e;};
    auto require=[&](bool condition,const std::string& text){if(!condition)fail(text);};
    try{
        const auto config=sessionConfig(p);
        DeterministicSession baseline(config),corrected(config),restored(config);
        require(configure(baseline,p,false) && configure(corrected,p,true) && configure(restored,p,(seed&1)!=0),"scenario configuration");
        DetRollbackConfig bounds;bounds.members={2,1};bounds.historyTicks=p.history;bounds.maxPredictionTicks=p.delay+2;
        bounds.futureTicks=8;bounds.maxBufferedBytes=16*1024*1024;
        // Held samples predict Hold. Only type5 is a per-tick integer delta:
        // its all-zero payload adds exactly zero and has no action/latch effect.
        bounds.prediction={{1,DetPredictionMode::Hold},{2,DetPredictionMode::Hold},{3,DetPredictionMode::Hold},{4,DetPredictionMode::Hold},{5,DetPredictionMode::Zero},{6,DetPredictionMode::Omit}};
        DeterministicRollback owner(corrected,bounds);
        // Bind submission failures to the actual contribution and the corrected
        // quiescent state. A partially predicted head has no independent complete
        // expected CP, so that witness is deliberately absent for this operation.
        auto submit=[&](std::uint32_t member,DetTickInput contribution){
            submitTick=contribution.tick;submitMember=member;submitBefore=corrected.checkpoint();
            submitInput=canonicalInput(contribution);active=&corrected;
            const bool valid=owner.submit(member,std::move(contribution));require(valid,owner.error());
            submitTick.reset();submitMember.reset();submitBefore.reset();submitInput.clear();
        };
        Oracle oracle(p);
        std::map<std::uint64_t,DetSessionCheckpoint> expected;
        expected.emplace(0,*baseline.checkpoint());witnessBefore=witnessExpected=expected.at(0);active=&baseline;
        std::vector<DetConfirmedEvent> expectedEvents,actualEvents;
        struct Arrival {std::uint64_t due,order;std::uint32_t member;DetTickInput contribution;};
        std::vector<Arrival> arrivals;
        std::uint64_t checked=0;
        auto compare=[&](const DetSessionCheckpoint& reference,const DetSessionCheckpoint& observed,const std::string& text){
            if(encodeDetCheckpoint(reference)!=encodeDetCheckpoint(observed)){
                CaseFailure error(seed,current,count,phase,text);error.difference=firstDetDifference(reference,observed);
                error.expected=encodeDetCheckpoint(reference);error.actual=encodeDetCheckpoint(observed);
                const auto tick=reference.nextTick?reference.nextTick-1:0;
                if(expected.contains(tick))error.before=encodeDetCheckpoint(expected.at(tick));
                error.failingInput=canonicalInput(input(p,tick));error.submittedOriginalInputs=originalSubmitted;throw error;
            }
            ++result.checkpointsChecked;
        };
        auto confirm=[&]{
            phase="confirmed-checkpoints";
            while(checked<owner.confirmedNextTick()){
                ++checked;const auto actual=owner.checkpointAt(checked);require(actual.has_value(),"confirmed checkpoint evicted before inspection");
                compare(expected.at(checked),*actual,"first corrected confirmed state mismatch");
            }
            phase="confirmed-events";auto batch=owner.takeConfirmedEvents();
            actualEvents.insert(actualEvents.end(),batch.begin(),batch.end());
            require(actualEvents.size()<=expectedEvents.size(),"unexpected or repeated confirmed effect");
            for(std::size_t i=actualEvents.size()-batch.size();i<actualEvents.size();++i)
                require(actualEvents[i]==expectedEvents[i],"first confirmed effect identity/payload mismatch at index "+std::to_string(i));
            result.eventsChecked+=batch.size();require(owner.takeConfirmedEvents().empty(),"drained effects delivered twice");
        };
        auto ingress=[&](std::uint64_t clock){
            phase="late-input";
            std::sort(arrivals.begin(),arrivals.end(),[](const auto& a,const auto& b){return a.order<b.order;});
            for(auto it=arrivals.begin();it!=arrivals.end();){
                if(it->due>clock){++it;continue;}
                submit(it->member,it->contribution);
                // Short cases can legitimately have no random duplicate. The
                // bootstrap member1 frame provides explicit per-case coverage;
                // remaining duplicates still follow the seeded arrival schedule.
                if(it->order%3==0 || (it->contribution.tick==0 && it->member==1)){
                    auto duplicate=it->contribution;std::reverse(duplicate.commands.begin(),duplicate.commands.end());
                    submit(it->member,std::move(duplicate));++result.duplicates;}
                it=arrivals.erase(it);confirm();phase="late-input";
            }
        };
        for(std::uint64_t tick=0;tick<count;++tick){
            current=tick;if(pulse && tick%32==0)pulse(tick,"baseline");active=&baseline;witnessBefore=*baseline.checkpoint();environment(FE_UPWARD,true);phase="baseline";appendCanonicalFrame(originalSubmitted,input(p,tick,(tick&1)!=0));require(baseline.advance(input(p,tick,(tick&1)!=0)),baseline.error());
            const auto boundary=*baseline.checkpoint();expected.emplace(tick+1,boundary);witnessExpected=boundary;
            oracle.step(p);phase="independent-integer-oracle";
            result.terminalBodyChecks+=validateTerminalBodySet(boundary,p,oracle,count,phase,witnessBefore?&*witnessBefore:nullptr);
            witnessExpected=oracleCheckpoint(boundary,p,oracle);
            compare(*witnessExpected,boundary,"dyadic advanced-character oracle mismatch");
            for(const auto& event:boundary.pendingEvents)expectedEvents.push_back({1,tick,event});
            active=&restored;environment(FE_DOWNWARD,false);phase="reverse-fenv-restore";require(restored.advance(input(p,tick,(tick&1)==0)),restored.error());
            compare(boundary,*restored.checkpoint(),"independent registration-order phase mismatch");
            const auto bits=mix(seed^mix(tick+3));
            if(tick>8 && bits%17==0){
                const auto depth=1+bits%8,start=tick+1-depth;
                require(restored.restore(expected.at(start)),restored.error());
                for(auto t=start;t<=tick;++t){current=t;require(restored.advance(input(p,t,true)),restored.error());
                    compare(expected.at(t+1),*restored.checkpoint(),"restored/replayed full state mismatch");++result.restoredTicks;}
                current=tick;
            }
            const auto full=input(p,tick);
            for(std::uint32_t member:{1u,2u}){
                auto contribution=full;std::erase_if(contribution.commands,[member](const auto& c){return c.source!=member;});
                const auto order=mix(seed^mix(tick)^mix(member+9));
                const auto due=tick==0?0:(tick==1 && member==2?tick+p.delay:tick+order%(p.delay+1));
                arrivals.push_back({due,order,member,std::move(contribution)});
            }
            // A second, identical future contribution exercises future buffering
            // and duplicates without allowing it to seed predictions early.
            active=&corrected;if(tick>2 && tick+2<count && bits%11==0){auto future=input(p,tick+2,true);
                const auto member=static_cast<std::uint32_t>(1+bits%2);
                std::erase_if(future.commands,[member](const auto& c){return c.source!=member;});
                phase="future-input";submit(member,std::move(future));}
            environment(FE_TOWARDZERO,true);ingress(tick);phase="predicted-advance";require(owner.ready() && owner.advance(),owner.error());
            require(corrected.nextTick()==tick+1,"rollback forward head mismatch");confirm();
            if(tick>p.history && bits%23==0){
                phase="explicit-history-replay";const auto before=*corrected.checkpoint();
                const auto depth=1+bits%std::min<std::uint64_t>(8,corrected.nextTick()-owner.oldestTick());
                require(owner.replayFrom(corrected.nextTick()-depth),owner.error());
                compare(before,*corrected.checkpoint(),"unchanged-input explicit history replay mismatch");
                require(owner.takeConfirmedEvents().empty(),"explicit replay repeated already drained effects");
            }
            phase="budgets";const auto diagnostic=owner.diagnostics();
            require(diagnostic.bufferedBytes<=bounds.maxBufferedBytes && diagnostic.head-diagnostic.confirmed<=bounds.maxPredictionTicks,"bounded history/prediction invariant");
            // Retain only the current correction/replay horizon, rather than all
            // prior checkpoints. Inputs are pure functions of (seed,tick).
            const auto floor=std::min(checked,tick>p.history?tick-p.history:0);
            while(expected.size()>1 && expected.begin()->first<floor)expected.erase(expected.begin());
        }
        current=count;ingress(count+p.delay+1);confirm();phase="final";
        require(arrivals.empty() && owner.confirmedNextTick()==count,"unconfirmed final contribution");
        compare(*baseline.checkpoint(),*corrected.checkpoint(),"final corrected full state mismatch");
        require(actualEvents==expectedEvents,"missing final confirmed effects");
        const auto diagnostic=owner.diagnostics();result.rollbacks=diagnostic.rollbacks;result.replayedTicks=diagnostic.replayedTicks;
        require(result.rollbacks>0 && result.replayedTicks>0 && result.duplicates>0,"seed did not exercise correction/duplicate phases");
        const auto final=*baseline.checkpoint();const auto& stats=final.globals.at(statsSchema);
        result.jumps=stats[4];result.buffered=stats[5];result.coyote=stats[6];result.drops=stats[7];result.crouchChanges=stats[8];result.spawns=stats[12];result.despawns=stats[13];
        require(result.jumps>0 && result.buffered==1 && result.coyote==1 && result.drops==1 && result.crouchChanges>0 && result.spawns==1 && result.despawns==1,"case did not exercise advanced features/topology");
        result.finalHash=detCheckpointHash(final);return result;
    }catch(CaseFailure& error){if(error.submittedOriginalInputs.empty())error.submittedOriginalInputs=std::move(originalSubmitted);throw;}
    catch(const std::exception& error){CaseFailure detail(seed,submitTick.value_or(current),count,phase,error.what());
        if(submitTick){if(submitBefore)detail.before=encodeDetCheckpoint(*submitBefore);detail.inputMember=submitMember;detail.failingInput=submitInput;}
        else {if(witnessBefore)detail.before=encodeDetCheckpoint(*witnessBefore);if(witnessExpected)detail.expected=encodeDetCheckpoint(*witnessExpected);
            detail.failingInput=canonicalInput(input(p,std::min(current,std::uint64_t(count-1))));}
        detail.submittedOriginalInputs=std::move(originalSubmitted);throw detail;}
}
inline BoundaryResult longBoundaries(const Pulse& pulse){
    constexpr std::uint32_t N=65535;BoundaryResult total;std::string phase="long-config";std::uint64_t tick=0;
    std::optional<DetSessionCheckpoint> witnessBefore,witnessActual,witnessExpected;DetTickInput lastInput;
    auto require=[&](bool value,const std::string& why){if(!value){CaseFailure e(0,tick,N+3,phase,why);
        if(witnessBefore)e.before=encodeDetCheckpoint(*witnessBefore);if(witnessActual)e.actual=encodeDetCheckpoint(*witnessActual);
        if(witnessExpected){e.expected=encodeDetCheckpoint(*witnessExpected);if(witnessActual)e.difference=firstDetDifference(*witnessExpected,*witnessActual);}
        e.failingInput=canonicalInput(lastInput);throw e;}};
    auto config=[](){auto c=detcharacter_advanced::policy();c.gravity={};c.coyoteTicks=c.jumpBufferTicks=c.dropThroughTicks=0;c.crouchEnabled=false;c.collision.maxBodies=4;c.collision.maxTriggerPairs=1;return c;};
    auto setup=[&](DeterministicSession& s,const DetCharacterController2DConfig& c,bool floor,bool moving){
        auto makeFloor=[c,moving](){return detcharacter_advanced::actor(c,moving?DetBodyMode2D::MovingObstacle:DetBodyMode2D::Static,{}, {q(1,2),q(1,2)},moving?V{D{},q(1,64)}:V{},2,1,false,false,true);};
        require(s.registerSystem(10,0,[c,makeFloor](DetTickContext& ctx){for(const auto& cmd:ctx.input().commands){
            if(cmd.type==1 && cmd.payload.size()==3)detCharacterInput2D(ctx,10,c,{D::fromInt(cmd.payload[2]),bool(cmd.payload[0]),false,bool(cmd.payload[1])});
            else if(cmd.type==2 && cmd.payload.empty())ctx.spawn(20,makeFloor());
            else if(cmd.type==3 && cmd.payload.empty())ctx.despawn(20);
            else if(cmd.type==4 && cmd.payload.empty())ctx.write(20,c.collision.bodySchema,DetBodyVelocity,V{});
            else return false;}return true;}) && installDetCharacterController2D(s,c),"boundary configure");
        require(s.addEntity(10,detcharacter_advanced::actor(c,DetBodyMode2D::Kinematic,{D{},D::fromInt(1)},{q(1,2),q(1,2)},{},1,2,false,true)),"boundary character");
        if(floor)require(s.addEntity(20,makeFloor()),"boundary floor");require(s.seal(),s.error());
    };
    auto cp=[&](DeterministicSession& s){auto value=s.checkpoint();require(bool(value),s.error());++total.checkpoints;witnessActual=*value;witnessExpected.reset();return *value;};
    auto run=[&](DeterministicSession& s,std::vector<DetTickCommand> commands={}){tick=s.nextTick();
        witnessBefore=s.checkpoint();witnessActual.reset();witnessExpected.reset();lastInput={tick,1,std::move(commands)};
        if(pulse && tick%64==0)pulse(tick,phase);require(s.advance(lastInput),s.error());++total.ticks;};
    auto scalar=[&]<class T>(const DetSessionCheckpoint& checkpoint,const DetCharacterController2DConfig& c,std::uint32_t field){return readDetState<T>(detcharacter_advanced::stateSchema(c),checkpoint.actors.at(10).blocks.at(c.stateSchema),field);};
    auto button=[](bool jump,bool drop=false,unsigned x=0){return DetTickCommand{1,0,1,{std::uint8_t(jump),std::uint8_t(drop),std::uint8_t(x)}};};
    phase="long-coyote-last-expired";auto c=config();c.coyoteTicks=N;
    DeterministicSession cy({1,1,1,64,0x43594f54453535ull});setup(cy,c,true,false);
    while(cy.nextTick()<N){if(!cy.nextTick())run(cy,{{1,0,3,{}}});else run(cy);
        const auto row=cp(cy);witnessExpected=row;witnessExpected->actors.at(10).pose.position[1]=D::fromInt(1).bits();
        writeDetState(detcharacter_advanced::stateSchema(c),witnessExpected->actors.at(10).blocks.at(c.stateSchema),DetCharacterCoyoteRemaining,N-std::uint32_t(row.nextTick-1));
        require(row.actors.at(10).pose.position[1]==D::fromInt(1).bits(),"coyote zero-gravity position");
        require(scalar.template operator()<std::uint32_t>(row,c,DetCharacterCoyoteRemaining)==N-std::uint32_t(row.nextTick-1),"coyote future-pre count");}
    const auto last=cp(cy);DeterministicSession cyLast({1,1,1,64,0x43594f54453535ull});setup(cyLast,c,true,false);require(cyLast.restore(last),"coyote branch restore");
    run(cyLast,{button(true)});auto row=cp(cyLast);require(row.actors.at(10).pose.position[1]==q(33,32).bits() && scalar.template operator()<V>(row,c,DetCharacterVelocity).y==D::fromInt(2),"coyote65535 final opportunity jumps");++total.templates;
    run(cy);run(cy,{button(true)});row=cp(cy);require(row.actors.at(10).pose.position[1]==D::fromInt(1).bits() && scalar.template operator()<V>(row,c,DetCharacterVelocity).y.isZero(),"coyote65536 first expired opportunity rejected");++total.templates;
    phase="long-buffer-last-expired";c=config();c.jumpBufferTicks=N;
    DeterministicSession buffer({1,1,1,64,0x4255464645523535ull});setup(buffer,c,false,false);
    while(buffer.nextTick()<N-1){run(buffer,buffer.nextTick()?std::vector<DetTickCommand>{}:std::vector<DetTickCommand>{button(true)});
        const auto value=cp(buffer);witnessExpected=value;writeDetState(detcharacter_advanced::stateSchema(c),witnessExpected->actors.at(10).blocks.at(c.stateSchema),DetCharacterJumpBufferRemaining,N-std::uint32_t(value.nextTick-1));
        require(scalar.template operator()<std::uint32_t>(value,c,DetCharacterJumpBufferRemaining)==N-std::uint32_t(value.nextTick-1),"buffer future-pre count");}
    const auto before=cp(buffer);DeterministicSession bLast({1,1,1,64,0x4255464645523535ull});setup(bLast,c,false,false);require(bLast.restore(before),"buffer branch restore");
    run(bLast,{{1,0,2,{}}});run(bLast);row=cp(bLast);require(row.actors.at(10).pose.position[1]==q(33,32).bits() && scalar.template operator()<V>(row,c,DetCharacterVelocity).y==D::fromInt(2),"buffer65535 future-pre last landing accepted");++total.templates;
    run(buffer);run(buffer,{{1,0,2,{}}});run(buffer);row=cp(buffer);require(row.actors.at(10).pose.position[1]==D::fromInt(1).bits() && scalar.template operator()<bool>(row,c,DetCharacterGrounded) && scalar.template operator()<std::uint32_t>(row,c,DetCharacterJumpBufferRemaining)==0,"buffer65536 expired held press cannot jump on landing");++total.templates;
    phase="long-drop-timer-geometric-retired";c=config();c.dropThroughTicks=N;
    DeterministicSession drop({1,1,1,64,0x44524f50543535ull});setup(drop,c,true,true);
    while(drop.nextTick()<N+1){if(drop.nextTick()==0)run(drop,{button(false,true)});else if(drop.nextTick()==1)run(drop,{{1,0,4,{}}});else run(drop);
        const auto value=cp(drop);const auto body=detcharacter_advanced::bodySchema(c);witnessExpected=value;
        const auto remaining=value.nextTick<N?N-std::uint32_t(value.nextTick):0;
        writeDetState(body,witnessExpected->actors.at(10).blocks.at(c.collision.bodySchema),DetBodyIgnoredOneWay,DetEntityRef{20});
        writeDetState(detcharacter_advanced::stateSchema(c),witnessExpected->actors.at(10).blocks.at(c.stateSchema),DetCharacterDropRemaining,remaining);
        writeDetState(detcharacter_advanced::stateSchema(c),witnessExpected->actors.at(10).blocks.at(c.stateSchema),DetCharacterDropTick,value.nextTick);
        witnessExpected->actors.at(10).pose.position[1]=D::fromInt(1).bits();witnessExpected->actors.at(20).pose.position[1]=q(1,4096).bits();
        require(readDetState<DetEntityRef>(body,value.actors.at(10).blocks.at(c.collision.bodySchema),DetBodyIgnoredOneWay).value==20,"drop retains ignored identity inside thickness");
        require(scalar.template operator()<std::uint32_t>(value,c,DetCharacterDropRemaining)==remaining && scalar.template operator()<std::uint64_t>(value,c,DetCharacterDropTick)==value.nextTick,"drop65535 future ticks and committed stamp");
        require(value.actors.at(10).pose.position[1]==D::fromInt(1).bits() && value.actors.at(20).pose.position[1]==q(1,4096).bits(),"drop parked overlapping top plane witness");}
    ++total.templates;const auto parked=cp(drop);DeterministicSession retired({1,1,1,64,0x44524f50543535ull});setup(retired,c,true,true);require(retired.restore(parked),"drop retired branch restore");
    for(unsigned i=1;i<=33;++i){run(drop,{button(false,true,2)});const auto value=cp(drop);
        const auto ignored=readDetState<DetEntityRef>(detcharacter_advanced::bodySchema(c),value.actors.at(10).blocks.at(c.collision.bodySchema),DetBodyIgnoredOneWay).value;
        require(value.actors.at(10).pose.position[0]==q(int(i),32).bits() && ignored==(i<=32?20u:0u),"drop releases after explicit full X clearance");}
    ++total.templates;run(retired,{{1,0,3,{}}});const auto fresh=cp(retired);require(retired.restore(fresh),"fresh retired drop checkpoint accepted");run(retired);row=cp(retired);
    require(!readDetState<DetEntityRef>(detcharacter_advanced::bodySchema(c),row.actors.at(10).blocks.at(c.collision.bodySchema),DetBodyIgnoredOneWay).value && scalar.template operator()<std::uint32_t>(row,c,DetCharacterDropRemaining)==0 && scalar.template operator()<std::uint64_t>(row,c,DetCharacterDropTick)==0,"retired target bypasses geometric wait on next pre");++total.templates;
    total.lastHash=detCheckpointHash(row);return total;
}
inline void expectedCrush(std::uint64_t seed){DetCollision2DConfig c;c.extended=c.oneWayPlatforms=true;c.maxBodies=3;c.maxTriggerPairs=1;c.maxContactIterations=8;
    const auto platform=mix(seed)&1?SimEntityId{5}:SimEntityId{20};
    for(bool ignore:{false,true}){DeterministicSession s({1,1,1,4,seed});if(!installDetCollision2D(s,c))throw CaseFailure(seed,0,1,"expected-crush","collision configuration");
        auto make=[&](DetBodyMode2D mode,int y,V velocity,bool oneWay){DetActorState a;DetSimTransformComponent pose;(void)pose.setPosition({D{},D::fromInt(y),D{}});a.pose=pose.snapshot();
            a.blocks[c.bodySchema]=detCollisionBodyState2D(c,{mode,{mode==DetBodyMode2D::Kinematic?q(1,2):D::fromInt(2),q(1,2)},{},velocity,mode==DetBodyMode2D::Kinematic?1u:2u,mode==DetBodyMode2D::Kinematic?2u:1u,false,oneWay,DetEntityRef{mode==DetBodyMode2D::Kinematic && ignore?platform:0}});return a;};
        if(!s.addEntity(10,make(DetBodyMode2D::Kinematic,1,{},false)) || !s.addEntity(platform,make(DetBodyMode2D::MovingObstacle,0,V::fromInts(0,1),true)) || !s.addEntity(30,make(DetBodyMode2D::Static,2,{},false)) || !s.seal())throw CaseFailure(seed,0,1,"expected-crush",s.error());
        const auto initial=*s.checkpoint();const auto success=s.advance({0,1,{}});
        if(ignore){if(!success || s.faulted() || s.checkpoint()->actors.at(10).pose.position[1]!=D::fromInt(1).bits())throw CaseFailure(seed,0,1,"expected-crush-ignore-control",s.error());}
        else if(success || !s.faulted() || !s.restore(initial) || encodeDetCheckpoint(*s.checkpoint())!=encodeDetCheckpoint(initial))throw CaseFailure(seed,0,1,"expected-crush","expected fault/explicit full restore did not hold");
    }
}
} // namespace detcharacter_campaign
