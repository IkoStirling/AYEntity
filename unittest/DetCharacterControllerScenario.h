#pragma once
// design reference: AYEntity/design.md Stage21; deterministic controller and platform carry.
// Shared owned-kernel/standard-Host fixture. Oracle is a dyadic lattice and an
// explicitly constructed trigger payload, never a copy of controller/solver code.
#include <AYEntity/DeterministicCharacterController2D.h>
#include <algorithm>
#include <array>
#include <stdexcept>

namespace detcharacter_scenario {
using namespace ayt::entity;
using D=ayt::math::DetFloat32;
using V=ayt::math::DetVec2;
inline constexpr SimEntityId characterId=10,platformId=20,triggerId=30;
inline constexpr std::uint32_t statsSchema=8,inputType=1,markerType=2;
inline constexpr std::uint64_t maxTicks=2048;
inline D ratio(int numerator,int denominator){return D::fromInt(numerator)/D::fromInt(denominator);}
inline V v(int x,int y){return V::fromInts(x,y);}
inline DetCharacterController2DConfig policy(){
    DetCharacterController2DConfig c;
    c.maxHorizontalSpeed=D::fromInt(2);
    c.acceleration=c.deceleration=D::fromInt(128);
    c.gravity=D::fromInt(4);c.jumpSpeed=D::fromInt(2);
    c.collision.maxBodies=8;c.collision.maxTriggerPairs=4;
    return c;
}
// Public field IDs/types: read the checkpoint through the typed codec, including
// persistent input and zeroed internal intent lanes. No production private layout.
inline DetTypedStateSchema stateSchema(const DetCharacterController2DConfig& c){
    return {c.stateSchema,1,{{DetCharacterEnabled,false},{DetCharacterGrounded,false},
        {DetCharacterSupport,DetEntityRef{}},{DetCharacterVelocity,V{}},
        {DetCharacterSupportVelocity,V{}},{DetCharacterTargetSpeed,D{}},
        {DetCharacterJumpRequested,false},{DetCharacterAppliedCarry,V{}},
        {DetCharacterIntentVelocity,V{}},{DetCharacterJumped,false},
        {DetCharacterSnapped,false},{DetCharacterSupportTick,std::uint64_t{0}}}};
}
inline DetTypedStateSchema bodySchema(const DetCharacterController2DConfig& c){
    return {c.collision.bodySchema,2,{{DetBodyMode,std::uint32_t{0}},
        {DetBodyHalf,V{}},{DetBodyOffset,V{}},{DetBodyVelocity,V{}},
        {DetBodyLayer,std::uint32_t{1}},{DetBodyMask,std::uint32_t{UINT32_MAX}},
        {DetBodyTrigger,false}}};
}
inline DetActorState actor(const DetCharacterController2DConfig& c,DetBodyMode2D mode,
        V position,V half,V velocity={},std::uint32_t layer=1,
        std::uint32_t mask=UINT32_MAX,bool trigger=false,bool character=false){
    DetActorState a;DetSimTransformComponent pose;
    if(!pose.setPosition({position.x,position.y,{}}))throw std::runtime_error("fixture pose");
    a.pose=pose.snapshot();
    a.blocks[c.collision.bodySchema]=detCollisionBodyState2D(c.collision,{mode,half,{},velocity,layer,mask,trigger});
    if(character){DetCharacterState2D state;state.enabled=true;
        a.blocks[c.stateSchema]=detCharacterState2D(c,state);}
    return a;
}
inline bool configure(DeterministicSession& s,bool reverse=false){
    const auto c=policy();
    if(!s.registerTypedSchema({statsSchema,1,{{10,std::uint64_t{0}},
        {20,std::uint64_t{0}},{30,std::uint64_t{0}},{40,std::uint64_t{0}},
        {50,std::uint64_t{0}},{60,std::uint64_t{0}}}})
        || !s.registerGlobalState(statsSchema))return false;
    auto inputSystem=[c](DetTickContext& context){
        // Rollback may omit/hold a contribution; persistent target semantics then
        // apply. Source2 is a distinct real member, not an external mutable flag.
        for(const auto& command:context.input().commands){
            if(command.source==1 && command.type==inputType){
                if(command.payload.size()!=1 || command.payload[0]>1)return false;
                detCharacterInput2D(context,characterId,c,{D::fromInt(command.payload[0]?-1:1),false});
            }else if(command.source!=2 || command.type!=markerType
                    || command.payload!=std::vector<std::uint8_t>{7})return false;
        }
        return true;
    };
    auto events=[c](DetTickContext& context){
        for(const auto& command:context.events()){
            DetTriggerEvent2D event;
            if(command.type!=c.collision.eventType || !decodeDetTriggerEvent2D(command.payload,event)
                || event.pair!=DetCollisionPair2D{characterId,triggerId})return false;
            context.writeGlobal(statsSchema,10,context.readGlobal<std::uint64_t>(statsSchema,10)+1);
            auto hash=context.readGlobal<std::uint64_t>(statsSchema,20);
            for(auto byte:command.payload)hash=hash*33+byte;
            context.writeGlobal(statsSchema,20,hash);
            const auto field=30+10*static_cast<std::uint32_t>(event.phase);
            context.writeGlobal(statsSchema,field,context.readGlobal<std::uint64_t>(statsSchema,field)+1);
        }
        context.writeGlobal(statsSchema,30,context.tick()+1);return true;
    };
    if(reverse){
        if(!s.registerSystem(50,0,events) || !installDetCharacterController2D(s,c)
            || !s.registerSystem(10,0,inputSystem))return false;
    }else if(!s.registerSystem(10,0,inputSystem) || !installDetCharacterController2D(s,c)
        || !s.registerSystem(50,0,events))return false;
    const auto half=ratio(1,2);
    std::vector<std::pair<SimEntityId,DetActorState>> actors={
        {characterId,actor(c,DetBodyMode2D::Kinematic,v(0,1),{half,half},{},1,6,false,true)},
        {platformId,actor(c,DetBodyMode2D::MovingObstacle,{}, {D::fromInt(16),half}, {half,{}},2,1)},
        {triggerId,actor(c,DetBodyMode2D::Static,v(2,1),{ratio(1,4),ratio(1,4)},{},4,1,true)}};
    if(reverse)std::reverse(actors.begin(),actors.end());
    for(auto& [id,state]:actors)if(!s.addEntity(id,std::move(state)))return false;
    return s.seal();
}
inline DetTickInput input(std::uint64_t tick,bool reverse=false){
    DetTickInput in{tick,1,{{1,0,inputType,{static_cast<std::uint8_t>(tick%128>=64)}},
        {2,0,markerType,{7}}}};
    if(reverse)std::reverse(in.commands.begin(),in.commands.end());return in;
}
inline std::array<std::uint8_t,24> eventBytes(unsigned phase){
    std::array<std::uint8_t,24> bytes{};
    bytes[0]=1;bytes[4]=static_cast<std::uint8_t>(phase);
    bytes[8]=static_cast<std::uint8_t>(characterId);
    bytes[16]=static_cast<std::uint8_t>(triggerId);return bytes;
}
struct Golden {int x128=0;std::array<std::uint64_t,6> stats{};unsigned pending=0;};
inline const std::vector<Golden>& golden(){
    static const auto rows=[](){
        std::vector<Golden> result(1);bool inside=false;
        for(std::uint64_t n=1;n<=maxTicks;++n){
            auto row=result.back();
            if(row.pending){++row.stats[0];++row.stats[2+row.pending];
                for(auto byte:eventBytes(row.pending))row.stats[1]=row.stats[1]*33+byte;}
            // Carrier contributes n/128. Own motion is a triangular walk with
            // exactly 64 positive and 64 negative steps of length1/64 each.
            const auto phase=static_cast<int>(n%128);
            const int triangle=phase<=64?phase:128-phase;
            row.x128=static_cast<int>(n)+2*triangle;row.stats[2]=n;
            // Strict positive trigger interior: |centerX-2| < .5+.25.
            const bool now=row.x128>160 && row.x128<352;
            row.pending=now?(inside?2u:1u):(inside?3u:0u);
            inside=now;result.push_back(row);
        }
        return result;
    }();return rows;
}
inline bool matchesGolden(const DetSessionCheckpoint& cp){
    if(cp.nextTick>maxTicks || cp.actors.size()!=3 || !cp.retiredIds.empty())return false;
    const auto c=policy();const auto n=cp.nextTick;const auto& row=golden()[n];
    const auto& p=cp.actors.at(characterId).pose.position;
    const auto& carrier=cp.actors.at(platformId).pose.position;
    if(p[0]!=ratio(row.x128,128).bits() || p[1]!=D::fromInt(1).bits()
        || carrier[0]!=ratio(static_cast<int>(n),128).bits() || carrier[1]!=D{}.bits())return false;
    const auto schema=stateSchema(c);const auto& words=cp.actors.at(characterId).blocks.at(c.stateSchema);
    if(!readDetState<bool>(schema,words,DetCharacterEnabled)
        || readDetState<bool>(schema,words,DetCharacterGrounded)!=(n!=0)
        || readDetState<DetEntityRef>(schema,words,DetCharacterSupport).value!=(n?platformId:0)
        || readDetState<std::uint64_t>(schema,words,DetCharacterSupportTick)!=n)return false;
    const auto sign=n?((n-1)%128>=64?-1:1):0;
    const auto own=readDetState<V>(schema,words,DetCharacterVelocity);
    const auto support=readDetState<V>(schema,words,DetCharacterSupportVelocity);
    if(own.x!=D::fromInt(sign) || own.y!=D{} || support.x!=(n?ratio(1,2):D{})
        || support.y!=D{} || readDetState<D>(schema,words,DetCharacterTargetSpeed)!=D::fromInt(sign))return false;
    for(auto field:{DetCharacterJumpRequested,DetCharacterJumped,DetCharacterSnapped})
        if(readDetState<bool>(schema,words,field))return false;
    for(auto field:{DetCharacterAppliedCarry,DetCharacterIntentVelocity}){
        const auto zero=readDetState<V>(schema,words,field);if(zero.x!=D{} || zero.y!=D{})return false;}
    if(cp.globals.at(statsSchema)!=std::vector<std::uint64_t>(row.stats.begin(),row.stats.end())
        || cp.pendingEvents.size()!=(row.pending?1u:0u))return false;
    if(row.pending){const auto& e=cp.pendingEvents[0];const auto expected=eventBytes(row.pending);
        if(e.source!=c.collision.systemId || e.sequence!=0 || e.type!=c.collision.eventType
            || !std::equal(e.payload.begin(),e.payload.end(),expected.begin(),expected.end()))return false;}
    return true;
}
} // namespace detcharacter_scenario
