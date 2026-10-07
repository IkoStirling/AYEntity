#pragma once
#include <AYEntity/DeterministicCollision2D.h>
#include <algorithm>
namespace detcollision_scenario {
using namespace ayt::entity;
using D=ayt::math::DetFloat32;using V=ayt::math::DetVec2;
inline V v(int x,int y){return {D::fromInt(x),D::fromInt(y)};}
inline bool configure(DeterministicSession& s,bool reversed=false,bool extended=false) {
    DetCollision2DConfig policy;policy.maxBodies=8;policy.maxTriggerPairs=1;policy.extended=extended;
    if(!s.registerTypedSchema({6,1,{{10,std::uint64_t{0}},{20,std::uint64_t{0}},{30,std::uint64_t{0}},
        {40,std::uint64_t{0}},{50,std::uint64_t{0}},{60,std::uint64_t{0}}}}) || !s.registerGlobalState(6))return false;
    auto input=[policy](DetTickContext& c){if(c.input().commands.size()!=2)return false;
        const auto& command=c.input().commands.front();if(command.type!=1 || command.payload.size()!=1 || command.payload[0]>1)return false;
        const int sign=command.payload[0]? -1:1;c.write(2,policy.bodySchema,DetBodyVelocity,v(sign*64,sign*16));return true;};
    auto events=[policy](DetTickContext& c){
        for(const auto& command:c.events()){if(command.type!=policy.eventType)return false;DetTriggerEvent2D e;if(!decodeDetTriggerEvent2D(command.payload,e))return false;
            c.writeGlobal(6,10,c.readGlobal<std::uint64_t>(6,10)+1);
            auto hash=c.readGlobal<std::uint64_t>(6,20);for(auto byte:command.payload)hash=hash*33+byte;c.writeGlobal(6,20,hash);
            const auto field=30+10*static_cast<std::uint32_t>(e.phase);c.writeGlobal(6,field,c.readGlobal<std::uint64_t>(6,field)+1);}
        c.writeGlobal(6,30,c.tick()+1);return true;};
    if(reversed){if(!s.registerSystem(40,0,events) || !installDetCollision2D(s,policy) || !s.registerSystem(10,0,input))return false;}
    else if(!s.registerSystem(10,0,input) || !installDetCollision2D(s,policy) || !s.registerSystem(40,0,events))return false;
    const auto half=D::fromBits(0x3f000000u);
    auto actor=[&](DetBodyMode2D mode,V position,V extent,bool trigger=false){DetActorState a;DetSimTransformComponent pose;
        (void)pose.setPosition({position.x,position.y,{}});a.pose=pose.snapshot();a.blocks[policy.bodySchema]=detCollisionBodyState2D(policy,{mode,extent,{},{},1,UINT32_MAX,trigger});return a;};
    std::vector<std::pair<SimEntityId,DetActorState>> actors={{2,actor(DetBodyMode2D::Kinematic,{}, {half,half})},
        {50,actor(DetBodyMode2D::Static,{},v(1,3),true)},
        {100,actor(DetBodyMode2D::Static,v(4,0),v(1,100))},{101,actor(DetBodyMode2D::Static,v(-4,0),v(1,100))},
        {102,actor(DetBodyMode2D::Static,v(0,4),v(100,1))},{103,actor(DetBodyMode2D::Static,v(0,-4),v(100,1))}};
    if(reversed)std::reverse(actors.begin(),actors.end());for(auto& [id,a]:actors)if(!s.addEntity(id,a))return false;
    return s.seal();
}
inline DetTickInput input(std::uint64_t tick,bool reversed=false){
    DetTickInput packet{tick,1,{{1,0,1,{static_cast<std::uint8_t>(tick%64>=32)}},{2,0,2,{7}}}};
    if(reversed)std::reverse(packet.commands.begin(),packet.commands.end());return packet;
}
// Independent integer lattice reference ONLY in the test oracle. Engine motion
// uses software Float32. Half/quarter unit coordinates are exactly representable.
struct Golden {int x2=0,y4=0;std::array<std::uint64_t,6> stats{};unsigned pending=0;};
inline const std::vector<Golden>& golden(){
    static const auto rows=[](){std::vector<Golden> result(1);bool inside=false;
        for(unsigned tick=0;tick<10000;++tick){auto row=result.back();
            if(row.pending){++row.stats[0];++row.stats[2+row.pending];std::array<std::uint8_t,24> bytes{};bytes[0]=1;bytes[4]=static_cast<std::uint8_t>(row.pending);bytes[8]=2;bytes[16]=50;
                for(auto byte:bytes)row.stats[1]=row.stats[1]*33+byte;}
            const int sign=tick%64>=32?-1:1;row.x2=std::clamp(row.x2+sign*2,-5,5);row.y4=std::clamp(row.y4+sign,-10,10);
            const bool now=row.x2>-3 && row.x2<3;row.pending=now?(inside?2:1):(inside?3:0);inside=now;row.stats[2]=tick+1;result.push_back(row);
        }return result;}();return rows;
}
inline bool matchesGolden(const DetSessionCheckpoint& cp){
    if(cp.nextTick>10000)return false;const auto& row=golden()[cp.nextTick];const auto& p=cp.actors.at(2).pose.position;
    if(p[0]!=(D::fromInt(row.x2)/D::fromInt(2)).bits() || p[1]!=(D::fromInt(row.y4)/D::fromInt(4)).bits())return false;
    const auto& stats=cp.globals.at(6);if(stats.size()!=row.stats.size() || !std::equal(stats.begin(),stats.end(),row.stats.begin()))return false;
    if(cp.pendingEvents.size()!=(row.pending?1:0))return false;
    if(row.pending){const auto& e=cp.pendingEvents[0];std::array<std::uint8_t,24> bytes{};bytes[0]=1;bytes[4]=static_cast<std::uint8_t>(row.pending);bytes[8]=2;bytes[16]=50;
        if(e.source!=30 || e.sequence!=0 || e.type!=0x30001 || !std::equal(e.payload.begin(),e.payload.end(),bytes.begin(),bytes.end()))return false;}
    return true;
}
}
