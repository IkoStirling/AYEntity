#include "DetHostFixture.h"
#include <AYTest.h>
using namespace dethost_test;
namespace {
DetHostedSceneRecipe networkRecipe(unsigned& calls) {
    auto r=Fixture::recipe();r.lockstep=DetLockstepConfig{71,1,1,{1,2},2,2};
    r.input=[&calls](const auto& request,auto& packet){
        ++calls;packet=detsession_scenario::input(request.tick);
        std::erase_if(packet.commands,[](const auto& c){return c.source!=1;});return true;};
    return r;
}
DetLockstepBarrier remote(const Fixture& f,unsigned epoch=1) {
    const auto cp=*f.controller.checkpoint();return {{71,epoch,2,{1,2},2,2},cp.manifest,detCheckpointHash(cp),cp.nextTick};
}
void exchange(Fixture& f,DetLockstepBarrier& b) {
    for(const auto& p:f.controller.networkPackets())CHECK_TRUE(b.receive(1,p));
    for(const auto& p:b.packets())CHECK_TRUE(f.controller.receiveNetwork(2,p));
}
void contribute(DetLockstepBarrier& b,unsigned tick) {
    auto input=detsession_scenario::input(tick,true);
    std::erase_if(input.commands,[](const auto& c){return c.source!=2;});
    CHECK_TRUE(b.submit(tick,encodeDetInput(input)));
}
}
TEST_SUITE(DeterministicHostNetwork)
TEST_CASE(missing_peer_blocks_both_clocks_and_samples_local_input_once) {
    Fixture f;unsigned calls=0;CHECK_TRUE(f.bind(networkRecipe(calls)));auto peer=remote(f);
    const auto initial=encodeDetCheckpoint(*f.controller.checkpoint());const auto hostTick=f.loop.getSimTick();
    f.frame();for(int i=0;i<12;++i)f.frame(1.0f/32.0f);
    CHECK(calls==1 && f.controller.session()->nextTick()==0 && f.loop.getSimTick()==hostTick);
    CHECK(f.controller.state()==DetHostState::Running && f.controller.networkWaiting());
    CHECK(encodeDetCheckpoint(*f.controller.checkpoint())==initial);
    contribute(peer,0);exchange(f,peer);f.frame();
    CHECK(calls==1 && f.controller.session()->nextTick()==1 && f.loop.getSimTick()==hostTick+1);
    CHECK_TRUE(peer.commit(detCheckpointHash(*f.controller.checkpoint())));exchange(f,peer);
    CHECK_TRUE(f.controller.networkSynchronized());
    f.frame();CHECK(calls==2 && f.controller.session()->nextTick()==1);
}
TEST_CASE(explicit_pause_is_not_cleared_by_network_readiness_and_step_is_exactly_once) {
    Fixture f;unsigned calls=0;CHECK_TRUE(f.bind(networkRecipe(calls)));auto peer=remote(f);
    f.frame();f.controller.pause();contribute(peer,0);exchange(f,peer);f.frame();
    CHECK(f.controller.session()->nextTick()==0 && calls==1);
    CHECK_TRUE(f.controller.stepOnce());CHECK(f.controller.session()->nextTick()==1 && calls==1);
    CHECK_TRUE(peer.commit(detCheckpointHash(*f.controller.checkpoint())));exchange(f,peer);
    f.frame();CHECK(f.controller.session()->nextTick()==1);
}
TEST_CASE(disconnect_faults_epoch_and_agreed_checkpoint_reset_requires_new_epoch) {
    Fixture f;unsigned calls=0;CHECK_TRUE(f.bind(networkRecipe(calls)));const auto saved=*f.controller.checkpoint();
    CHECK_FALSE(f.controller.restore(saved));CHECK_FALSE(f.controller.resetNetwork(saved,1));
    CHECK_FALSE(f.controller.disconnectNetworkMember(2));CHECK(f.controller.state()==DetHostState::Faulted);
    CHECK_TRUE(f.controller.resetNetwork(saved,2));auto peer=remote(f,2);
    CHECK_TRUE(f.controller.resume());f.frame();contribute(peer,0);exchange(f,peer);f.frame();
    CHECK(f.controller.session()->nextTick()==1);
}
TEST_CASE(replay_rejects_live_network_and_wrong_local_source_fails_before_state_writes) {
    {Fixture f;unsigned calls=0;auto r=networkRecipe(calls);r.mode=DetHostMode::Replay;CHECK_FALSE(f.bind(r));}
    {Fixture f;unsigned calls=0;auto r=networkRecipe(calls);r.input=[](const auto&,auto& p){p=detsession_scenario::input(p.tick);return true;};
        CHECK_TRUE(f.bind(r));const auto saved=encodeDetCheckpoint(*f.controller.checkpoint());f.frame();
        CHECK(f.controller.state()==DetHostState::Faulted && f.controller.session()->nextTick()==0);
        CHECK(encodeDetCheckpoint(*f.controller.checkpoint())==saved);}
}
TEST_CASE(wrong_post_tick_hash_faults_without_second_tick_or_ordinary_fallback) {
    Fixture f;unsigned calls=0;CHECK_TRUE(f.bind(networkRecipe(calls)));auto peer=remote(f);
    f.frame();contribute(peer,0);exchange(f,peer);f.frame();
    CHECK_TRUE(peer.commit(detCheckpointHash(*f.controller.checkpoint())^1));
    bool rejected=false;for(const auto& p:peer.packets())if(!f.controller.receiveNetwork(2,p))rejected=true;
    CHECK(rejected && f.controller.state()==DetHostState::Faulted);f.frame();
    CHECK(f.controller.session()->nextTick()==1 && calls==1);
}
TEST_SUITE_END
