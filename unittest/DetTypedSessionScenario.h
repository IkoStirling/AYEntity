#pragma once
#include "DetSessionScenario.h"
#include <AYEntity/DeterministicCollision2D.h>

namespace dettyped_scenario {
using namespace ayt::entity;
using D=ayt::math::DetFloat32;
using V=ayt::math::DetVec3;
using Q=ayt::math::DetQuaternion;
inline DetTypedStateSchema actorSchema() {
    return {2,1,{{10,std::uint64_t{0}},{20,std::uint64_t{0}},{30,D::fromBits(0x3f000000u)},
        {40,V{}},{50,Q{}},{60,false},{70,DetEntityRef{}},{80,ayt::math::DetVec2{}},{90,std::int32_t{0}}}};
}
inline bool configure(DeterministicSession& s,bool reversed=false,bool altered=false,bool collision=false,std::uint64_t alteredAt=0) {
    if(!s.registerTypedSchema(actorSchema()) || !s.registerTypedSchema({3,1,{{1,std::uint64_t{0}},{2,std::uint64_t{0}}}})
        || !s.registerGlobalState(3) || !s.registerRandomStream(7,42) || !s.registerRandomStream(8,0))return false;
    auto inputSystem=[](DetTickContext& c){
        c.writeGlobal(3,1,c.readGlobal<std::uint64_t>(3,1)+1);
        auto hash=c.readGlobal<std::uint64_t>(3,2);
        for(const auto& command:c.input().commands)for(auto byte:command.payload)hash=hash*33+byte;
        for(const auto& event:c.events())for(auto byte:event.payload)hash+=byte;
        c.writeGlobal(3,2,hash);
        const std::uint8_t value=static_cast<std::uint8_t>(c.tick());c.emit(1,{&value,1});
        if(c.tick()%997==0)c.spawn(1000+c.tick());
        if(c.tick()%997==1)c.despawn(1000+c.tick()-1);return true;
    };
    auto movement=[altered,collision,alteredAt](DetTickContext& c){
        for(auto id:c.entities()) {
            if(collision && (id==200 || id==201))continue; // Collision is their sole XY owner.
            c.write(id,2,10,c.read<std::uint64_t>(id,2,10)+ayt::math::random_uint(c.random(7),100));
            c.write(id,2,20,c.read<std::uint64_t>(id,2,20)+1+static_cast<unsigned>(altered && c.tick()>=alteredAt));
            const auto speed=c.read<D>(id,2,30);
            const V velocity{speed,D::fromInt(ayt::math::random_int(c.random(8),-2,2)),{}};
            c.write(id,2,40,velocity);
            auto& pose=c.pose(id);if(!pose.translate(c.read<V>(id,2,40)*c.dt()))return false;
            if(pose.rotationEnabled && !pose.integrateAngularVelocityWorld(V::fromInts(0,2,0),c.dt()))return false;
            c.write(id,2,50,Q::fromBits(pose.snapshot().rotation));
            c.write(id,2,60,c.tick()%2==0);c.write(id,2,70,DetEntityRef{2});
            c.write(id,2,80,ayt::math::DetVec2{velocity.x,velocity.y});
            c.write(id,2,90,static_cast<std::int32_t>(c.tick()));
        }
        if(c.tick()%11==0)c.emit(2);return true;
    };
    if(reversed) {
        if(!s.registerSystem(20,0,movement) || !s.registerSystem(10,0,inputSystem))return false;
    }else if(!s.registerSystem(10,0,inputSystem) || !s.registerSystem(20,0,movement))return false;
    if(collision) {
        DetCollision2DConfig policy;policy.extended=true;policy.maxBodies=4;policy.maxTriggerPairs=1;
        if(!installDetCollision2D(s,policy) || !s.registerSystem(25,0,[policy](DetTickContext& c){
            int sign=1;for(const auto& command:c.input().commands)
                if(command.source==1 && !command.payload.empty())sign=command.payload.front()%2?-1:1;
            c.write(200,policy.bodySchema,DetBodyVelocity,ayt::math::DetVec2::fromInts(640*sign,0));
            return c.pose(200).setPosition(V::fromInts(-5*sign,40,0));
        }))return false;
        DetActorState moving,trigger;DetSimTransformComponent pose;
        (void)pose.setPosition(V::fromInts(-5,40,0));moving.pose=pose.snapshot();
        (void)pose.setPosition(V::fromInts(0,40,0));trigger.pose=pose.snapshot();
        moving.blocks[4]=detCollisionBodyState2D(policy,{DetBodyMode2D::Kinematic,ayt::math::DetVec2::fromInts(1,1)});
        DetCollisionBody2D body;body.mode=DetBodyMode2D::Static;body.half=ayt::math::DetVec2::fromInts(1,1);body.trigger=true;
        trigger.blocks[4]=detCollisionBodyState2D(policy,body);
        if(!s.addEntity(200,moving) || !s.addEntity(201,trigger))return false;
    }
    DetActorState a;DetSimTransformComponent p;(void)p.setRotation({});a.pose=p.snapshot();
    // Initial typed state uses the same checked codec as callback access.
    a.blocks[2]=detStateDefaults(actorSchema());writeDetState(actorSchema(),a.blocks[2],70,DetEntityRef{100});
    if(reversed){if(!s.addEntity(100,a) || !s.addEntity(2,a))return false;}
    else if(!s.addEntity(2,a) || !s.addEntity(100,a))return false;return s.seal();
}
using detsession_scenario::input;
}
