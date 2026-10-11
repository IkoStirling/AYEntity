#include <AYEntity/DeterministicCharacterController2D.h>
#include <AYEntity/DeterministicCollisionQuery2D.h>
#include <AYEntity/DeterministicPathMotion2D.h>
#include <AYMath/DetSlopeGeometry2D.h>
#include <algorithm>
#include <iostream>
#include <stdexcept>

using namespace ayt::entity;
using D=ayt::math::DetFloat32;using V=ayt::math::DetVec2;using Bytes=std::vector<std::uint8_t>;
namespace {
void require(bool value,const std::string& why){if(!value)throw std::runtime_error(why);}
D q(int n,int denominator=4){return D::fromInt(n)/D::fromInt(denominator);}
DetCharacterController2DConfig policy(){DetCharacterController2DConfig c;c.collision.staticSlopes=true;c.collision.maxTriggerPairs=58;
    c.maxHorizontalSpeed=D::fromInt(4);c.acceleration=c.deceleration=D::fromInt(64);c.gravity=D::fromInt(4);
    c.jumpSpeed=D::fromInt(4);return c;}
DetActorState actor(const DetCharacterController2DConfig& c,DetBodyMode2D mode,V position,V half,V velocity={},bool character=false,
    DetCollisionShape2D shape=DetCollisionShape2D::Aabb){DetActorState a;DetSimTransformComponent p;require(p.setPosition({position.x,position.y,{}}),"pose");a.pose=p.snapshot();
    DetCollisionBody2D body{mode,half,{},velocity};body.shape=shape;a.blocks[c.collision.bodySchema]=detCollisionBodyState2D(c.collision,body);
    if(character){DetCharacterState2D state;state.enabled=true;a.blocks[c.stateSchema]=detCharacterState2D(c,state);}return a;}
DetSessionCheckpoint cp(DeterministicSession& s){auto result=s.checkpoint();require(bool(result),s.error());return *result;}
Bytes bytes(DeterministicSession& s){return encodeDetCheckpoint(cp(s));}
DetTypedStateSchema stateLayout(const DetCharacterController2DConfig& c){
    DetTypedStateSchema s{c.stateSchema,kDetCharacterController2DSlopeProfileVersion,{
        {DetCharacterEnabled,false},{DetCharacterGrounded,false},{DetCharacterSupport,DetEntityRef{}},{DetCharacterVelocity,V{}},
        {DetCharacterSupportVelocity,V{}},{DetCharacterTargetSpeed,D{}},{DetCharacterJumpRequested,false},{DetCharacterAppliedCarry,V{}},
        {DetCharacterIntentVelocity,V{}},{DetCharacterJumped,false},{DetCharacterSnapped,false},{DetCharacterSupportTick,std::uint64_t{}},
        {DetCharacterGroundNormal,V{}}}};return s;}
template<class T>T state(DeterministicSession& s,const DetCharacterController2DConfig& c,std::uint32_t field){
    const auto checkpoint=cp(s);return readDetState<T>(stateLayout(c),checkpoint.actors.at(10).blocks.at(c.stateSchema),field);}
bool configure(DeterministicSession& s,const DetCharacterController2DConfig& c,bool left=false,bool reverse=false,bool highFlat=false){
    auto input=[c](DetTickContext& ctx){for(const auto& command:ctx.input().commands){if(command.payload.size()!=2)return false;
        const int speed=command.payload[0]<128?command.payload[0]:int(command.payload[0])-256;
        detCharacterInput2D(ctx,10,c,{D::fromInt(speed),bool(command.payload[1])});}return true;};
    if(reverse){if(!installDetCharacterController2D(s,c) || !s.registerSystem(10,0,input))return false;}
    else if(!s.registerSystem(10,0,input) || !installDetCharacterController2D(s,c))return false;
    auto hero=actor(c,DetBodyMode2D::Kinematic,{D::fromInt(left?3:1),q(7,8)},{q(1),q(1)},{},true);
    auto wedge=actor(c,DetBodyMode2D::Static,{D::fromInt(2),D::fromInt(1)},{D::fromInt(2),D::fromInt(1)},{},false,
        left?DetCollisionShape2D::SlopeRisesLeft:DetCollisionShape2D::SlopeRisesRight);
    if(reverse){if(!s.addEntity(20,wedge) || !s.addEntity(10,hero))return false;}
    else if(!s.addEntity(10,hero) || !s.addEntity(20,wedge))return false;
    if(highFlat && !s.addEntity(30,actor(c,DetBodyMode2D::Static,{D::fromInt(left?-2:6),q(6)},{D::fromInt(2),q(2)})))return false;
    return s.seal();
}
bool configureHighEnd(DeterministicSession& s,const DetCharacterController2DConfig& c,bool left){
    auto input=[c](DetTickContext& ctx){for(const auto& command:ctx.input().commands){if(command.payload.size()!=2)return false;
        const int halfSpeed=command.payload[0]<128?command.payload[0]:int(command.payload[0])-256;
        detCharacterInput2D(ctx,10,c,{D::fromInt(halfSpeed)/D::fromInt(2),bool(command.payload[1])});}return true;};
    if(!s.registerSystem(10,0,input) || !installDetCharacterController2D(s,c))return false;
    return s.addEntity(10,actor(c,DetBodyMode2D::Kinematic,{D::fromInt(left?0:4),q(9)},{q(1),q(1)},{},true))
        && s.addEntity(20,actor(c,DetBodyMode2D::Static,{D::fromInt(2),D::fromInt(1)},{D::fromInt(2),D::fromInt(1)},{},false,
            left?DetCollisionShape2D::SlopeRisesLeft:DetCollisionShape2D::SlopeRisesRight)) && s.seal();
}
DetTickInput frame(std::uint64_t tick,int speed,bool jump=false){return {tick,1,{{1,0,1,{std::uint8_t(speed),std::uint8_t(jump)}}}};}
void advance(DeterministicSession& s,int speed,bool jump=false){require(s.advance(frame(s.nextTick(),speed,jump)),s.error());}
void expect(DeterministicSession& s,const DetCharacterController2DConfig& c,V position,V normal,bool grounded,const std::string& why){
    const auto p=cp(s).actors.at(10).pose.position;
    require(p[0]==position.x.bits() && p[1]==position.y.bits(),why+" actual="+std::to_string(p[0])+","+std::to_string(p[1]));
    require(state<bool>(s,c,DetCharacterGrounded)==grounded && state<V>(s,c,DetCharacterGroundNormal)==normal,why+" ground normal");
    require(state<DetEntityRef>(s,c,DetCharacterSupport).value==(grounded?20u:0u),why+" support");
}
void walkChecks(){const auto c=policy();
    for(bool left:{false,true}){DeterministicSession a({1,1,1,4,29}),b({1,1,1,4,29});require(configure(a,c,left) && configure(b,c,left,true),"slope seal");
        const V n{D::fromInt(left?2:-2),D::fromInt(4)};std::vector<DetSessionCheckpoint> checkpoints{cp(a)};
        for(unsigned t=0;t<4;++t){advance(a,left?-2:2);advance(b,left?-2:2);require(bytes(a)==bytes(b),"full bytes reverse registration slope walk");
            expect(a,c,{q(left?12-int(t+1)*2:4+int(t+1)*2),q(7+int(t+1)*2,8)},n,true,"dyadic uphill");checkpoints.push_back(cp(a));}
        require(a.restore(checkpoints[2]),"mid-slope restore");for(unsigned t=2;t<4;++t){advance(a,left?-2:2);require(bytes(a)==encodeDetCheckpoint(checkpoints[t+1]),"slope restore continuation full bytes");}
        for(unsigned t=0;t<4;++t){advance(a,left?2:-2);expect(a,c,{q(left?4+int(t+1)*2:12-int(t+1)*2),q(15-int(t+1)*2,8)},n,true,"dyadic downhill adhesion");}
        advance(a,0,true);expect(a,c,{D::fromInt(left?3:1),q(15,8)},V{},false,"vertical jump releases slope");
    }
    for(bool left:{false,true}){DeterministicSession s({1,1,1,4,29});require(configure(s,c,left,false,true),s.error());
        for(unsigned t=0;t<7;++t)advance(s,left?-2:2);
        const auto checkpoint=cp(s);require(checkpoint.actors.at(10).pose.position[0]==q(left?-2:18).bits()
            && checkpoint.actors.at(10).pose.position[1]==q(9).bits(),"slope high endpoint enters adjacent flat without launch actual="
                +std::to_string(checkpoint.actors.at(10).pose.position[0])+","+std::to_string(checkpoint.actors.at(10).pose.position[1]));
        require(state<bool>(s,c,DetCharacterGrounded) && state<V>(s,c,DetCharacterGroundNormal)==V{D{},D::fromInt(1)}
            && state<DetEntityRef>(s,c,DetCharacterSupport).value==30,"real flat support after high-end transition");
        for(unsigned t=0;t<7;++t)advance(s,left?2:-2);
        expect(s,c,{D::fromInt(left?3:1),q(7,8)},{D::fromInt(left?2:-2),D::fromInt(4)},true,"high flat returns continuously to descending slope");}
    for(bool left:{false,true}){DeterministicSession s({1,1,1,4,29});require(configureHighEnd(s,c,left),s.error());
        const V normal{D::fromInt(left?2:-2),D::fromInt(4)};
        advance(s,left?1:-1);
        expect(s,c,{q(left?1:31,8),q(9)},normal,true,"standalone high-end flat keeps support during slow reversal");
        const auto highEnd=cp(s);advance(s,0,true);
        expect(s,c,{q(left?1:31,8),q(13)},V{},false,"standalone high-end flat permits immediate jump");
        require(s.restore(highEnd) && bytes(s)==encodeDetCheckpoint(highEnd),"standalone high-end support restore");
        for(unsigned t=0;t<3;++t)advance(s,left?1:-1);
        expect(s,c,{q(left?4:28,8),q(17,8)},normal,true,"standalone high-end flat returns continuously downhill");
    }
    std::cout<<"PASS left/right hand-computed ascent/descent, high-end flat transitions both ways, standalone top support/reverse/jump/restore, normals, complete checkpoint order/restore\n";
}
void queryChecks(){const auto c=policy();DeterministicSession s({1,1,1,4,29});require(configure(s,c),s.error());const auto checkpoint=cp(s);
    DetCollisionQuery2D query(checkpoint,c.collision);DetCollisionQueryFilter2D filter;filter.ignore=10;
    const auto empty=ayt::math::DetAabb2::fromCenterHalf({q(1),q(7)},{q(1,16),q(1,16)});
    require(query.overlap(empty,filter).empty(),"triangle empty upper bounding-box corner has no overlap");
    const auto filled=ayt::math::DetAabb2::fromCenterHalf({q(13),q(1)},{q(1,16),q(1,16)});
    require(query.overlap(filled,filter)==std::vector<SimEntityId>{20},"solid wedge lower interior overlaps");
    auto ray=query.raycast({D::fromInt(1),D::fromInt(3)},{D{},-D::fromInt(4)},filter);
    require(ray.size()==1 && ray.front().entity==20 && ray.front().hit.fraction.bits()==q(5,8).bits() && ray.front().hit.normal==V{-D::fromInt(2),D::fromInt(4)},"ray exact slope surface");
    auto invalid=checkpoint;const auto layout=stateLayout(c);writeDetState(layout,invalid.actors.at(10).blocks.at(c.stateSchema),DetCharacterGroundNormal,V{D{},D::fromInt(1)});
    const auto before=bytes(s);require(!s.restore(invalid) && bytes(s)==before,"airborne malformed ground normal atomically rejected");
    auto shapeInvalid=checkpoint;writeDetState(detCollisionBodySchema2D(c.collision),shapeInvalid.actors.at(10).blocks.at(c.collision.bodySchema),DetBodyShape,std::uint32_t{1});
    require(!s.restore(shapeInvalid) && bytes(s)==before,"moving/character slope shape rejected");
    std::cout<<"PASS real wedge query empty/solid corners, slope ray and invalid restore\n";
}
void steepChecks(){auto c=policy();c.maxSlopeRisePerRun=q(1);c.gravity={};
    DeterministicSession s({1,1,1,4,29});require(configure(s,c),s.error());advance(s,2);
    const auto result=cp(s);const auto p=result.actors.at(10).pose.position;
    require(p[0]==D::fromInt(1).bits() && p[1]==q(7,8).bits(),"above-limit uphill input is blocked without climbing");
    require(!state<bool>(s,c,DetCharacterGrounded) && state<V>(s,c,DetCharacterGroundNormal)==V{},"steep face is not walkable support");
    std::cout<<"PASS explicit walkable slope limit and uphill blocking\n";
}
void carryStopChecks(){auto c=policy();c.collision.motionPolicy=DetCollisionMotionPolicy2D::StopPrescribedOnConflict;c.gravity={};
    DetPathMotion2DConfig path;path.collision=c.collision;path.tickPeriod=q(1);path.waypoints={{{},1,0},{{D{},D::fromInt(1)},1,0}};
    DeterministicSession s({1,1,1,4,29});require(installDetCharacterController2D(s,c) && installDetPathMotion2D(s,path),"combined facade/path installation");
    auto hero=actor(c,DetBodyMode2D::Kinematic,{D{},D::fromInt(1)},{q(2),q(2)},{},true);
    auto elevator=actor(c,DetBodyMode2D::MovingObstacle,{},{D::fromInt(2),q(2)});
    DetPathMotionState2D bound;bound.bound=true;elevator.blocks[path.stateSchema]=detPathMotionState2D(path,bound);
    auto independent=actor(c,DetBodyMode2D::MovingObstacle,{D::fromInt(10),D{}},{q(2),q(2)});
    bound.origin={D::fromInt(10),D{}};independent.blocks[path.stateSchema]=detPathMotionState2D(path,bound);
    require(s.addEntity(10,hero) && s.addEntity(20,elevator)
        && s.addEntity(30,actor(c,DetBodyMode2D::Static,{D{},q(10)},{D::fromInt(2),q(2)}))
        && s.addEntity(40,independent) && s.seal(),s.error());
    require(s.advance({0,1,{}}),s.error());const auto checkpoint=cp(s);const auto layout=detCollisionBodySchema2D(c.collision);
    require(checkpoint.actors.at(20).pose.position[1]==0 && checkpoint.actors.at(10).pose.position[1]==D::fromInt(1).bits(),"rewound platform and continuous carry stop together");
    require(checkpoint.actors.at(40).pose.position[1]==D::fromInt(1).bits(),"independent platform completes its intended tick");
    require(readDetState<std::uint32_t>(layout,checkpoint.actors.at(20).blocks.at(c.collision.bodySchema),DetBodyMotionResult)
        ==static_cast<std::uint32_t>(DetCollisionMotionResult2D::CrushPrevented),"real classified crush result");
    require(state<bool>(s,c,DetCharacterGrounded) && state<DetEntityRef>(s,c,DetCharacterSupport).value==20
        && state<V>(s,c,DetCharacterSupportVelocity)==V{} && state<V>(s,c,DetCharacterVelocity)==V{},"post observes effective stopped carry, no reverse own velocity");
    // Path scalar lanes are independently read from the fixed public schema.
    DetTypedStateSchema phase{path.stateSchema,1,{{10,false},{20,false},{30,false},{40,false},{50,V{}},{60,std::uint32_t{}},
        {70,std::int32_t{1}},{80,std::uint32_t{}},{90,std::uint32_t{}},{100,V{}},{110,false},{120,false},{130,V{}},{140,false}}};
    require(readDetState<bool>(phase,checkpoint.actors.at(20).blocks.at(path.stateSchema),DetPathBlocked)
        && readDetState<std::uint32_t>(phase,checkpoint.actors.at(20).blocks.at(path.stateSchema),DetPathElapsedTicks)==0
        && readDetState<std::uint32_t>(phase,checkpoint.actors.at(40).blocks.at(path.stateSchema),DetPathWaypoint)==1,"path phase follows accepted motion for both movers");
    require(s.restore(checkpoint) && bytes(s)==encodeDetCheckpoint(checkpoint),"blocked carrier/path full checkpoint restores");
    std::cout<<"PASS combined path, directly proved crush recovery, carry cancellation, independent mover and phase restore\n";
}
void resultPolicyChecks(){const auto c=policy();DeterministicSession s({1,1,1,4,29});
    require(installDetCollision2D(s,c.collision)
        && s.addEntity(10,actor(c,DetBodyMode2D::Kinematic,{},{q(1),q(1)}))
        && s.addEntity(20,actor(c,DetBodyMode2D::MovingObstacle,{D::fromInt(4),D{}},{q(1),q(1)}))
        && s.addEntity(30,actor(c,DetBodyMode2D::Static,{D::fromInt(8),D{}},{q(1),q(1)})) && s.seal(),"result policy fixture seal");
    require(s.advance({0,1,{}}),s.error());const auto original=cp(s);const auto before=bytes(s);const auto layout=detCollisionBodySchema2D(c.collision);
    auto invalid=original;auto& body=invalid.actors.at(10).blocks.at(c.collision.bodySchema);
    writeDetState(layout,body,DetBodyMotionResult,static_cast<std::uint32_t>(DetCollisionMotionResult2D::CrushPrevented));
    writeDetState(layout,body,DetBodyMotionOther,DetEntityRef{20});writeDetState(layout,body,DetBodyMotionResultTick,invalid.nextTick);
    require(!s.restore(invalid) && bytes(s)==before,"Fault policy cannot forge a recovered crush output");
    invalid=original;auto& other=invalid.actors.at(10).blocks.at(c.collision.bodySchema);
    writeDetState(layout,other,DetBodyMotionResult,static_cast<std::uint32_t>(DetCollisionMotionResult2D::Blocked));
    writeDetState(layout,other,DetBodyMotionOther,DetEntityRef{30});writeDetState(layout,other,DetBodyMotionAxis,std::uint32_t{2});
    writeDetState(layout,other,DetBodyMotionResultTick,invalid.nextTick);
    require(!s.restore(invalid) && bytes(s)==before,"diagonal result cannot reference an AABB partner");
    invalid=original;auto& prescribed=invalid.actors.at(20).blocks.at(c.collision.bodySchema);
    writeDetState(layout,prescribed,DetBodyMotionResult,static_cast<std::uint32_t>(DetCollisionMotionResult2D::Blocked));
    writeDetState(layout,prescribed,DetBodyMotionOther,DetEntityRef{30});writeDetState(layout,prescribed,DetBodyMotionResultTick,invalid.nextTick);
    require(!s.restore(invalid) && bytes(s)==before,"Fault policy cannot forge prescribed mover recovery");
    std::cout<<"PASS motion result policy and actual diagonal partner restore semantics\n";
}
} // namespace
int main(){try{walkChecks();queryChecks();steepChecks();carryStopChecks();resultPolicyChecks();std::cout<<"PASS Stage29 necessary slope checks\n";return 0;}
    catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
