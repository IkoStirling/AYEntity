#include <AYEntity/DeterministicCollision2D.h>
#include <AYEntity.h>
#include <AYTest.h>
#include <algorithm>
namespace {
using namespace ayt::entity;
using D=ayt::math::DetFloat32;using V=ayt::math::DetVec2;
V v(int x,int y){return {D::fromInt(x),D::fromInt(y)};}
struct Fixture {
    World& world=World::instance();std::unique_ptr<DeterministicSession> session;
    Fixture(){world.initialize();DetSessionConfig cfg;cfg.stepDenominator=1;session=std::make_unique<DeterministicSession>(world,cfg);}
    ~Fixture(){session.reset();world.shutdown();}
};
DetActorState actor(const DetCollision2DConfig& c,DetBodyMode2D mode,V p,V half=v(1,1),V velocity={},bool trigger=false,V offset={}){
    DetActorState a;DetSimTransformComponent pose;(void)pose.setPosition({p.x,p.y,D::fromInt(7)});a.pose=pose.snapshot();
    a.blocks[c.bodySchema]=detCollisionBodyState2D(c,{mode,half,offset,velocity,1,UINT32_MAX,trigger});return a;
}
template<class F>bool rejects(F f){try{f();return false;}catch(const std::exception&){return true;}}
V position(const DetSessionCheckpoint& cp,SimEntityId id){const auto& p=cp.actors.at(id).pose;return V::fromBits({p.position[0],p.position[1]});}
DetTriggerPhase2D phase(const DetSessionCheckpoint& cp){DetTriggerEvent2D e;if(cp.pendingEvents.size()!=1 || !decodeDetTriggerEvent2D(cp.pendingEvents[0].payload,e))throw std::runtime_error("bad event");return e.phase;}
}
TEST_SUITE(DeterministicCollision2D)
TEST_CASE(pair_generation_stable_order_masks_and_touching){
    const ayt::math::DetAabb2 box{v(0,0),v(2,2)};
    std::vector<DetCollisionProxy2D> p={{9,box,1,3},{2,box,2,1},{7,{v(2,0),v(3,2)},1,3},{4,box,4,4}};
    const auto expected=std::vector<DetCollisionPair2D>{{2,9}};CHECK(detCollisionPairs2D(p)==expected);
    std::reverse(p.begin(),p.end());CHECK(detCollisionPairs2D(p)==expected);p[0].id=p[1].id;CHECK(rejects([&]{(void)detCollisionPairs2D(p);}));
}
TEST_CASE(event_explicit_little_endian_and_malformed_decode_atomic){
    DetTriggerEvent2D e{DetTriggerPhase2D::Exit,{2,UINT64_MAX}},decoded=e;auto bytes=encodeDetTriggerEvent2D(e);
    CHECK(bytes.size()==24);CHECK(bytes[0]==1 && bytes[4]==3 && bytes[8]==2 && bytes[16]==255);CHECK(decodeDetTriggerEvent2D(bytes,decoded));CHECK(decoded==e);
    for(unsigned i=0;i<5;++i){auto bad=bytes;if(i==0)bad.pop_back();if(i==1)bad[0]=2;if(i==2)bad[4]=4;if(i==3)bad[8]=0;
        if(i==4)std::fill(bad.begin()+16,bad.end(),0);decoded=e;CHECK(!decodeDetTriggerEvent2D(bad,decoded));CHECK(decoded==e);}
    CHECK(rejects([]{(void)encodeDetTriggerEvent2D({DetTriggerPhase2D::Enter,{3,2}});}));
}
TEST_CASE(broadphase_matches_independent_integer_bruteforce_across_permutations){
    std::uint32_t seed=1471;
    for(unsigned scene=0;scene<128;++scene){
        struct Rect {int x,y,w,h;std::uint32_t layer,mask;};std::vector<Rect> boxes;std::vector<DetCollisionProxy2D> proxies;
        auto next=[&]{seed=seed*1664525u+1013904223u;return seed;};
        for(unsigned i=0;i<12;++i){const int x=int(next()%17)-8,y=int(next()%17)-8,w=int(next()%4)+1,h=int(next()%4)+1;
            const auto layer=1u<<(next()%3),mask=next()%8;boxes.push_back({x,y,w,h,layer,mask});proxies.push_back({i+1,{v(x,y),v(x+w,y+h)},layer,mask});}
        std::vector<DetCollisionPair2D> reference;
        for(unsigned i=0;i<boxes.size();++i)for(unsigned j=i+1;j<boxes.size();++j){const auto& a=boxes[i];const auto& b=boxes[j];
            if((a.mask&b.layer) && (b.mask&a.layer) && a.x<b.x+b.w && a.x+a.w>b.x && a.y<b.y+b.h && a.y+a.h>b.y)reference.push_back({i+1,j+1});}
        CHECK(detCollisionPairs2D(proxies)==reference);std::reverse(proxies.begin(),proxies.end());CHECK(detCollisionPairs2D(proxies)==reference);
    }
}
TEST_CASE(subnormal_motion_signed_zero_body_limits_and_kinematic_nonblocking){
    Fixture f;DetCollision2DConfig c;c.maxBodies=2;c.maxTriggerPairs=1;auto& s=*f.session;CHECK(installDetCollision2D(s,c));
    CHECK(s.addEntity(2,actor(c,DetBodyMode2D::Kinematic,{},v(1,1),{D::fromBits(1),D::fromBits(0x80000000u)})));
    CHECK(s.addEntity(3,actor(c,DetBodyMode2D::Kinematic,{},v(1,1))));CHECK(s.seal());CHECK(s.advance({0,1,{}}));
    CHECK(s.checkpoint()->actors.at(2).pose.position[0]==1);CHECK(s.checkpoint()->actors.at(2).blocks.at(c.bodySchema)[6]==0x80000000u);
    CHECK(s.advance({1,1,{}}));CHECK(s.checkpoint()->actors.at(2).pose.position[0]==2);
}
TEST_CASE(active_body_cap_rejects_before_adapter_motion){
    Fixture f;DetCollision2DConfig c;c.maxBodies=1;auto& s=*f.session;CHECK(installDetCollision2D(s,c));
    CHECK(s.addEntity(2,actor(c,DetBodyMode2D::Kinematic,{},v(1,1),v(1,0))));CHECK(s.addEntity(3,actor(c,DetBodyMode2D::Static,v(10,0))));
    CHECK(s.seal());auto saved=*s.checkpoint();CHECK(!s.advance({0,1,{}}));CHECK(s.faulted());CHECK(s.restore(saved));CHECK(!firstDetDifference(saved,*s.checkpoint()));
}
TEST_CASE(policy_and_body_validation){
    Fixture f;DetCollision2DConfig c;c.maxTriggerPairs=62;CHECK(!installDetCollision2D(*f.session,c));c.maxTriggerPairs=61;CHECK(installDetCollision2D(*f.session,c));
    CHECK(f.session->addEntity(1));CHECK(f.session->seal());CHECK(f.session->advance({0,1,{}}));
    CHECK(rejects([&]{(void)detCollisionBodyState2D(c,{DetBodyMode2D::Static,v(1,1),{},v(1,0)});}));
    CHECK(rejects([&]{(void)detCollisionBodyState2D(c,{DetBodyMode2D::Kinematic,{}});}));
}
TEST_CASE(high_speed_block_and_slide_keeps_z_and_zeroes_only_normal_velocity){
    Fixture f;DetCollision2DConfig c;auto& s=*f.session;CHECK(installDetCollision2D(s,c));
    CHECK(s.addEntity(100,actor(c,DetBodyMode2D::Static,v(5,0),v(1,100))));
    CHECK(s.addEntity(2,actor(c,DetBodyMode2D::Kinematic,{},v(1,1),v(100,3))));CHECK(s.seal());CHECK(s.advance({0,1,{}}));
    auto cp=*s.checkpoint();CHECK(position(cp,2)==v(3,3));CHECK(cp.actors.at(2).pose.position[2]==D::fromInt(7).bits());
    auto& lanes=cp.actors.at(2).blocks.at(c.bodySchema);CHECK(lanes[5]==0 && lanes[6]==D::fromInt(3).bits());
    CHECK(s.advance({1,1,{}}));CHECK(position(*s.checkpoint(),2)==v(3,6));
}
TEST_CASE(equal_time_corner_negative_direction_and_restore_no_hidden_cache){
    for(bool reversed:{false,true}){Fixture f;DetCollision2DConfig c;auto& s=*f.session;CHECK(installDetCollision2D(s,c));
        const auto wallX=actor(c,DetBodyMode2D::Static,v(-5,0),v(1,100)),wallY=actor(c,DetBodyMode2D::Static,v(0,-5),v(100,1));
        CHECK(s.addEntity(reversed?90:80,reversed?wallY:wallX));CHECK(s.addEntity(reversed?80:90,reversed?wallX:wallY));
        CHECK(s.addEntity(2,actor(c,DetBodyMode2D::Kinematic,{},v(1,1),v(-100,-100))));CHECK(s.seal());auto saved=*s.checkpoint();
        CHECK(s.advance({0,1,{}}));auto cp=*s.checkpoint();CHECK(position(cp,2)==v(-3,-3));CHECK(s.restore(saved));CHECK(s.advance({0,1,{}}));CHECK(!firstDetDifference(cp,*s.checkpoint()));}
}
TEST_CASE(offset_rounding_contact_does_not_penetrate){
    Fixture f;DetCollision2DConfig c;auto& s=*f.session;CHECK(installDetCollision2D(s,c));
    const auto tenth=D::fromBits(0x3dcccccdu),third=D::fromBits(0x3eaaaaabu);
    CHECK(s.addEntity(100,actor(c,DetBodyMode2D::Static,{third,D{}},{tenth,D::fromInt(10)})));
    CHECK(s.addEntity(2,actor(c,DetBodyMode2D::Kinematic,v(-2,0),{tenth,tenth},v(10,0),false,{tenth,{}})));
    CHECK(s.seal());CHECK(s.advance({0,1,{}}));const auto p=position(*s.checkpoint(),2);
    const auto moving=ayt::math::DetAabb2::fromCenterHalf(p+V{tenth,{}},{tenth,tenth});
    const auto wall=ayt::math::DetAabb2::fromCenterHalf({third,{}},{tenth,D::fromInt(10)});CHECK(!moving.interiorOverlap(wall));
}
TEST_CASE(trigger_enter_stay_exit_next_tick_and_history_restore){
    Fixture f;DetCollision2DConfig c;auto& s=*f.session;CHECK(installDetCollision2D(s,c));
    CHECK(s.registerSystem(10,0,[c](DetTickContext& ctx){if(ctx.tick()==2)ctx.write(2,c.bodySchema,DetBodyMode,std::uint32_t{0});return true;}));
    CHECK(s.addEntity(100,actor(c,DetBodyMode2D::Static,{},v(2,2),{},true)));
    CHECK(s.addEntity(2,actor(c,DetBodyMode2D::Kinematic,{},v(1,1))));CHECK(s.seal());
    CHECK(s.advance({0,1,{}}));CHECK(phase(*s.checkpoint())==DetTriggerPhase2D::Enter);auto saved=*s.checkpoint();
    CHECK(s.advance({1,1,{}}));CHECK(phase(*s.checkpoint())==DetTriggerPhase2D::Stay);auto stay=*s.checkpoint();
    CHECK(s.restore(saved));CHECK(s.advance({1,1,{}}));CHECK(!firstDetDifference(stay,*s.checkpoint()));
    CHECK(s.advance({2,1,{}}));CHECK(phase(*s.checkpoint())==DetTriggerPhase2D::Exit);CHECK(s.advance({3,1,{}}));CHECK(s.checkpoint()->pendingEvents.empty());
}
TEST_CASE(despawned_partner_exits_on_next_evaluation_and_trigger_crossing_is_excluded){
    for(bool crossing:{false,true}){Fixture f;DetCollision2DConfig c;auto& s=*f.session;CHECK(installDetCollision2D(s,c));
        CHECK(s.registerSystem(50,0,[crossing](DetTickContext& ctx){if(!crossing && ctx.tick()==0)ctx.despawn(100);return true;}));
        CHECK(s.addEntity(100,actor(c,DetBodyMode2D::Static,{},v(1,1),{},true)));
        CHECK(s.addEntity(2,actor(c,DetBodyMode2D::Kinematic,crossing?v(-10,0):V{},v(1,1),crossing?v(20,0):V{})));
        CHECK(s.seal());CHECK(s.advance({0,1,{}}));if(crossing)CHECK(s.checkpoint()->pendingEvents.empty());else{
            CHECK(phase(*s.checkpoint())==DetTriggerPhase2D::Enter);CHECK(s.advance({1,1,{}}));CHECK(phase(*s.checkpoint())==DetTriggerPhase2D::Exit);}}
}
TEST_CASE(initial_penetration_and_capacity_fault_require_explicit_restore){
    for(bool capacity:{false,true}){Fixture f;DetCollision2DConfig c;c.maxTriggerPairs=1;auto& s=*f.session;CHECK(installDetCollision2D(s,c));
        CHECK(s.addEntity(100,actor(c,DetBodyMode2D::Static,{},v(2,2),{},capacity)));
        if(capacity)CHECK(s.addEntity(101,actor(c,DetBodyMode2D::Static,{},v(2,2),{},true)));
        CHECK(s.addEntity(2,actor(c,DetBodyMode2D::Kinematic,{},v(1,1),v(1,0))));CHECK(s.seal());const auto saved=*s.checkpoint();
        CHECK(!s.advance({0,1,{}}));CHECK(s.faulted());CHECK(!s.checkpoint());CHECK(s.restore(saved));CHECK(!firstDetDifference(saved,*s.checkpoint()));}
}
TEST_CASE(noncanonical_history_and_changed_immutable_policy_fault_at_semantic_validation){
    for(bool changed:{false,true}){Fixture f;DetCollision2DConfig c;auto& s=*f.session;CHECK(installDetCollision2D(s,c));CHECK(s.addEntity(2));CHECK(s.seal());auto saved=*s.checkpoint(),bad=saved;
        // Six policy/count lanes followed by typed EntityRef slots.
        bad.globals.at(c.historySchema)[changed?1:6]=999;CHECK(s.restore(bad));CHECK(!s.advance({0,1,{}}));CHECK(s.restore(saved));CHECK(s.advance({0,1,{}}));}
}

TEST_CASE(extended_relative_sweep_mutual_kinematics_exact_dyadic_oracle){
    for(int velocity=0;velocity<=16;++velocity){Fixture f;DetCollision2DConfig c;c.extended=true;c.maxBodies=2;auto& s=*f.session;
        CHECK(installDetCollision2D(s,c));
        CHECK(s.addEntity(20,actor(c,DetBodyMode2D::Kinematic,v(5,0),v(1,1),v(velocity-16,0))));
        CHECK(s.addEntity(10,actor(c,DetBodyMode2D::Kinematic,v(-5,0),v(1,1),v(velocity,0))));
        CHECK(s.seal());CHECK(s.advance({0,1,{}}));auto cp=*s.checkpoint();
        // Initial center gap 10, surface gap 8, relative speed 16 -> t=1/2.
        // Equal normal velocities after contact: velocity-8; final centers +/-1.
        CHECK(position(cp,10)==v(velocity-9,0));CHECK(position(cp,20)==v(velocity-7,0));
        CHECK(cp.actors.at(10).blocks.at(c.bodySchema)[5]==D::fromInt(velocity-8).bits());
        CHECK(cp.actors.at(20).blocks.at(c.bodySchema)[5]==D::fromInt(velocity-8).bits());
    }
}
TEST_CASE(extended_moving_obstacle_pushes_stationary_kinematic_without_tangent_friction){
    Fixture f;DetCollision2DConfig c;c.extended=true;auto& s=*f.session;CHECK(installDetCollision2D(s,c));
    CHECK(s.addEntity(10,actor(c,DetBodyMode2D::MovingObstacle,v(-5,0),v(1,10),v(8,0))));
    CHECK(s.addEntity(20,actor(c,DetBodyMode2D::Kinematic,v(0,0),v(1,1),v(0,3))));
    CHECK(s.seal());CHECK(s.advance({0,1,{}}));auto cp=*s.checkpoint();
    CHECK(position(cp,10)==v(3,0));CHECK(position(cp,20)==v(5,3));CHECK(cp.actors.at(20).blocks.at(c.bodySchema)[5]==D::fromInt(8).bits());
    CHECK(cp.actors.at(20).pose.position[2]==D::fromInt(7).bits());
}
TEST_CASE(extended_transient_trigger_crossing_emits_enter_then_exit_without_history){
    Fixture f;DetCollision2DConfig c;c.extended=true;auto& s=*f.session;CHECK(installDetCollision2D(s,c));
    CHECK(s.addEntity(10,actor(c,DetBodyMode2D::Kinematic,v(-5,0),v(1,1),v(10,0))));
    CHECK(s.addEntity(20,actor(c,DetBodyMode2D::Static,{},v(1,1),{},true)));
    CHECK(s.seal());const auto initial=*s.checkpoint();CHECK(s.advance({0,1,{}}));const auto final=*s.checkpoint();
    CHECK(position(final,10)==v(5,0));CHECK(final.pendingEvents.size()==2);CHECK(final.globals.at(c.historySchema)[4]==0);
    DetTriggerEvent2D a,b;CHECK(decodeDetTriggerEvent2D(final.pendingEvents[0].payload,a));CHECK(decodeDetTriggerEvent2D(final.pendingEvents[1].payload,b));
    CHECK(a.phase==DetTriggerPhase2D::Enter && b.phase==DetTriggerPhase2D::Exit && a.pair==b.pair);
    CHECK(s.restore(initial));CHECK(s.advance({0,1,{}}));CHECK(!firstDetDifference(final,*s.checkpoint()));
}
TEST_CASE(extended_trigger_crossing_uses_piecewise_solved_path_not_endpoint_chord){
    Fixture f;DetCollision2DConfig c;c.extended=true;auto& s=*f.session;CHECK(installDetCollision2D(s,c));
    CHECK(s.addEntity(10,actor(c,DetBodyMode2D::Kinematic,v(-5,-5),v(1,1),v(10,10))));
    CHECK(s.addEntity(20,actor(c,DetBodyMode2D::Static,v(0,0),v(1,100))));
    const auto quarter=D::fromBits(0x3e800000u);
    CHECK(s.addEntity(30,actor(c,DetBodyMode2D::Static,v(-4,0),{quarter,quarter},{},true)));
    CHECK(s.seal());CHECK(s.advance({0,1,{}}));CHECK(position(*s.checkpoint(),10)==v(-2,5));CHECK(s.checkpoint()->pendingEvents.empty());
}
TEST_CASE(extended_body_policy_history_and_event_semantics_reject_restore_atomically){
    Fixture f;DetCollision2DConfig c;c.extended=true;c.maxTriggerPairs=1;auto& s=*f.session;CHECK(installDetCollision2D(s,c));
    CHECK(s.addEntity(10,actor(c,DetBodyMode2D::Kinematic,{})));CHECK(s.seal());const auto saved=*s.checkpoint();auto* pointer=s.presentationEntity(10);
    for(unsigned test=0;test<7;++test){auto bad=saved;
        if(test==0)bad.actors.at(10).blocks.at(c.bodySchema)[1]=0;
        if(test==1)bad.actors.at(10).blocks.at(c.bodySchema)[0]=4;
        if(test==2)bad.globals.at(c.historySchema)[0]=1;
        if(test==3)bad.globals.at(c.historySchema)[4]=2;
        if(test==4)bad.globals.at(c.historySchema)[8]=11;
        if(test==5)bad.pendingEvents.push_back({c.systemId,0,c.eventType,{1}});
        if(test==6){bad.globals.at(c.historySchema)[4]=1;bad.globals.at(c.historySchema)[8]=99;bad.globals.at(c.historySchema)[9]=100;}
        CHECK(!s.restore(bad));CHECK(s.presentationEntity(10)==pointer);CHECK(!firstDetDifference(saved,*s.checkpoint()));}
}
TEST_CASE(extended_capacity_penetration_and_contact_exhaustion_are_explicit){
    {Fixture f;DetCollision2DConfig c;c.extended=true;c.maxBodies=1;auto& s=*f.session;CHECK(installDetCollision2D(s,c));
        CHECK(s.addEntity(10,actor(c,DetBodyMode2D::Kinematic,{})));CHECK(s.addEntity(20,actor(c,DetBodyMode2D::Kinematic,v(5,0))));CHECK(!s.seal());}
    {Fixture f;DetCollision2DConfig c;c.extended=true;auto& s=*f.session;CHECK(installDetCollision2D(s,c));
        CHECK(s.addEntity(10,actor(c,DetBodyMode2D::Kinematic,{})));CHECK(s.addEntity(20,actor(c,DetBodyMode2D::Kinematic,{})));CHECK(!s.seal());}
    {Fixture f;DetCollision2DConfig c;c.extended=true;c.maxContactIterations=1;auto& s=*f.session;CHECK(installDetCollision2D(s,c));
        CHECK(s.addEntity(10,actor(c,DetBodyMode2D::Kinematic,v(-5,-5),v(1,1),v(10,10))));
        CHECK(s.addEntity(20,actor(c,DetBodyMode2D::Static,{},v(1,100))));CHECK(s.addEntity(30,actor(c,DetBodyMode2D::Static,v(-5,5),v(100,1))));
        CHECK(s.seal());auto saved=*s.checkpoint();CHECK(!s.advance({0,1,{}}));CHECK(s.faulted());CHECK(s.restore(saved));}
}
TEST_CASE(extended_motion_policy_is_manifest_distinct_and_legacy_disallows_moving_obstacle){
    Fixture f;DetCollision2DConfig c;c.extended=true;c.maxTriggerPairs=61;CHECK(!installDetCollision2D(*f.session,c));
    c.maxTriggerPairs=60;CHECK(installDetCollision2D(*f.session,c));CHECK(f.session->seal());CHECK(f.session->manifest()[4]==3);
    c.extended=false;CHECK(rejects([&]{(void)detCollisionBodyState2D(c,{DetBodyMode2D::MovingObstacle,v(1,1),{},v(1,0)});}));
}
TEST_SUITE_END;
