#include <AYEntity/DeterministicRigidBody3D.h>
#include <AYEntity/DeterministicRollback.h>
#include <iostream>
using namespace ayt::entity;using D=ayt::math::DetFloat32;using V=ayt::math::DetVec3;using Q=ayt::math::DetQuaternion;
void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
D ratio(int a,int b){return D::fromInt(a)/D::fromInt(b);}D abs(D x){return x.signBit()?-x:x;}
DetRigidBody3D sphere(D radius,D mass){DetRigidBody3D b;b.shape=DetRigidShape3D::Sphere;b.half={radius,radius,radius};b.mass=mass;b.friction=D{};return b;}
V position(const DetSessionCheckpoint& cp,SimEntityId id){return V::fromBits(cp.actors.at(id).pose.position);}
V velocity(const DetSessionCheckpoint& cp,SimEntityId id,const DetRigidBody3DConfig& c){DetStateLayout l(detRigidBodySchema3D(c));return std::get<V>(l.read(cp.actors.at(id).blocks.at(c.bodySchema),5));}
void advance(DeterministicSession& s,unsigned count){for(unsigned i=0;i<count;++i){auto tick=s.nextTick();if(!s.advance({tick,1,{}}))throw std::runtime_error(s.error());}}
DetRigidBody3D fixed(V half){DetRigidBody3D b;b.mode=DetRigidMode3D::Static;b.mass=D{};b.half=half;return b;}
void elastic(){for(int massB:{1,3}){DetRigidBody3DConfig c;c.gravity={};c.substeps=1;DeterministicSession s({77,1,1,64,77});require(installDetRigidBody3D(s,c),"install elastic");
    auto a=sphere(ratio(1,2),D::fromInt(1)),b=sphere(ratio(1,2),D::fromInt(massB));a.velocity=V::fromInts(1,0,0);b.velocity=V::fromInts(-1,0,0);a.restitution=b.restitution=D::fromInt(1);
    require(s.addEntity(1,detRigidBodyActor3D(c,a,{}))&&s.addEntity(2,detRigidBodyActor3D(c,b,{ratio(3,4),D{},D{}}))&&s.seal(),s.error().c_str());advance(s,1);auto cp=*s.checkpoint();
    require(velocity(cp,1,c).x==D::fromInt(massB==1?-1:-2)&&velocity(cp,2,c).x==D::fromInt(massB==1?1:0),"independent 1D elastic mass impulse");}}
void forceAndRotation(){DetRigidBody3DConfig c;c.gravity={};c.substeps=1;DeterministicSession s({77,1,1,64,78});require(installDetRigidBody3D(s,c),"install torque");
    DetRigidBody3D b;b.mass=D::fromInt(2);require(s.addEntity(10,detRigidBodyActor3D(c,b)),"force body");
    require(s.registerSystem(1,0,[c](DetTickContext& ctx){detRigidBodyForce3D(ctx,c,10,V::fromInts(1,0,0),V::fromInts(0,0,1));return true;})&&s.seal(),"force system");advance(s,1);auto cp=*s.checkpoint();DetStateLayout l(detRigidBodySchema3D(c));auto& words=cp.actors.at(10).blocks.at(c.bodySchema);
    require(velocity(cp,10,c).x==ratio(1,128),"F/m software hand velocity");require(std::get<V>(l.read(words,6)).z>D{}&&Q::fromBits(cp.actors.at(10).pose.rotation).z>D{},"torque rotation authority");
    require(std::get<V>(l.read(words,7))==V{}&&std::get<V>(l.read(words,8))==V{},"force cleared");}
void orientedGeometry(){DetRigidBody3DConfig c;c.gravity={};auto q=Q::fromAxisAngle(V::fromInts(0,0,1),D::fromBits(0x3f490fdbu));require(bool(q),"rotation authoring");auto r=*ayt::math::detRotationMatrix(*q);auto normal=V{r(0,1),r(1,1),r(2,1)};
    auto rod=fixed({D::fromInt(1),ratio(1,10),ratio(1,10)});DeterministicSession separated({77,1,1,64,84});require(installDetRigidBody3D(separated,c)&&separated.addEntity(1,detRigidBodyActor3D(c,rod,{},*q))&&separated.addEntity(2,detRigidBodyActor3D(c,rod,{ratio(-35,100),ratio(35,100),D{}},*q))&&separated.seal(),"oriented rods");
    require(detRigidContacts3D(*separated.checkpoint(),c).empty(),"SAT rejects overlapping AABB separated OBBs");
    auto q3=Q::fromAxisAngle(V::fromInts(1,2,3),ratio(1,2));require(bool(q3),"three-axis orientation");auto r3=*ayt::math::detRotationMatrix(*q3);normal={r3(0,1),r3(1,1),r3(2,1)};require(abs(normal.z)>ratio(1,100),"actual out-of-XY orientation");
    DetRigidBody3D ball=sphere(ratio(1,4),D{});ball.mode=DetRigidMode3D::Static;DeterministicSession touching({77,1,1,64,85});require(installDetRigidBody3D(touching,c)&&touching.addEntity(1,detRigidBodyActor3D(c,rod,{},*q3))&&touching.addEntity(2,detRigidBodyActor3D(c,ball,normal*ratio(3,10)))&&touching.seal(),"oriented sphere box");
    auto ct=detRigidContacts3D(*touching.checkpoint(),c);require(ct.size()==1&&ct[0].normal.dot(normal)>ratio(999,1000)&&abs(ct[0].penetration-ratio(5,100))<ratio(1,1000),"true rotated box-sphere normal/depth");
    c.substeps=1;DeterministicSession hit({77,1,1,64,86});require(installDetRigidBody3D(hit,c),"off-center install");DetRigidBody3D box;box.half={ratio(1,2),ratio(1,2),ratio(1,2)};box.friction=D{};auto sphereBody=sphere(ratio(1,2),D::fromInt(1));sphereBody.velocity=V::fromInts(0,-2,0);
    require(hit.addEntity(1,detRigidBodyActor3D(c,box))&&hit.addEntity(2,detRigidBodyActor3D(c,sphereBody,{ratio(4,10),ratio(8,10),D{}}))&&hit.seal(),"off-center bodies");advance(hit,1);DetStateLayout layout(detRigidBodySchema3D(c));require(std::get<V>(layout.read(hit.checkpoint()->actors.at(1).blocks.at(c.bodySchema),6)).z<D{},"contact impulse induces actual angular velocity");}
void stackAndFriction(){DetRigidBody3DConfig c;DeterministicSession s({77,1,1,64,79});require(installDetRigidBody3D(s,c),"install stack");auto floor=fixed(V::fromInts(5,1,5));
    DetRigidBody3D box;box.half={ratio(1,2),ratio(1,2),ratio(1,2)};
    require(s.addEntity(1,detRigidBodyActor3D(c,floor,V::fromInts(0,-1,0)))&&s.addEntity(2,detRigidBodyActor3D(c,box,{D{},ratio(1,2),D{}}))&&s.seal(),s.error().c_str());advance(s,64);auto cp=*s.checkpoint();
    require(position(cp,2).y>ratio(48,100)&&position(cp,2).y<ratio(55,100)&&velocity(cp,2,c).length()<ratio(1,2),"box face manifold support");
    auto contacts=detRigidContacts3D(cp,c);require(!contacts.empty()&&contacts.size()<=4,"face manifold bounded points");
    DeterministicSession sliding({77,1,1,64,80});require(installDetRigidBody3D(sliding,c),"sliding install");auto ball=sphere(ratio(1,2),D::fromInt(1));ball.friction=D::fromInt(1);ball.velocity=V::fromInts(2,0,0);floor.friction=D::fromInt(1);
    require(sliding.addEntity(1,detRigidBodyActor3D(c,floor,V::fromInts(0,-1,0)))&&sliding.addEntity(2,detRigidBodyActor3D(c,ball,{D{},ratio(1,2),D{}}))&&sliding.seal(),"slide seal");advance(sliding,32);cp=*sliding.checkpoint();DetStateLayout l(detRigidBodySchema3D(c));
    require(velocity(cp,2,c).x<D::fromInt(2)&&std::get<V>(l.read(cp.actors.at(2).blocks.at(c.bodySchema),6)).z<D{},"friction linear/angular coupling");}
void joints(){for(auto kind:{DetJointKind3D::Distance,DetJointKind3D::BallSocket,DetJointKind3D::Fixed}){DetRigidBody3DConfig c;c.gravity={};DetRigidJoint3D j;j.id=1;j.kind=kind;j.first=1;j.second=2;
    if(kind==DetJointKind3D::Distance)j.restLength=D::fromInt(1);else j.anchorFirst=V::fromInts(0,-1,0);c.joints={j};
    DeterministicSession s({77,1,1,64,81});require(installDetRigidBody3D(s,c),"joint install");auto anchor=fixed(V::fromInts(1,1,1));anchor.mask=0;DetRigidBody3D body;body.mask=0;
    require(s.addEntity(1,detRigidBodyActor3D(c,anchor,V::fromInts(0,2,0)))&&s.addEntity(2,detRigidBodyActor3D(c,body,V::fromInts(0,kind==DetJointKind3D::Distance?0:1,0))),"joint actors");
    if(kind==DetJointKind3D::Fixed)require(s.registerSystem(1,0,[c](DetTickContext& ctx){detRigidBodyForce3D(ctx,c,2,{},V::fromInts(0,0,1));return true;}),"fixed torque");
    require(s.seal(),s.error().c_str());advance(s,32);auto cp=*s.checkpoint();auto delta=position(cp,2)-position(cp,1);
    require(kind==DetJointKind3D::Distance?abs(delta.length()-D::fromInt(1))<ratio(1,100):(delta-V::fromInts(0,-1,0)).length()<ratio(1,100),"joint anchor/distance constraint");
    if(kind==DetJointKind3D::Fixed)require(abs(Q::fromBits(cp.actors.at(2).pose.rotation).z)<ratio(1,100),"fixed rotation constraint");}}
bool setup(DeterministicSession& s,DetRigidBody3DConfig c,bool reverse=false){if(!installDetRigidBody3D(s,c))return false;
    auto moving=sphere(ratio(1,4),D{});moving.mode=DetRigidMode3D::Kinematic;moving.velocity=V::fromInts(4,0,0);auto sensor=fixed({ratio(1,2),ratio(1,2),ratio(1,2)});sensor.trigger=true;
    auto a=detRigidBodyActor3D(c,moving,{ratio(-5,4),D{},D{}}),b=detRigidBodyActor3D(c,sensor);if(reverse){if(!s.addEntity(20,b)||!s.addEntity(10,a))return false;}else if(!s.addEntity(10,a)||!s.addEntity(20,b))return false;
    return s.seal();}
void triggerRestore(){DetRigidBody3DConfig c;c.gravity={};DetSessionConfig config{77,1,1,64,82};DeterministicSession a(config),b(config);require(setup(a,c)&&setup(b,c,true),"trigger setup");std::optional<DetSessionCheckpoint> middle;unsigned enters=0,exits=0;
    for(unsigned tick=0;tick<40;++tick){advance(a,1);advance(b,1);auto cp=*a.checkpoint();require(encodeDetCheckpoint(cp)==encodeDetCheckpoint(*b.checkpoint()),"ordered full CP");for(auto& event:cp.pendingEvents){DetRigidTrigger3D e{};require(decodeDetRigidTrigger3D(event.payload,e),"trigger codec");enters+=e.phase==DetRigidTriggerPhase3D::Enter;exits+=e.phase==DetRigidTriggerPhase3D::Exit;}if(tick==15)middle=cp;}
    auto final=encodeDetCheckpoint(*a.checkpoint());require(enters==1&&exits==1,"actual trigger edges");require(a.restore(*middle),"3D restore");advance(a,24);require(encodeDetCheckpoint(*a.checkpoint())==final,"trigger restore suffix");auto bad=*a.checkpoint();bad.actors.at(10).blocks.at(c.bodySchema)[0]=99;require(!a.restore(bad)&&encodeDetCheckpoint(*a.checkpoint())==final,"rigid semantic restore atomicity");}
void rollback(){DetRigidBody3DConfig c;c.gravity={};DetSessionConfig config{77,1,1,64,83};auto configure=[c](DeterministicSession& s){auto body=sphere(ratio(1,4),D::fromInt(1));body.mask=0;
    return installDetRigidBody3D(s,c)&&s.addEntity(10,detRigidBodyActor3D(c,body))&&s.registerSystem(1,0,[c](DetTickContext& ctx){for(auto cmd:ctx.input().commands){if(cmd.type!=77||cmd.payload.size()!=1)return false;detRigidBodyForce3D(ctx,c,10,V::fromInts(cmd.payload[0]?1:0,0,0));}return true;})&&s.seal();};
    DeterministicSession truth(config),predicted(config);require(configure(truth)&&configure(predicted),"rollback setup");DetRollbackConfig rc;rc.members={1,2};rc.prediction={{77,DetPredictionMode::Hold}};DeterministicRollback r(predicted,rc);
    auto input=[](unsigned tick){return DetTickInput{tick,1,{{2,0,77,{static_cast<std::uint8_t>(tick==3)}}}};};
    for(unsigned tick=0;tick<8;++tick){require(truth.advance(input(tick)),"truth input");require(r.submit(1,{tick,1,{}}),"local actual");if(tick!=3 && !r.submit(2,input(tick)))throw std::runtime_error("remote tick "+std::to_string(tick)+": "+r.error());if(!r.advance())throw std::runtime_error("rollback advance: "+r.error());if(tick==5 && !r.submit(2,input(3)))throw std::runtime_error("late actual: "+r.error());}
    require(r.rollbackCount()>0&&encodeDetCheckpoint(*truth.checkpoint())==encodeDetCheckpoint(*predicted.checkpoint()),"actual late-input 3D rollback equality");}
int main(){try{elastic();forceAndRotation();orientedGeometry();stackAndFriction();joints();triggerRestore();rollback();std::cout<<"PASS rigid3D: elastic mass oracle, torque, rotated OBB SAT and sphere contact, off-center spin, clipped box manifold, friction spin, distance/ball/fixed joints, canonical triggers, full CP restore and actual late-input rollback\n";}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
