#include "DetHostFixture.h"
#include <AYTest.h>
#include <AYReplay/FileReplayRecorder.h>
#include <fstream>
#include <chrono>
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
std::string archivePath(const char* name) {
    return path((std::string(name)+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".rpl").c_str());
}
std::vector<char> readBytes(const std::string& path) {
    std::ifstream f(path,std::ios::binary);return {std::istreambuf_iterator<char>(f),std::istreambuf_iterator<char>()};
}
void writeBytes(const std::string& path,const std::vector<char>& bytes) {
    std::ofstream f(path,std::ios::binary|std::ios::trunc);f.write(bytes.data(),bytes.size());
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
    DetRollbackReplayWriter archived;CHECK_TRUE(archived.begin(net,archivePath("archive-prefix"),DetRollbackReplayArchiveOptions{}));
    {DetRollbackReplayWriter w;CHECK_TRUE(w.begin(net,base));file=w.path();
        step(net,s);CHECK_TRUE(w.sync(net));CHECK(w.recordedNextTick()==0);
        CHECK(!net.history().confirmedInputAt(0));
        auto d=net.diagnostics();CHECK(d.head==1 && d.confirmed==0 && d.verified==0 && d.predictedTicks==1 && d.bufferedBytes>0);
        CHECK_TRUE(w.finish(net));}
    CHECK_TRUE(archived.finish(net));DetRollbackReplayReader archiveReader;CHECK_TRUE(archiveReader.open(archived.path()));
    CHECK(archiveReader.endTick()==0 && archiveReader.speculativeHeadAtSeal()==1 && archiveReader.files().size()==1);
    CHECK_TRUE(archiveReader.restoreInitial(peer));CHECK(archiveReader.atEnd());
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
    DetRollbackReplayWriter writer;DetRollbackReplayArchiveOptions options;options.maxSegmentRecords=2;
    CHECK_TRUE(writer.begin(peer,archivePath("rollback-gap"),options,2));
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
    CHECK(reader.files().size()>=3);CHECK_TRUE(reader.seek(replay,2,3));CHECK(reader.atEnd());
}
TEST_CASE(indexed_archive_rotates_seeks_and_preserves_exact_events_across_files_and_epochs) {
    DeterministicSession s({1,1,1,64,0});CHECK_TRUE(dettyped_scenario::configure(s));
    DeterministicRollbackNetwork net(s,replayConfig());DetRollbackReplayWriter w;
    DetRollbackReplayArchiveOptions options;options.maxSegmentRecords=4;
    CHECK_TRUE(w.begin(net,archivePath("archive-epochs"),options,3));const auto index=w.path();
    CHECK(std::filesystem::path(index).extension()==".rpi" && !std::filesystem::exists(index));
    std::vector<DetConfirmedEvent> expected;std::vector<std::vector<std::uint8_t>> states;
    states.push_back(encodeDetCheckpoint(*s.checkpoint()));
    for(unsigned tick=0;tick<12;++tick) {
        if(tick==6){CHECK_TRUE(net.beginRecovery(2));CHECK_TRUE(w.sync(net));}
        step(net,s);CHECK_TRUE(w.sync(net));auto e=net.takeConfirmedEvents();expected.insert(expected.end(),e.begin(),e.end());
        states.push_back(encodeDetCheckpoint(*s.checkpoint()));
    }
    CHECK_TRUE(w.finish(net));CHECK(w.path()==index && w.fileSegmentCount()>=5);
    CHECK(!std::filesystem::exists(index+".partial"));
    DetRollbackReplayReader r;CHECK_TRUE(r.open(index));CHECK(r.files().size()==w.fileSegmentCount());CHECK(r.segments().size()==2);
    for(const auto& file:r.files()) {DetRollbackReplayReader part;CHECK_TRUE(part.open(file.path));
        CHECK(part.files().empty() && file.records<=options.maxSegmentRecords && file.storedBytes<=options.maxSegmentBytes);}
    DeterministicSession playback({1,1,1,64,0});CHECK_TRUE(dettyped_scenario::configure(playback,true));
    CHECK_TRUE(r.restoreInitial(playback));std::vector<DetConfirmedEvent> actual;
    while(!r.atEnd()){const auto ok=r.advance(playback);CHECK_TRUE(ok);if(!ok)break;
        auto e=r.takeConfirmedEvents();actual.insert(actual.end(),e.begin(),e.end());}
    CHECK(actual==expected && encodeDetCheckpoint(*playback.checkpoint())==states.back());
    for(const auto tick:{10u,2u,6u,0u,12u,7u,3u}) {
        CHECK_TRUE(r.seek(playback,tick>=6?2:1,tick));
        CHECK(encodeDetCheckpoint(*playback.checkpoint())==states[tick] && r.takeConfirmedEvents().empty());
    }
    for(const auto& file:r.files()) {CHECK_TRUE(r.seek(playback,file.firstEpoch,file.firstTick));CHECK(r.takeConfirmedEvents().empty());}
    CHECK_TRUE(r.seek(playback,1,5));CHECK_TRUE(r.advance(playback));CHECK(r.epoch()==1);
    CHECK_TRUE(r.advance(playback));CHECK(r.epoch()==2 && playback.nextTick()==7);
    CHECK_TRUE(r.seek(playback,2,12));CHECK(r.atEnd());
    DeterministicSession fresh({1,1,1,64,0});CHECK_TRUE(dettyped_scenario::configure(fresh));
    DeterministicRollbackNetwork newOwner(fresh,replayConfig());
    DetRollbackReplayWriter overwrite;CHECK_FALSE(overwrite.begin(newOwner,index,options));
    CHECK(overwrite.error().find("already exists")!=std::string::npos);
    // A sealed index is mandatory; any truncation, chain change or trailing bytes fails.
    const auto original=readBytes(index);auto corrupt=original;corrupt.pop_back();writeBytes(index,corrupt);
    DetRollbackReplayReader bad;CHECK_FALSE(bad.open(index));
    corrupt=original;corrupt[30]^=1;writeBytes(index,corrupt);CHECK_FALSE(bad.open(index));
    corrupt=original;corrupt.push_back(0);writeBytes(index,corrupt);CHECK_FALSE(bad.open(index));writeBytes(index,original);
}
TEST_CASE(archive_later_corruption_is_rejected_before_seek_or_boundary_mutates_session) {
    DeterministicSession s({1,1,1,64,0});CHECK_TRUE(dettyped_scenario::configure(s));
    DeterministicRollbackNetwork net(s,replayConfig());DetRollbackReplayWriter w;
    DetRollbackReplayArchiveOptions options;options.maxSegmentRecords=3;
    CHECK_TRUE(w.begin(net,archivePath("archive-damage"),options));
    for(unsigned i=0;i<8;++i){step(net,s);CHECK_TRUE(w.sync(net));(void)net.takeConfirmedEvents();}CHECK_TRUE(w.finish(net));
    DetRollbackReplayReader r;CHECK_TRUE(r.open(w.path()));const auto files=r.files();CHECK(files.size()>=4);
    const auto original=readBytes(files[1].path);auto corrupt=original;corrupt[corrupt.size()/2]^=1;writeBytes(files[1].path,corrupt);
    CHECK_TRUE(r.open(w.path())); // lazy: unchanged size, first file remains valid
    DeterministicSession playback({1,1,1,64,0});CHECK_TRUE(dettyped_scenario::configure(playback));CHECK_TRUE(r.restoreInitial(playback));
    const auto before=encodeDetCheckpoint(*playback.checkpoint());
    CHECK_FALSE(r.seek(playback,1,files[1].firstTick+1));CHECK(encodeDetCheckpoint(*playback.checkpoint())==before);
    CHECK_TRUE(r.advance(playback));CHECK_TRUE(r.advance(playback));(void)r.takeConfirmedEvents();
    const auto boundary=encodeDetCheckpoint(*playback.checkpoint());CHECK_FALSE(r.advance(playback));
    CHECK(encodeDetCheckpoint(*playback.checkpoint())==boundary && r.takeConfirmedEvents().empty());
    writeBytes(files[1].path,original);CHECK_TRUE(r.advance(playback));
    // Physical file metadata is checked even before lazy payload loading.
    std::filesystem::rename(files.back().path,files.back().path+".missing");CHECK_FALSE(r.open(w.path()));
    std::filesystem::rename(files.back().path+".missing",files.back().path);
    const auto last=readBytes(files.back().path);auto shortFile=last;shortFile.pop_back();writeBytes(files.back().path,shortFile);CHECK_FALSE(r.open(w.path()));
    writeBytes(files.back().path,last);CHECK_TRUE(r.open(w.path()));
}
TEST_CASE(archive_byte_limits_unsealed_output_and_segment_exhaustion_fail_explicitly) {
    DeterministicSession s({1,1,1,64,0});CHECK_TRUE(dettyped_scenario::configure(s));
    DeterministicRollbackNetwork net(s,replayConfig());
    DetRollbackReplayArchiveOptions options;options.maxSegmentBytes=8192;options.maxSegmentRecords=99996;
    DetRollbackReplayWriter w;CHECK_TRUE(w.begin(net,archivePath("archive-bytes"),options));
    for(unsigned i=0;i<32;++i){step(net,s);CHECK_TRUE(w.sync(net));(void)net.takeConfirmedEvents();}CHECK_TRUE(w.finish(net));
    DetRollbackReplayReader r;CHECK_TRUE(r.open(w.path()));CHECK(r.files().size()>1);
    for(const auto& f:r.files())CHECK(f.storedBytes<=options.maxSegmentBytes);
    // New owners start at tick zero; no publication for interruption or invalid budgets.
    DeterministicSession initial({1,1,1,64,0});CHECK_TRUE(dettyped_scenario::configure(initial));
    DeterministicRollbackNetwork fresh(initial,replayConfig());std::string unfinished;
    {DetRollbackReplayWriter partial;CHECK_TRUE(partial.begin(fresh,archivePath("archive-partial"),options));unfinished=partial.path();}
    CHECK(!std::filesystem::exists(unfinished));CHECK_FALSE(r.open(unfinished+".partial"));
    options.maxSegmentRecords=2;options.maxSegments=1;DetRollbackReplayWriter capped;
    CHECK_TRUE(capped.begin(fresh,archivePath("archive-capped"),options));step(fresh,initial);CHECK_TRUE(capped.sync(fresh));
    step(fresh,initial);CHECK_FALSE(capped.sync(fresh));CHECK_FALSE(capped.finish(fresh));CHECK(!std::filesystem::exists(capped.path()));
    DeterministicSession zero({1,1,1,64,0});CHECK_TRUE(dettyped_scenario::configure(zero));DeterministicRollbackNetwork healthy(zero,replayConfig());
    options.maxSegmentRecords=1;DetRollbackReplayWriter invalid;CHECK_FALSE(invalid.begin(healthy,archivePath("archive-invalid"),options));
    options.maxSegmentRecords=2;options.maxSegmentBytes=1024;DetRollbackReplayWriter tiny;
    const bool started=tiny.begin(healthy,archivePath("archive-tiny"),options);
    if(started){step(healthy,zero);CHECK_FALSE(tiny.sync(healthy));}CHECK(!std::filesystem::exists(tiny.path()));
}
TEST_SUITE_END
