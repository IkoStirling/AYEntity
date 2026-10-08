#include "DetHostFixture.h"
#include <AYTest.h>
#include <AYReplay/FileReplayRecorder.h>
#include <fstream>
using namespace dethost_test;
namespace {
DetRollbackNetworkConfig replayConfig(unsigned member=1) {
    DetRollbackNetworkConfig c;c.sessionId=115;c.localMember=member;c.recoveryMember=1;
    c.rollback.members={1};c.rollback.historyTicks=16;c.rollback.maxPredictionTicks=4;
    c.rollback.prediction={{1,DetPredictionMode::Hold}};return c;
}
DetTickInput ownedInput(unsigned member,std::uint64_t tick) {
    auto in=detsession_scenario::input(tick);
    std::erase_if(in.commands,[&](const auto& c){return c.source!=member;});return in;
}
void step(DeterministicRollbackNetwork& net,DeterministicSession& s) {
    CHECK_TRUE(net.submitLocal(ownedInput(net.config().localMember,s.nextTick())));
    CHECK_TRUE(net.advance());
}
}
TEST_SUITE(DeterministicRollbackReplay)
TEST_CASE(recording_seals_verified_prefix_and_rejects_missing_seal_and_overwrite) {
    DeterministicSession s({1,1,1,64,0});CHECK_TRUE(dettyped_scenario::configure(s));
    auto c=replayConfig();c.rollback.members={1,2};DeterministicRollbackNetwork net(s,c);
    DeterministicSession peer({1,1,1,64,0});CHECK_TRUE(dettyped_scenario::configure(peer));
    c.localMember=2;DeterministicRollbackNetwork remote(peer,c);
    for(const auto& p:remote.packets())CHECK_TRUE(net.receive(2,p));
    const auto base=path("rollback-prefix.rpl");std::string file;
    {DetRollbackReplayWriter w;CHECK_TRUE(w.begin(net,base));file=w.path();
        step(net,s);CHECK_TRUE(w.sync(net));CHECK(w.recordedNextTick()==0);
        CHECK(!net.history().confirmedInputAt(0));
        auto d=net.diagnostics();CHECK(d.head==1 && d.confirmed==0 && d.verified==0 && d.predictedTicks==1 && d.bufferedBytes>0);
        CHECK_TRUE(w.finish(net));}
    DetRollbackReplayReader r;CHECK_TRUE(r.open(file));CHECK(r.endTick()==0 && r.speculativeHeadAtSeal()==1);
    CHECK_TRUE(r.restoreInitial(peer));CHECK(r.atEnd());
    DetRollbackReplayWriter overwrite;CHECK_FALSE(overwrite.begin(net,base));
    const auto partial=path("rollback-unsealed.rpl");
    {DetRollbackReplayWriter w;CHECK_TRUE(w.begin(remote,partial));}
    DetRollbackReplayReader incomplete;CHECK_FALSE(incomplete.open(ayt::replay::FileReplayRecorder::rotationPathFor(partial,0)));
}
TEST_CASE(missed_history_and_changed_network_owner_cannot_seal_success) {
    DeterministicSession s({1,1,1,64,0}),other({1,1,1,64,0});
    CHECK_TRUE(dettyped_scenario::configure(s));CHECK_TRUE(dettyped_scenario::configure(other));
    DeterministicRollbackNetwork net(s,replayConfig()),remote(other,replayConfig());
    DetRollbackReplayWriter wrong;CHECK_TRUE(wrong.begin(net,path("rollback-owner.rpl")));
    CHECK_FALSE(wrong.sync(remote));CHECK_FALSE(wrong.finish(net));
    DetRollbackReplayWriter late;CHECK_TRUE(late.begin(net,path("rollback-eviction.rpl")));
    for(unsigned i=0;i<32;++i){step(net,s);(void)net.takeConfirmedEvents();}
    CHECK_FALSE(late.sync(net));CHECK_FALSE(late.finish(net));
}
TEST_CASE(epoch_seek_suppresses_effects_and_changed_logic_reports_first_field) {
    DeterministicSession s({1,1,1,64,0});CHECK_TRUE(dettyped_scenario::configure(s));
    DeterministicRollbackNetwork net(s,replayConfig());const auto base=path("rollback-epochs.rpl");
    DetRollbackReplayWriter w;CHECK_TRUE(w.begin(net,base,3));
    std::vector<DetConfirmedEvent> expected;
    for(unsigned i=0;i<12;++i) {
        if(i==6){CHECK_TRUE(net.beginRecovery(2));CHECK_TRUE(w.sync(net));}
        step(net,s);CHECK_TRUE(w.sync(net));auto e=net.takeConfirmedEvents();expected.insert(expected.end(),e.begin(),e.end());
    }
    const auto final=encodeDetCheckpoint(*s.checkpoint());CHECK_TRUE(w.finish(net));
    DeterministicSession playback({1,1,1,64,0});CHECK_TRUE(dettyped_scenario::configure(playback,true));
    DetRollbackReplayReader r;CHECK_TRUE(r.open(w.path()));CHECK(r.segments().size()==2);
    CHECK_TRUE(r.restoreInitial(playback));std::vector<DetConfirmedEvent> actual;
    while(!r.atEnd()){const bool progressed=r.advance(playback);CHECK_TRUE(progressed);if(!progressed)break;auto e=r.takeConfirmedEvents();actual.insert(actual.end(),e.begin(),e.end());}
    CHECK(actual==expected && encodeDetCheckpoint(*playback.checkpoint())==final);
    CHECK_TRUE(r.seek(playback,1,5));CHECK(playback.nextTick()==5 && r.takeConfirmedEvents().empty());
    CHECK_TRUE(r.advance(playback));CHECK(r.epoch()==1);(void)r.takeConfirmedEvents();
    CHECK_TRUE(r.advance(playback));CHECK(r.epoch()==2 && playback.nextTick()==7);CHECK(r.takeConfirmedEvents().size()>0);
    CHECK_TRUE(r.seek(playback,2,9));CHECK(r.takeConfirmedEvents().empty());
    CHECK_FALSE(r.seek(playback,2,5));CHECK(playback.nextTick()==9);
    DeterministicSession altered({1,1,1,64,0});CHECK_TRUE(dettyped_scenario::configure(altered,false,true));
    CHECK_TRUE(r.restoreInitial(altered));CHECK_FALSE(r.advance(altered));CHECK(r.difference().has_value());
    CHECK(r.difference()->tick==1);
    // Corrupt container bytes must be rejected before any Session restore.
    const auto corrupt=w.path()+".corrupt";std::filesystem::copy_file(w.path(),corrupt,std::filesystem::copy_options::overwrite_existing);
    {std::fstream f(corrupt,std::ios::in|std::ios::out|std::ios::binary);f.seekp(-12,std::ios::end);char x=0;f.write(&x,1);}
    // Explicit truncation guarantees missing seal/end regardless of compressed byte value.
    std::filesystem::resize_file(corrupt,std::filesystem::file_size(corrupt)-32);CHECK_FALSE(r.open(corrupt));
    std::filesystem::copy_file(w.path(),corrupt,std::filesystem::copy_options::overwrite_existing);
    std::filesystem::resize_file(corrupt,std::filesystem::file_size(corrupt)-sizeof(ayt::replay::ReplayEventHeader));
    CHECK_FALSE(r.open(corrupt)); // seal exists, physical SessionEnd is missing
    std::filesystem::copy_file(w.path(),corrupt,std::filesystem::copy_options::overwrite_existing);
    {std::ofstream f(corrupt,std::ios::binary|std::ios::app);f.put(0);}CHECK_FALSE(r.open(corrupt));
}
TEST_CASE(asymmetric_recovery_records_snapshot_gap_and_old_epoch_events_once) {
    DeterministicSession a({1,1,1,64,0}),b({1,1,1,64,0});
    CHECK_TRUE(dettyped_scenario::configure(a));CHECK_TRUE(dettyped_scenario::configure(b));
    auto c=replayConfig();c.rollback.members={1,2};DeterministicRollbackNetwork authority(a,c);c.localMember=2;DeterministicRollbackNetwork peer(b,c);
    DetRollbackReplayWriter writer;CHECK_TRUE(writer.begin(peer,path("rollback-gap.rpl"),2));
    auto exchange=[&] {
        for(const auto& p:authority.packets()) {CHECK_TRUE(peer.receive(1,p));CHECK_TRUE(writer.sync(peer));}
        for(const auto& p:peer.packets())CHECK_TRUE(authority.receive(2,p));
    };exchange();
    for(unsigned tick=0;tick<3;++tick) {
        CHECK_TRUE(authority.submitLocal(ownedInput(1,tick)));CHECK_TRUE(peer.submitLocal(ownedInput(2,tick)));
        exchange();CHECK_TRUE(authority.advance());CHECK_TRUE(peer.advance());CHECK_TRUE(writer.sync(peer));
        // Deliver only peer hashes; authority confirms, peer never sees the new authority hash.
        for(const auto& p:peer.packets())CHECK_TRUE(authority.receive(2,p));
    }
    // The next loop's exchange verified the earlier two ticks, leaving tick 2 unverified.
    CHECK(writer.recordedNextTick()==2 && authority.verifiedNextTick()==3);
    auto expected=authority.takeConfirmedEvents();auto presented=peer.takeConfirmedEvents();CHECK(presented.size()<expected.size());
    CHECK_TRUE(authority.beginRecovery(2));exchange();exchange();
    auto repaired=peer.takeConfirmedEvents();presented.insert(presented.end(),repaired.begin(),repaired.end());CHECK(presented==expected);
    CHECK_TRUE(writer.finish(peer));
    DetRollbackReplayReader reader;CHECK_TRUE(reader.open(writer.path()));CHECK(reader.segments().size()==2 && reader.segments()[1].skippedTicks==1);
    DeterministicSession replay({1,1,1,64,0});CHECK_TRUE(dettyped_scenario::configure(replay));CHECK_TRUE(reader.restoreInitial(replay));
    std::vector<DetConfirmedEvent> events;
    while(!reader.atEnd()){const bool progressed=reader.advance(replay);CHECK_TRUE(progressed);if(!progressed)break;auto e=reader.takeConfirmedEvents();events.insert(events.end(),e.begin(),e.end());}
    CHECK(events==expected && encodeDetCheckpoint(*replay.checkpoint())==encodeDetCheckpoint(*a.checkpoint()));
    CHECK_FALSE(reader.seek(replay,2,2));CHECK_TRUE(reader.seek(replay,1,2));CHECK(reader.takeConfirmedEvents().empty());
}
TEST_SUITE_END
