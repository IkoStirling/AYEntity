#include "DetHostFixture.h"
#include <AYTest.h>
#include <chrono>
using namespace dethost_test;
namespace {
DetRollbackNetworkConfig config(unsigned member=1,unsigned delay=0) {
    DetRollbackNetworkConfig c;c.sessionId=92;c.localMember=member;c.recoveryMember=1;c.inputDelay=delay;
    c.rollback.members={1,2};c.rollback.historyTicks=16;c.rollback.maxPredictionTicks=4;
    c.rollback.prediction={{1,DetPredictionMode::Hold}};return c;
}
DetTickInput input(unsigned member,std::uint64_t tick) {
    auto in=detsession_scenario::input(tick);std::erase_if(in.commands,[member](const auto& c){return c.source!=member;});return in;
}
DetHostedSceneRecipe recipe(unsigned& samples,unsigned delay=0) {
    auto r=Fixture::recipe();r.rollback=config(1,delay);
    r.input=[&samples,delay](const auto& request,auto& in){++samples;in=input(1,request.tick-delay);in.tick=request.tick;return true;};return r;
}
void exchange(Fixture& f,DeterministicRollbackNetwork& remote) {
    for(const auto& p:f.controller.networkPackets())CHECK_TRUE(remote.receive(1,p));
    for(const auto& p:remote.packets())CHECK_TRUE(f.controller.receiveNetwork(2,p));
}
}
TEST_SUITE(DeterministicHostRollback)
TEST_CASE(prediction_horizon_blocks_clocks_and_late_correction_replays_without_sampling) {
    Fixture f;unsigned samples=0;CHECK_TRUE(f.bind(recipe(samples)));
    DeterministicSession s({1,1,1,64,0});CHECK_TRUE(dettyped_scenario::configure(s,true));
    DeterministicRollbackNetwork remote(s,config(2));exchange(f,remote);
    for(unsigned i=0;i<4;++i)f.frame();CHECK(samples==4 && f.controller.session()->nextTick()==4);
    f.frame();const auto hostTick=f.loop.getSimTick();CHECK(samples==5 && f.controller.networkWaiting());
    for(unsigned i=0;i<12;++i)f.frame();CHECK(samples==5 && f.loop.getSimTick()==hostTick);
    CHECK(f.controller.takeConfirmedEvents().empty());
    // Submit actual remote frames and run remote Sim. Ingress corrects Host state,
    // but neither its scheduling clock nor its input callback runs during replay.
    for(unsigned tick=0;tick<4;++tick){CHECK_TRUE(remote.submitLocal(input(2,tick)));
        exchange(f,remote);CHECK_TRUE(remote.advance());}
    exchange(f,remote);
    CHECK(samples==5 && f.loop.getSimTick()==hostTick && f.controller.rollbackCount()>0);
    CHECK(encodeDetCheckpoint(*f.controller.checkpoint())==encodeDetCheckpoint(*s.checkpoint()));
    CHECK(f.controller.networkVerifiedNextTick()==4 && f.controller.takeConfirmedEvents().size()>=4);
    CHECK(f.controller.takeConfirmedEvents().empty());f.frame();
    CHECK(samples==5 && f.controller.session()->nextTick()==5 && f.loop.getSimTick()==hostTick+1);
}
TEST_CASE(pause_survives_ingress_and_authority_recovery_resumes_through_standard_host) {
    Fixture f;unsigned samples=0;CHECK_TRUE(f.bind(recipe(samples,2)));
    DeterministicSession s({1,1,1,64,0});CHECK_TRUE(dettyped_scenario::configure(s));
    DeterministicRollbackNetwork remote(s,config(2,2));exchange(f,remote);f.controller.pause();
    CHECK_TRUE(remote.submitLocal({2,1,{}}));exchange(f,remote);f.frame();CHECK(samples==0);
    CHECK_TRUE(f.controller.stepOnce());CHECK(samples==1 && f.controller.session()->nextTick()==1);
    CHECK_TRUE(remote.advance());exchange(f,remote);
    CHECK_FALSE(f.controller.restore(*f.controller.checkpoint()));
    CHECK_TRUE(f.controller.beginNetworkRecovery(2));exchange(f,remote);exchange(f,remote);
    f.frame();CHECK(f.controller.session()->nextTick()==1 && samples==1);
    CHECK_TRUE(f.controller.resume());f.frame();CHECK(samples==2 && f.controller.session()->nextTick()==2);
}
TEST_CASE(rollback_recipe_rejects_replay_empty_record_path_and_two_tick_owners) {
    for(auto mode:{DetHostMode::Record,DetHostMode::Replay}) {Fixture f;unsigned samples=0;auto r=recipe(samples);r.mode=mode;
        CHECK_FALSE(f.bind(r));CHECK(samples==0);}
    Fixture f;unsigned samples=0;auto r=recipe(samples);r.lockstep=DetLockstepConfig{92,1,1,{1,2}};CHECK_FALSE(f.bind(r));
}
TEST_CASE(archive_options_require_rollback_record_mode) {
    for(auto mode:{DetHostMode::Live,DetHostMode::Replay}) {Fixture f;auto r=Fixture::recipe(mode);
        r.rollbackArchive=DetRollbackReplayArchiveOptions{};CHECK_FALSE(f.bind(r));}
    Fixture f;auto r=Fixture::recipe(DetHostMode::Record,path("archive-no-rollback.rpl"));
    r.rollbackArchive=DetRollbackReplayArchiveOptions{};CHECK_FALSE(f.bind(r));
}
TEST_CASE(host_exposes_recovered_source_status_separately_from_playback_completion) {
    const auto directory=std::filesystem::temp_directory_path()/("host-recovered-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    CHECK_TRUE(std::filesystem::create_directory(directory));
    DeterministicSession source({1,1,1,64,0});CHECK_TRUE(dettyped_scenario::configure(source));
    auto cfg=config();cfg.rollback.members={1};DeterministicRollbackNetwork net(source,cfg);
    DetRollbackReplayWriter writer;CHECK_TRUE(writer.begin(net,(directory/"sample.rpl").string(),DetRollbackReplayArchiveOptions{}));
    CHECK_TRUE(net.submitLocal(input(1,0)) && net.advance() && writer.finish(net));
    const auto rebuilt=DetRollbackReplayArchive::rebuildIndex(writer.path(),(directory/"recovered").string());CHECK_TRUE(rebuilt.valid);
    Fixture f;CHECK_TRUE(f.bind(Fixture::recipe(DetHostMode::Replay,rebuilt.path)));CHECK(f.controller.replayRecovery().has_value());
    CHECK_TRUE(f.controller.seekRollback(1,1));CHECK(f.controller.state()==DetHostState::Completed);
    CHECK(f.controller.replayRecovery().has_value() && f.controller.takeConfirmedEvents().empty());
}
TEST_CASE(actual_host_matches_owned_baseline_after_10000_delayed_ticks_with_exact_effects) {
    Fixture f;unsigned samples=0;auto recording=recipe(samples);recording.mode=DetHostMode::Record;
    recording.replayPath=path(("rollback-10000-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".rpl").c_str());
    recording.rollbackArchive=DetRollbackReplayArchiveOptions{};recording.rollbackArchive->maxSegmentRecords=1000;
    recording.checkpointInterval=300;CHECK_TRUE(f.bind(recording));
    DeterministicSession s({1,1,1,64,0}),baseline({1,1,1,64,0});
    CHECK_TRUE(dettyped_scenario::configure(s,true));CHECK_TRUE(dettyped_scenario::configure(baseline));
    DeterministicRollbackNetwork remote(s,config(2));
    std::vector<DetConfirmedEvent> expected,actual;
    for(std::uint64_t tick=0;tick<10000;++tick){CHECK_TRUE(baseline.advance(detsession_scenario::input(tick)));
        const auto cp=baseline.checkpoint();for(const auto& c:cp->pendingEvents)expected.push_back({1,tick,c});}
    exchange(f,remote);
    for(std::uint64_t round=0;round<30000;++round) {
        if(s.nextTick()<10000 && !remote.history().hasInput(2,remote.localInputTick()))CHECK_TRUE(remote.submitLocal(input(2,remote.localInputTick())));
        if(round%3==0)exchange(f,remote);
        if(f.controller.session()->nextTick()<10000)f.frame();
        if(s.nextTick()<10000 && remote.ready())CHECK_TRUE(remote.advance());
        auto effects=f.controller.takeConfirmedEvents();actual.insert(actual.end(),effects.begin(),effects.end());(void)remote.takeConfirmedEvents();
        CHECK(f.controller.state()!=DetHostState::Faulted);
        if(f.controller.session()->nextTick()==10000 && s.nextTick()==10000) {exchange(f,remote);exchange(f,remote);
            auto last=f.controller.takeConfirmedEvents();actual.insert(actual.end(),last.begin(),last.end());break;}
    }
    CHECK(samples==10000 && f.controller.networkSynchronized() && actual==expected);
    CHECK(encodeDetCheckpoint(*f.controller.checkpoint())==encodeDetCheckpoint(*baseline.checkpoint()));
    CHECK(encodeDetCheckpoint(*s.checkpoint())==encodeDetCheckpoint(*baseline.checkpoint()));
    const auto diagnostics=f.controller.rollbackDiagnostics();
    CHECK(diagnostics.head==10000 && diagnostics.verified==10000 && diagnostics.maxDepth>0 && diagnostics.replayedTicks>=diagnostics.rollbacks);
    const auto final=encodeDetCheckpoint(*baseline.checkpoint());const auto file=f.controller.recordingPath();
    CHECK_TRUE(f.controller.stop());
    DetRollbackReplayReader reader;CHECK_TRUE(reader.open(file));CHECK(reader.files().size()>10);CHECK_TRUE(reader.restoreInitial(baseline));
    std::vector<DetConfirmedEvent> replayEffects;
    while(!reader.atEnd()){const bool progressed=reader.advance(baseline);CHECK_TRUE(progressed);if(!progressed)break;auto e=reader.takeConfirmedEvents();replayEffects.insert(replayEffects.end(),e.begin(),e.end());}
    CHECK(replayEffects==expected && encodeDetCheckpoint(*baseline.checkpoint())==final);
    CHECK_TRUE(reader.seek(baseline,1,7351));CHECK(reader.takeConfirmedEvents().empty());
    while(!reader.atEnd()){const bool progressed=reader.advance(baseline);CHECK_TRUE(progressed);if(!progressed)break;}
    CHECK(encodeDetCheckpoint(*baseline.checkpoint())==final);
    f.controller.disconnect();CHECK_TRUE(f.bind(Fixture::recipe(DetHostMode::Replay,file)));
    CHECK_TRUE(f.controller.seekRollback(1,9999));CHECK(f.controller.takeConfirmedEvents().empty());
    CHECK_TRUE(f.controller.stepOnce());CHECK(f.controller.state()==DetHostState::Completed);
    CHECK(encodeDetCheckpoint(*f.controller.checkpoint())==final && f.controller.takeConfirmedEvents().size()>0);
}
TEST_SUITE_END
