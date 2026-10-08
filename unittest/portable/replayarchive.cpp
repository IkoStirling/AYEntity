// Production file adapters, fixed Windows fixture, independent process interruption.
#include "../DetTypedSessionScenario.h"
#include <AYEntity/DeterministicRollbackReplay.h>
#include <AYReplay/ReplayHash.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <chrono>
#include <cstdlib>
#include <cfenv>
using namespace ayt::entity;
namespace fs=std::filesystem;
namespace {
using Bytes=std::vector<std::uint8_t>;
void require(bool ok,const std::string& text){if(!ok)throw std::runtime_error(text);}
Bytes read(const fs::path& path){std::ifstream f(path,std::ios::binary);require(bool(f),"read "+path.string());return {std::istreambuf_iterator<char>(f),std::istreambuf_iterator<char>()};}
void write(const fs::path& path,const Bytes& data){std::ofstream f(path,std::ios::binary);f.write(reinterpret_cast<const char*>(data.data()),data.size());require(bool(f),"write");}
void integer(Bytes& out,std::uint64_t n,unsigned bytes){for(unsigned i=0;i<bytes;++i)out.push_back(static_cast<std::uint8_t>(n>>(8*i)));}
unsigned u32(const Bytes& b,std::size_t at){unsigned n=0;for(unsigned i=0;i<4;++i)n|=unsigned(b.at(at+i))<<(8*i);return n;}
void put(Bytes& b,std::size_t at,std::uint64_t n,unsigned width){for(unsigned i=0;i<width;++i)b.at(at+i)=static_cast<std::uint8_t>(n>>(8*i));}
void rechain(Bytes& b){std::uint64_t chain=0;for(std::size_t at=0;at<b.size();){const auto length=u32(b,at);
    put(b,at+4+12,chain,8);chain=ayt::replay::fnv1a64(b.data()+at+4,length-8);put(b,at+4+length-8,chain,8);at+=4+length;}}
void events(Bytes& out,const std::vector<DetConfirmedEvent>& items){for(const auto& e:items){
    integer(out,e.epoch,4);integer(out,e.tick,8);integer(out,e.command.source,4);integer(out,e.command.sequence,4);
    integer(out,e.command.type,4);integer(out,e.command.payload.size(),4);out.insert(out.end(),e.command.payload.begin(),e.command.payload.end());}}
fs::path fresh(const fs::path& parent,const char* label){return parent/(std::string(label)+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));}
void configure(DeterministicSession& s,bool reversed=false){require(dettyped_scenario::configure(s,reversed,false,true),s.error());}
void record(const fs::path& directory,bool interrupt) {
    require(fs::create_directory(directory),"fresh recording directory");
    DeterministicSession session({1,1,1,64,0});configure(session);
    DetRollbackNetworkConfig cfg;cfg.sessionId=180;cfg.localMember=1;cfg.recoveryMember=1;cfg.rollback.members={1};
    cfg.rollback.historyTicks=16;cfg.rollback.maxPredictionTicks=4;DeterministicRollbackNetwork net(session,cfg);
    DetRollbackReplayWriter writer;DetRollbackReplayArchiveOptions options;options.maxSegmentRecords=33;
    require(writer.begin(net,(directory/"sample.rpl").string(),options,7),writer.error());Bytes emitted;
    for(unsigned tick=0;tick<(interrupt?90u:128u);++tick){
        if(tick==64){require(net.beginRecovery(2),net.error());require(writer.sync(net),writer.error());}
        auto input=detsession_scenario::input(tick);std::erase_if(input.commands,[](const auto& c){return c.source!=1;});
        require(net.submitLocal(std::move(input)) && net.advance(),net.error());require(writer.sync(net),writer.error());events(emitted,net.takeConfirmedEvents());
    }
    if(interrupt)std::_Exit(0); // No writer/stream destructors: genuinely unsealed tail.
    require(writer.finish(net),writer.error());write(directory/"expected.state",encodeDetCheckpoint(*session.checkpoint()));write(directory/"expected.events",emitted);
}
Bytes play(const fs::path& path,Bytes* emitted=nullptr) {
    DeterministicSession session({1,1,1,64,0});configure(session,true);DetRollbackReplayReader reader;
    require(reader.open(path.string()),reader.error());require(reader.restoreInitial(session),reader.error());
    while(!reader.atEnd()){require(reader.advance(session),reader.error());auto e=reader.takeConfirmedEvents();if(emitted)events(*emitted,e);}
    return encodeDetCheckpoint(*session.checkpoint());
}
void copySet(const fs::path& source,const fs::path& output,const DetReplayArchiveReport& full){
    require(fs::create_directory(output),"fresh case directory");
    for(const auto& f:full.files)require(fs::copy_file(f.path,output/fs::path(f.path).filename()),"copy fixture part");
    require(fs::copy_file(source/"sample.rpi",output/"sample.rpi"),"copy fixture index");
}
void checks(const fs::path& fixture,const fs::path& crash) {
    const auto path=(fixture/"sample.rpi").string();const auto full=DetRollbackReplayArchive::inspect(path);
    require(full.valid && !full.recovery && full.files.size()==5 && full.segments.size()==2,"fixed fixture inspection");
    const auto expected=read(fixture/"expected.state");Bytes emitted;require(play(path,&emitted)==expected,"Windows fixture full state");
    require(emitted==read(fixture/"expected.events"),"Windows fixture complete event identities/payloads");
    DeterministicSession seek({1,1,1,64,0});configure(seek);DetRollbackReplayReader r;require(r.open(path),r.error());
    for(const auto tick:{100u,3u,64u,0u,128u,35u}) {
        require(r.seek(seek,tick>=64?2:1,tick),r.error());require(r.takeConfirmedEvents().empty(),"silent seek");
        while(!r.atEnd()){require(r.advance(seek),r.error());(void)r.takeConfirmedEvents();}
        require(encodeDetCheckpoint(*seek.checkpoint())==expected,"cross-file/epoch seek end");
    }
    for(const auto& f:full.files){require(DetRollbackReplayArchive::inspect(f.path).valid,"independent .rpl compatibility");
        require(r.seek(seek,f.firstEpoch,f.firstTick),r.error());require(r.takeConfirmedEvents().empty(),"silent physical boundary");}
    const auto working=fresh(fs::temp_directory_path(),"archive-checks-");require(fs::create_directory(working),"new workspace");
    const auto lost=working/"lost";copySet(fixture,lost,full);require(fs::remove(lost/"sample.rpi"),"remove owned test index");
    const auto scanned=DetRollbackReplayArchive::scan((lost/"sample.rpi").string());
    require(scanned.valid && scanned.files.size()==5 && scanned.recovery->reason==DetReplayArchiveStop::MissingSegment,"index lost scan");
    const auto rebuilt=DetRollbackReplayArchive::rebuildIndex((lost/"sample.rpi").string(),(working/"rebuilt").string());
    require(rebuilt.valid && rebuilt.recovery && rebuilt.recovery->stopOrdinal==5,"explicit recovered marker");
    require(play(rebuilt.path)==expected,"rebuilt state");require(r.open(rebuilt.path) && r.recovery(),"reader recovery status");
    const auto recoveredBytes=read(rebuilt.path);auto mixed=recoveredBytes;put(mixed,4+u32(mixed,0)+4+4,1,4);rechain(mixed);write(rebuilt.path,mixed);
    require(!r.open(rebuilt.path) && r.error().find("Mixed")!=std::string::npos,"mixed index profiles with valid checksums rejected");
    auto badMarker=recoveredBytes;put(badMarker,4+20+4+u32(badMarker,4+20),0,4);rechain(badMarker);write(rebuilt.path,badMarker);
    require(!r.open(rebuilt.path),"invalid recovery reason with valid checksums rejected");
    auto badCount=recoveredBytes;put(badCount,4+20+4+u32(badCount,4+20)+4,4,4);rechain(badCount);write(rebuilt.path,badCount);
    require(!r.open(rebuilt.path),"recovered prefix ordinal/count mismatch rejected");write(rebuilt.path,recoveredBytes);
    require(!DetRollbackReplayArchive::rebuildIndex(path,(working/"rebuilt").string()).valid,"refuse existing destination");
    const Bytes badIndex{1,2,3,4,5};write(lost/"sample.rpi",badIndex);
    require(!DetRollbackReplayArchive::inspect((lost/"sample.rpi").string()).valid,"bad index inspection");
    const auto rebuiltBad=DetRollbackReplayArchive::rebuildIndex((lost/"sample.rpi").string(),(working/"bad-index-rebuilt").string());
    require(rebuiltBad.valid && read(lost/"sample.rpi")==badIndex && play(rebuiltBad.path)==expected,"bad index rebuilt without source modification");
    for(const auto& f:full.files)require(read(f.path)==read(lost/fs::path(f.path).filename()),"original segment bytes preserved");
    const auto missing=working/"missing";copySet(fixture,missing,full);
    require(fs::remove(missing/fs::path(full.files[2].path).filename()),"remove owned middle segment");
    const auto prefix=DetRollbackReplayArchive::scan((missing/"sample.rpi").string());
    require(prefix.valid && prefix.files.size()==2 && prefix.recovery->stopOrdinal==2,"never skip missing middle");
    const auto shorter=DetRollbackReplayArchive::rebuildIndex((missing/"sample.rpi").string(),(working/"prefix").string());
    require(shorter.valid && shorter.files.size()==2 && shorter.segments.back().endTick==full.files[1].endTick,"sealed prefix index");
    require(r.open(path) && r.seek(seek,full.files[1].lastEpoch,full.files[1].endTick),"prefix reference seek");
    require(play(shorter.path)==encodeDetCheckpoint(*seek.checkpoint()),"prefix exact state");
    const auto corrupt=working/"corrupt";copySet(fixture,corrupt,full);const auto tail=corrupt/fs::path(full.files.back().path).filename();
    fs::resize_file(tail,fs::file_size(tail)-20);
    const auto tailReport=DetRollbackReplayArchive::scan((corrupt/"sample.rpi").string());
    require(tailReport.valid && tailReport.files.size()==4 && tailReport.recovery->reason==DetReplayArchiveStop::InvalidSegment
        && tailReport.issue && tailReport.issue->offset,"truncated tail record offset");
    const auto damaged=DetRollbackReplayArchive::inspect(tail.string());require(!damaged.valid && damaged.issue && damaged.issue->offset,"inspection byte diagnosis");
    const auto tailRecovered=DetRollbackReplayArchive::rebuildIndex((corrupt/"sample.rpi").string(),(working/"tail-prefix").string());
    require(tailRecovered.valid && tailRecovered.recovery->reason==DetReplayArchiveStop::InvalidSegment,"invalid tail recovery");
    const auto splice=working/"splice";copySet(fixture,splice,full);
    fs::copy_file(full.files.front().path,splice/fs::path(full.files[2].path).filename(),fs::copy_options::overwrite_existing);
    const auto mismatched=DetRollbackReplayArchive::scan((splice/"sample.rpi").string());
    require(mismatched.valid && mismatched.files.size()==2 && mismatched.recovery->reason==DetReplayArchiveStop::Discontinuity,"valid file with wrong boundary rejected");
    require(!DetRollbackReplayArchive::scan("bad.extension").valid,"invalid scan input reports failure");
    const auto interrupted=DetRollbackReplayArchive::scan((crash/"sample.rpi.partial").string());
    require(!fs::exists(crash/"sample.rpi") && interrupted.valid && interrupted.files.size()==2
        && interrupted.recovery->reason==DetReplayArchiveStop::InvalidSegment,"separate abrupt process closed prefix");
    const auto restored=DetRollbackReplayArchive::rebuildIndex((crash/"sample.rpi.partial").string(),(working/"interrupted").string());
    require(restored.valid && restored.segments.back().endTick==64 && restored.recovery->stopOrdinal==2,"abrupt recording recovery boundary");
    require(r.open(restored.path) && r.recovery() && r.seek(seek,1,64) && r.atEnd(),"recovered prefix seek/end");
    std::cout<<"PASS production file adapter: fixed Windows fixture128 ticks/5 files; exact state/events, cross-file epoch seek, lost/corrupt index reconstruction, missing/spliced/truncated segments, abrupt-process recovery and source preservation\n";
}
}
int main(int argc,char** argv){try {
    std::fesetround(FE_UPWARD);
    if(argc==3 && std::string(argv[1])=="--generate"){record(argv[2],false);return 0;}
    if(argc==3 && std::string(argv[1])=="--interrupt"){
        const auto marker=fs::path(argv[2]);const auto directory=fresh(marker.parent_path(),"crashed-recording-");
        {std::ofstream f(marker);f<<directory.string();require(bool(f),"crash marker");}
        record(directory,true);return 1;
    }
    if(argc==4 && std::string(argv[1])=="--check") {
        std::ifstream marker(argv[3]);std::string crash;std::getline(marker,crash);require(!crash.empty(),"crash process fixture");checks(argv[2],crash);return 0;
    }
    std::cerr<<"usage: --generate NEW_DIRECTORY | --interrupt MARKER | --check FIXTURE MARKER\n";return 2;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
