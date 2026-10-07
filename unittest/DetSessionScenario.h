#pragma once
#include <AYEntity/DeterministicSession.h>
#include <algorithm>

namespace detsession_scenario {
using namespace ayt::entity;
using D=ayt::math::DetFloat32;
using V=ayt::math::DetVec3;
inline bool configure(DeterministicSession& s,bool reversed=false,bool altered=false) {
    if(!s.registerSchema({2,1,{10,20,30}},{0,0,0x3f000000u}) || !s.registerSchema({3,1,{1,2}})
        || !s.registerGlobalState(3) || !s.registerRandomStream(7,42) || !s.registerRandomStream(8,0))return false;
    auto inputSystem=[](DetTickContext& c){
        auto g=c.globals(3);++g[0];
        for(const auto& command:c.input().commands)for(auto byte:command.payload)g[1]=g[1]*33+byte;
        for(const auto& event:c.events())for(auto byte:event.payload)g[1]+=byte;
        const std::uint8_t value=static_cast<std::uint8_t>(c.tick());c.emit(1,{&value,1});
        if(c.tick()%997==0)c.spawn(1000+c.tick());
        if(c.tick()%997==1)c.despawn(1000+c.tick()-1);
        return true;
    };
    auto movement=[altered](DetTickContext& c){
        for(auto id:c.entities()) {
            auto words=c.words(id,2);words[0]+=ayt::math::random_uint(c.random(7),100);
            words[1]+=1+static_cast<unsigned>(altered);
            const auto speed=D::fromBits(static_cast<std::uint32_t>(words[2]));
            auto& pose=c.pose(id);
            if(!pose.translate(V{speed,D::fromInt(ayt::math::random_int(c.random(8),-2,2)),{}}*c.dt()))return false;
            if(pose.rotationEnabled && !pose.integrateAngularVelocityWorld(V::fromInts(0,2,0),c.dt()))return false;
        }
        if(c.tick()%11==0)c.emit(2);
        return true;
    };
    if(reversed) {
        if(!s.registerSystem(20,0,movement) || !s.registerSystem(10,0,inputSystem))return false;
    }else if(!s.registerSystem(10,0,inputSystem) || !s.registerSystem(20,0,movement))return false;
    DetActorState a;DetSimTransformComponent p;(void)p.setRotation({});a.pose=p.snapshot();
    if(reversed) {if(!s.addEntity(100,a) || !s.addEntity(2,a))return false;}
    else if(!s.addEntity(2,a) || !s.addEntity(100,a))return false;
    return s.seal();
}
inline DetTickInput input(std::uint64_t tick,bool reversed=false) {
    DetTickInput packet{tick,1,{{2,0,1,{static_cast<std::uint8_t>(tick%7)}},{1,1,1,{3}},{1,0,1,{5}}}};
    if(reversed)std::reverse(packet.commands.begin(),packet.commands.end());return packet;
}
} // namespace detsession_scenario
