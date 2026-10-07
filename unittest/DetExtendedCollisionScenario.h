#pragma once
#include "DetCollisionScenario.h"
namespace detextendedcollision_scenario {
using namespace ayt::entity;
using detcollision_scenario::D;using detcollision_scenario::V;using detcollision_scenario::v;
inline DetTickInput input(std::uint64_t tick,bool reversed=false){return detcollision_scenario::input(tick,reversed);}
inline bool configure(DeterministicSession& s,bool reversed=false) {
    DetCollision2DConfig policy;policy.extended=true;policy.maxBodies=8;policy.maxTriggerPairs=1;
    if(!s.registerTypedSchema({6,1,{{10,std::uint64_t{0}},{20,std::uint64_t{0}},{30,std::uint64_t{0}},
        {40,std::uint64_t{0}},{50,std::uint64_t{0}},{60,std::uint64_t{0}}}}) || !s.registerGlobalState(6))return false;
    auto inputSystem=[policy](DetTickContext& c){
        if(c.input().commands.size()!=2 || c.input().commands[0].payload.size()!=1 || c.input().commands[0].payload[0]>1)return false;
        const int sign=c.input().commands[0].payload[0]?-1:1;
        auto reset=[&](SimEntityId id,V p,V velocity){c.write(id,policy.bodySchema,DetBodyVelocity,velocity);
            if(!c.pose(id).setPosition({p.x,p.y,{}}))throw std::runtime_error("fixture pose failure");};
        reset(10,v(-5*sign,0),v(512*sign,0));reset(20,v(5*sign,0),v(-512*sign,0));
        reset(30,v(-5*sign,20),v(512*sign,0));reset(40,v(0,20),{});
        reset(50,v(-5*sign,40),v(640*sign,0));return true;
    };
    auto events=[policy](DetTickContext& c){for(const auto& command:c.events()){
        DetTriggerEvent2D e;if(command.type!=policy.eventType || !decodeDetTriggerEvent2D(command.payload,e))return false;
        c.writeGlobal(6,10,c.readGlobal<std::uint64_t>(6,10)+1);auto h=c.readGlobal<std::uint64_t>(6,20);
        for(auto byte:command.payload)h=h*33+byte;c.writeGlobal(6,20,h);
        const auto field=30+10*static_cast<std::uint32_t>(e.phase);c.writeGlobal(6,field,c.readGlobal<std::uint64_t>(6,field)+1);}
        c.writeGlobal(6,30,c.tick()+1);return true;};
    if(reversed){if(!s.registerSystem(40,0,events) || !installDetCollision2D(s,policy) || !s.registerSystem(10,0,inputSystem))return false;}
    else if(!s.registerSystem(10,0,inputSystem) || !installDetCollision2D(s,policy) || !s.registerSystem(40,0,events))return false;
    auto actor=[&](DetBodyMode2D mode,V p,bool trigger=false){DetActorState a;DetSimTransformComponent pose;
        (void)pose.setPosition({p.x,p.y,{}});a.pose=pose.snapshot();a.blocks[policy.bodySchema]=detCollisionBodyState2D(policy,{mode,v(1,1),{},{},1,UINT32_MAX,trigger});return a;};
    std::vector<std::pair<SimEntityId,DetActorState>> actors={{10,actor(DetBodyMode2D::Kinematic,v(-5,0))},{20,actor(DetBodyMode2D::Kinematic,v(5,0))},
        {30,actor(DetBodyMode2D::MovingObstacle,v(-5,20))},{40,actor(DetBodyMode2D::Kinematic,v(0,20))},
        {50,actor(DetBodyMode2D::Kinematic,v(-5,40))},{60,actor(DetBodyMode2D::Static,v(0,40),true)}};
    if(reversed)std::reverse(actors.begin(),actors.end());for(auto& [id,a]:actors)if(!s.addEntity(id,a))return false;
    return s.seal();
}
inline bool matchesGolden(const DetSessionCheckpoint& cp) {
    if(cp.nextTick>10000)return false;
    if(cp.nextTick){const int sign=(cp.nextTick-1)%64>=32?-1:1;
        const std::pair<SimEntityId,V> expected[]={{10,v(-sign,0)},{20,v(sign,0)},{30,v(3*sign,20)},{40,v(5*sign,20)},{50,v(5*sign,40)},{60,v(0,40)}};
        for(const auto& [id,p]:expected){const auto& pose=cp.actors.at(id).pose;if(pose.position[0]!=p.x.bits() || pose.position[1]!=p.y.bits())return false;}
        DetTriggerEvent2D a,b;if(cp.pendingEvents.size()!=2 || !decodeDetTriggerEvent2D(cp.pendingEvents[0].payload,a) || !decodeDetTriggerEvent2D(cp.pendingEvents[1].payload,b)
            || a.phase!=DetTriggerPhase2D::Enter || b.phase!=DetTriggerPhase2D::Exit || a.pair!=DetCollisionPair2D{50,60} || b.pair!=a.pair)return false;
    }
    const auto delivered=cp.nextTick?cp.nextTick-1:0;std::uint64_t hash=0;
    // Exact integer byte oracle. Every preceding tick emits one Enter and Exit.
    static const auto hashes=[](){std::vector<std::uint64_t> rows{0};std::uint64_t h=0;
        for(unsigned i=0;i<10000;++i){for(unsigned phase:{1u,3u}){std::array<std::uint8_t,24> bytes{};bytes[0]=1;bytes[4]=phase;bytes[8]=50;bytes[16]=60;
                for(auto byte:bytes)h=h*33+byte;}rows.push_back(h);}return rows;}();
    hash=hashes[delivered];const std::vector<std::uint64_t> expectedStats={2*delivered,hash,cp.nextTick,delivered,0,delivered};
    return cp.globals.at(6)==expectedStats && cp.globals.at(5)[4]==0;
}
} // namespace detextendedcollision_scenario
