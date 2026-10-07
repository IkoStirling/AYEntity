#include <AYEntity/DeterministicLockstep.h>
#include <AYEntity/DeterministicReplay.h>
#include <AYEntity.h>
#include <AYTest.h>
namespace {
using namespace ayt::entity;
struct Fixture {
    World& world=World::instance();std::unique_ptr<DeterministicSession> session;
    Fixture(){world.initialize();session=std::make_unique<DeterministicSession>(world);}
    ~Fixture(){session.reset();world.shutdown();}
};
bool configure(DeterministicSession& s){return s.registerSchema({2,1,{10}}) && s.registerGlobalState(2)
    && s.registerSystem(1,0,[](auto& c){++c.globals(2)[0];return true;}) && s.seal();}
void deliver(DetLockstepBarrier& remote,DeterministicLockstep& local){for(const auto& p:remote.packets())CHECK(local.receive(2,p));}
}
TEST_SUITE(DeterministicLockstep)
TEST_CASE(single_member_empty_inputs_use_session_and_checkpoint_closure){
    Fixture f;auto& s=*f.session;CHECK(configure(s));DeterministicLockstep p(s,{17,1,1,{1}});
    CHECK(!p.submit({0,1,{{2,0,1,{}}}}));
    for(unsigned t=0;t<100;++t){CHECK(p.submit({t,1,{}}));CHECK(p.ready());CHECK(p.advance());}
    CHECK(s.nextTick()==100);CHECK(s.checkpoint()->globals.at(2)[0]==100);CHECK(!p.faulted());
}
TEST_CASE(peer_inputs_stall_and_validate_owned_sources_before_sim_writes){
    Fixture f;auto& s=*f.session;CHECK(configure(s));const auto cp=*s.checkpoint();
    DeterministicLockstep local(s,{17,1,1,{1,2}});DetLockstepBarrier remote({17,1,2,{1,2}},cp.manifest,detCheckpointHash(cp));
    CHECK(local.submit({0,1,{}}));CHECK(!local.advance());CHECK(s.nextTick()==0);
    CHECK(remote.submit(0,encodeDetInput({0,1,{{1,0,1,{}}}})));deliver(remote,local);
    CHECK(local.ready());CHECK(!local.advance());CHECK(local.faulted());CHECK(!firstDetDifference(cp,*s.checkpoint()));
}
TEST_CASE(peer_hash_and_manifest_mismatch_stop_before_next_tick){
    Fixture f;auto& s=*f.session;CHECK(configure(s));const auto cp=*s.checkpoint();
    DeterministicLockstep local(s,{17,1,1,{1,2}});DetLockstepBarrier remote({17,1,2,{1,2}},cp.manifest,detCheckpointHash(cp)^1);
    CHECK(!local.receive(2,remote.packets().front()));CHECK(local.faulted());CHECK(s.nextTick()==0);
}
TEST_CASE(correct_two_peer_inputs_commit_and_wait_for_post_tick_hash){
    Fixture f;auto& s=*f.session;CHECK(configure(s));const auto cp=*s.checkpoint();
    DeterministicLockstep local(s,{17,1,1,{1,2}});DetLockstepBarrier remote({17,1,2,{1,2}},cp.manifest,detCheckpointHash(cp));
    CHECK(local.submit({0,1,{{1,0,1,{1}}}}));CHECK(remote.submit(0,encodeDetInput({0,1,{{2,0,1,{2}}}})));deliver(remote,local);
    for(const auto& p:local.packets())CHECK(remote.receive(1,p));
    CHECK(local.advance());CHECK(!local.synchronized());CHECK(remote.commit(detCheckpointHash(*s.checkpoint())));deliver(remote,local);
    CHECK(local.synchronized());CHECK(s.nextTick()==1 && s.checkpoint()->globals.at(2)[0]==1);
}
TEST_CASE(second_tick_owner_is_detected){
    Fixture f;auto& s=*f.session;CHECK(configure(s));DeterministicLockstep local(s,{17,1,1,{1}});
    CHECK(local.submit({0,1,{}}));CHECK(s.advance({0,1,{}}));CHECK(!local.advance());CHECK(local.faulted());CHECK(s.nextTick()==1);
}
TEST_SUITE_END;
