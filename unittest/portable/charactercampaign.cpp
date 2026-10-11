// design reference: AYEntity/design.md Stage23; streamed character campaigns and real replay representatives.
#include "../DetCharacterCampaign.h"
#include <AYEntity/DeterministicReplay.h>
#include <AYEntity/DeterministicRollbackReplay.h>
#include <charconv>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

namespace {
using namespace detcharacter_campaign;namespace fs=std::filesystem;using Clock=std::chrono::steady_clock;
struct Options {std::uint64_t seed=1,cases=8,seconds=0;std::uint32_t ticks=0;fs::path output;bool longWindows=false;};
std::string quote(std::string_view value){std::ostringstream out;out<<'"';constexpr char digits[]="0123456789abcdef";
    for(unsigned char c:value){if(c=='"'||c=='\\')out<<'\\'<<c;else if(c<32)out<<"\\u00"<<digits[c>>4]<<digits[c&15];else out<<c;}out<<'"';return out.str();}
std::string decimal(std::uint64_t n){return quote(std::to_string(n));}
std::uint64_t number(std::string_view text,std::uint64_t low,std::uint64_t high,const char* name){std::uint64_t n=0;
    const auto [end,err]=std::from_chars(text.data(),text.data()+text.size(),n);if(text.empty() || err!=std::errc{} || end!=text.data()+text.size() || n<low || n>high)throw std::invalid_argument(std::string("Invalid ")+name);return n;}
Options options(int argc,char** argv){Options o;for(int i=1;i<argc;++i){std::string_view key=argv[i];
    if(key=="--long-boundaries"){o.longWindows=true;continue;}
    if(key!="--seed" && key!="--cases" && key!="--seconds" && key!="--ticks" && key!="--output")throw std::invalid_argument("Unknown option: "+std::string(key));
    if(++i==argc)throw std::invalid_argument("Missing option value");const std::string_view value=argv[i];
    if(key=="--seed")o.seed=number(value,0,UINT64_MAX,"seed");if(key=="--cases")o.cases=number(value,1,1000000,"cases1..1000000");
    if(key=="--seconds")o.seconds=number(value,0,86400,"seconds0..86400");if(key=="--ticks")o.ticks=std::uint32_t(number(value,192,1024,"ticks192..1024"));
    if(key=="--output"){if(value.empty())throw std::invalid_argument("Empty output");o.output=argv[i];}}
    if(o.output.empty())throw std::invalid_argument("--output is required");return o;}
fs::path fresh(const fs::path& root){const auto status=fs::symlink_status(root);
    if(!fs::is_directory(status)||fs::is_symlink(status))throw std::invalid_argument("--output must be an existing ordinary parent directory");
    const auto stamp=Clock::now().time_since_epoch().count();for(unsigned i=0;i<64;++i){auto path=root/("character-campaign-"+std::to_string(stamp)+"-"+std::to_string(i));
        if(fs::create_directory(path))return fs::absolute(path);}throw std::runtime_error("Cannot reserve fresh campaign directory");}
void write(const fs::path& path,std::string_view value){std::ofstream file(path,std::ios::binary|std::ios::trunc);file.write(value.data(),std::streamsize(value.size()));file.flush();if(!file)throw std::runtime_error("Artifact write: "+path.string());}
void writeProgress(const fs::path& path,std::string_view value){auto staged=path;staged+=".tmp";write(staged,value);
#if defined(_WIN32)
    if(!MoveFileExW(staged.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))throw std::runtime_error("Atomic progress replacement failed");
#else
    fs::rename(staged,path);
#endif
}
void binary(const fs::path& path,const Bytes& value){if(!value.empty())write(path,{reinterpret_cast<const char*>(value.data()),value.size()});}
Bytes canonical(const DetTickInput& original){auto in=original;std::string error;if(!canonicalizeDetInput(in,error))throw std::runtime_error(error);return encodeDetInput(in);}
void appendFrame(Bytes& frames,const Bytes& bytes){const auto n=std::uint32_t(bytes.size());for(unsigned i=0;i<4;++i)frames.push_back(std::uint8_t(n>>(8*i)));frames.insert(frames.end(),bytes.begin(),bytes.end());}
std::uint64_t digest(std::uint64_t previous,const Bytes& bytes){for(auto b:bytes)previous=(previous^b)*1099511628211ull;return previous;}
void check(bool valid,std::uint64_t seed,std::uint64_t tick,std::uint32_t count,std::string phase,std::string why){if(!valid)throw CaseFailure(seed,tick,count,std::move(phase),std::move(why));}
DetSessionCheckpoint cp(DeterministicSession& s,std::uint64_t seed,std::uint32_t count,const std::string& phase){auto value=s.checkpoint();check(bool(value),seed,s.nextTick(),count,phase,s.error());return *value;}
struct ReplayResult {std::uint64_t ticks=0,checkpoints=0,events=0,files=0,originalInputHash=0,variantInputHash=0;};
ReplayResult replayRepresentative(std::uint64_t seed,std::uint32_t count,const fs::path& dir,const Pulse& pulse){
    const auto p=policy(seed);ReplayResult result;std::string phase="representative-original-rpl";std::vector<DetSessionCheckpoint> rows;
    Bytes originalSubmitted,variantSubmitted,activeCanonicalInput;
    std::optional<DetSessionCheckpoint> operationBefore,lastExpected,lastActual;std::uint64_t operationTick=0;
    auto resetLane=[&](std::string lane){phase=std::move(lane);operationBefore.reset();lastExpected.reset();lastActual.reset();activeCanonicalInput.clear();operationTick=0;};
    auto prepare=[&](DeterministicSession& session,const DetTickInput* in,const DetSessionCheckpoint* expected){
        operationBefore=session.checkpoint();lastActual.reset();lastExpected=expected?std::optional<DetSessionCheckpoint>{*expected}:std::nullopt;
        activeCanonicalInput=in?canonical(*in):Bytes{};operationTick=in?in->tick:session.nextTick();};
    // Capture only the state actually available AFTER this operation. A faulted
    // Session supplies no checkpoint; a previous lane's successful state must
    // never be substituted for that missing actual witness.
    auto invoke=[&](DeterministicSession& session,auto operation){lastActual.reset();const bool valid=operation();lastActual=session.checkpoint();return valid;};
    try{
    std::vector<DetConfirmedEvent> expectedEvents;auto require=[&](bool valid,const std::string& why){if(!valid)throw CaseFailure(seed,operationTick,count,phase,why);};
    auto compare=[&](const DetSessionCheckpoint& expected,const DetSessionCheckpoint& actual,const std::string& lane){
        ++result.checkpoints;lastExpected=expected;lastActual=actual;if(encodeDetCheckpoint(expected)!=encodeDetCheckpoint(actual)){CaseFailure failure(seed,actual.nextTick,count,lane,"representative complete checkpoint mismatch");
            failure.difference=firstDetDifference(expected,actual);failure.expected=encodeDetCheckpoint(expected);failure.actual=encodeDetCheckpoint(actual);
            if(operationBefore)failure.before=encodeDetCheckpoint(*operationBefore);failure.failingInput=activeCanonicalInput;throw failure;}};
    DeterministicSession recording(sessionConfig(p));require(configure(recording,p),recording.error());rows.push_back(cp(recording,seed,count,phase));
    DetReplayWriter writer;prepare(recording,nullptr,&rows[0]);require(invoke(recording,[&]{return writer.begin(recording,(dir/"original-inputs").string(),64);}),writer.error());
    Oracle oracle(p);result.originalInputHash=result.variantInputHash=14695981039346656037ull;
    for(std::uint64_t t=0;t<count;++t){if(pulse && t%32==0)pulse(t,phase);const auto in=input(p,t);
        result.originalInputHash=digest(result.originalInputHash,canonical(in));result.variantInputHash=digest(result.variantInputHash,canonical(archiveInput(p,t)));
        prepare(recording,&in,nullptr);appendFrame(originalSubmitted,activeCanonicalInput);require(invoke(recording,[&]{return writer.advance(in);}),writer.error());++result.ticks;auto current=cp(recording,seed,count,phase);oracle.step(p);
        (void)validateTerminalBodySet(current,p,oracle,count,"representative-original-oracle",operationBefore?&*operationBefore:nullptr,activeCanonicalInput);
        compare(oracleCheckpoint(current,p,oracle),current,"representative-original-oracle");
        for(const auto& event:current.pendingEvents)expectedEvents.push_back({1,t,event});rows.push_back(std::move(current));}
    prepare(recording,nullptr,&rows.back());require(invoke(recording,[&]{return writer.finish();}),writer.error());
    resetLane("representative-original-rpl-playback");DeterministicSession playing(sessionConfig(p));require(configure(playing,p,true),playing.error());DetReplayReader reader;
    prepare(playing,nullptr,&rows[0]);
    // open() replaces the reader's Impl. Read error() only AFTER open returns:
    // argument evaluation order otherwise permits a dangling old-Impl string.
    const bool openedOriginal=invoke(playing,[&]{return reader.open(writer.path());});require(openedOriginal,reader.error());
    require(invoke(playing,[&]{return reader.restoreInitial(playing);}),reader.error());compare(rows[0],cp(playing,seed,count,phase),phase);
    while(!reader.atEnd()){const auto in=input(p,playing.nextTick());prepare(playing,&in,&rows[playing.nextTick()+1]);require(invoke(playing,[&]{return reader.advance(playing);}),reader.error());++result.ticks;compare(rows[playing.nextTick()],cp(playing,seed,count,phase),phase);}
    prepare(playing,nullptr,&rows[count/2]);require(invoke(playing,[&]{return reader.seek(playing,count/2);}),reader.error());compare(rows[playing.nextTick()],cp(playing,seed,count,phase),phase);
    while(!reader.atEnd()){const auto in=input(p,playing.nextTick());prepare(playing,&in,&rows[playing.nextTick()+1]);require(invoke(playing,[&]{return reader.advance(playing);}),reader.error());++result.ticks;compare(rows[playing.nextTick()],cp(playing,seed,count,phase),phase);}
    const std::string variant="representative-single-member-remapped-variant";
    resetLane(variant);
    DeterministicSession archiveSession(sessionConfig(p));require(configure(archiveSession,p),archiveSession.error());
    DetRollbackNetworkConfig netConfig;netConfig.sessionId=230;netConfig.localMember=netConfig.recoveryMember=1;netConfig.rollback.members={1};netConfig.rollback.historyTicks=32;
    DetRollbackReplayArchiveOptions limits;limits.maxSegmentRecords=65;limits.maxSegments=32;
    DeterministicRollbackNetwork network(archiveSession,netConfig);DetRollbackReplayWriter archive;
    prepare(archiveSession,nullptr,&rows[0]);require(invoke(archiveSession,[&]{return archive.begin(network,(dir/"single-member-remapped-variant.rpl").string(),limits,64);}),archive.error());std::vector<DetConfirmedEvent> written;
    for(std::uint64_t t=0;t<count;++t){if(pulse && t%32==0)pulse(t,variant);
        const auto in=archiveInput(p,t);prepare(archiveSession,&in,&rows[t+1]);appendFrame(variantSubmitted,activeCanonicalInput);
        require(invoke(archiveSession,[&]{return network.submitLocal(in) && network.advance();}),network.error());++result.ticks;
        require(invoke(archiveSession,[&]{return archive.sync(network);}),archive.error());
        compare(rows[t+1],cp(archiveSession,seed,count,variant),variant);auto events=network.takeConfirmedEvents();written.insert(written.end(),events.begin(),events.end());}
    prepare(archiveSession,nullptr,&rows.back());require(invoke(archiveSession,[&]{return archive.finish(network);}),archive.error());require(written==expectedEvents,"remapped representative exact confirmed effects");
    const auto report=DetRollbackReplayArchive::inspect(archive.path());require(report.valid && !report.recovery && report.files.size()>1,"actual sealed representative segmented index");result.files=report.files.size();
    resetLane(variant+"-playback");DeterministicSession replay(sessionConfig(p));require(configure(replay,p,true),replay.error());DetRollbackReplayReader ar;
    prepare(replay,nullptr,&rows[0]);const bool openedVariant=invoke(replay,[&]{return ar.open(archive.path());});require(openedVariant,ar.error());
    require(invoke(replay,[&]{return ar.restoreInitial(replay);}),ar.error());std::vector<DetConfirmedEvent> actualEvents;
    while(!ar.atEnd()){const auto in=archiveInput(p,replay.nextTick());prepare(replay,&in,&rows[replay.nextTick()+1]);require(invoke(replay,[&]{return ar.advance(replay);}),ar.error());++result.ticks;compare(rows[replay.nextTick()],cp(replay,seed,count,variant),variant);
        auto events=ar.takeConfirmedEvents();actualEvents.insert(actualEvents.end(),events.begin(),events.end());}
    require(actualEvents==expectedEvents,"actual .rpi replay exactly-once event identities");result.events=actualEvents.size();
    for(auto seek:{std::uint64_t{65},std::uint64_t{1},std::uint64_t(count-1)}){prepare(replay,nullptr,&rows[seek]);require(invoke(replay,[&]{return ar.seek(replay,1,seek);}),ar.error());compare(rows[seek],cp(replay,seed,count,variant),variant);require(ar.takeConfirmedEvents().empty(),"silent cross-segment seek effects");
        while(!ar.atEnd()){const auto in=archiveInput(p,replay.nextTick());prepare(replay,&in,&rows[replay.nextTick()+1]);require(invoke(replay,[&]{return ar.advance(replay);}),ar.error());++result.ticks;compare(rows[replay.nextTick()],cp(replay,seed,count,variant),variant);(void)ar.takeConfirmedEvents();}}
    std::ostringstream hashes;for(const auto& row:rows)hashes<<row.nextTick<<' '<<detCheckpointHash(row)<<'\n';write(dir/"tick-hashes.txt",hashes.str());
    std::ostringstream metadata;metadata<<"{\"schema\":1,\"status\":\"passed\",\"seed\":"<<decimal(seed)<<",\"ticks\":"<<count
        <<",\"originalCanonicalInputFNV64\":"<<decimal(result.originalInputHash)<<",\"variantCanonicalInputFNV64\":"<<decimal(result.variantInputHash)
        <<",\"sameCanonicalInputs\":false,\"sameFullCheckpointExecution\":true,\"exactConfirmedEffects\":true,\"variant\":\"single-member source/sequence remap; roles selected by type\""
        <<",\"transportAuthentication\":false,\"originalRpl\":"<<quote(writer.path())<<",\"variantRpi\":"<<quote(archive.path())<<",\"physicalSegments\":"<<decimal(result.files)
        <<",\"originalSubmittedInputs\":\"original-submitted-canonical-frames.bin\",\"variantSubmittedInputs\":\"variant-submitted-canonical-frames.bin\",\"inputFrameFormat\":\"repeated uint32 little-endian length plus canonical encodeDetInput bytes\"}\n";
    write(dir/"original-submitted-canonical-frames.bin",{reinterpret_cast<const char*>(originalSubmitted.data()),originalSubmitted.size()});
    write(dir/"variant-submitted-canonical-frames.bin",{reinterpret_cast<const char*>(variantSubmitted.data()),variantSubmitted.size()});
    write(dir/"sample.json",metadata.str());return result;
    }catch(CaseFailure& e){e.submittedOriginalInputs=std::move(originalSubmitted);e.submittedVariantInputs=std::move(variantSubmitted);
        if(e.expected.empty() && lastExpected)e.expected=encodeDetCheckpoint(*lastExpected);if(e.actual.empty() && lastActual)e.actual=encodeDetCheckpoint(*lastActual);
        if(e.before.empty() && operationBefore)e.before=encodeDetCheckpoint(*operationBefore);if(e.failingInput.empty())e.failingInput=activeCanonicalInput;throw;
    }catch(const std::exception& error){CaseFailure e(seed,operationTick,count,phase,error.what());e.submittedOriginalInputs=std::move(originalSubmitted);e.submittedVariantInputs=std::move(variantSubmitted);
        if(lastExpected)e.expected=encodeDetCheckpoint(*lastExpected);if(lastActual)e.actual=encodeDetCheckpoint(*lastActual);
        if(operationBefore)e.before=encodeDetCheckpoint(*operationBefore);e.failingInput=activeCanonicalInput;throw e;}
}
struct Totals {std::uint64_t cases=0,ticks=0,checkpoints=0,events=0,restored=0,rollbacks=0,replayed=0,duplicates=0,lastSeed=0,lastHash=0,lastTicks=0,
        jumps=0,buffered=0,coyote=0,drops=0,crouch=0,spawns=0,despawns=0,terminalBodyChecks=0,samples=0,samplesSkipped=0,replayTicks=0,replayChecks=0,replayEvents=0,replayFiles=0;
    void add(const CaseResult& r){++cases;ticks+=r.ticks;checkpoints+=r.checkpointsChecked;events+=r.eventsChecked;restored+=r.restoredTicks;rollbacks+=r.rollbacks;replayed+=r.replayedTicks;duplicates+=r.duplicates;lastSeed=r.seed;lastHash=r.finalHash;lastTicks=r.ticks;
        jumps+=r.jumps;buffered+=r.buffered;coyote+=r.coyote;drops+=r.drops;crouch+=r.crouchChanges;spawns+=r.spawns;despawns+=r.despawns;terminalBodyChecks+=r.terminalBodyChecks;}
    void add(const ReplayResult& r){++samples;replayTicks+=r.ticks;replayChecks+=r.checkpoints;replayEvents+=r.events;replayFiles+=r.files;}};
std::string status(const Options& o,const Totals& t,const BoundaryResult& boundary,Clock::time_point start,std::string_view state,std::string_view phase,std::uint64_t activeSeed,std::uint64_t activeTick,const fs::path& run){
    const auto elapsed=std::uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now()-start).count());
    std::ostringstream out;out<<"{\"schema\":1,\"status\":"<<quote(state)<<",\"phase\":"<<quote(phase)<<",\"baseSeed\":"<<decimal(o.seed)<<",\"lastSeed\":"<<decimal(t.lastSeed)<<",\"nextSeed\":"<<decimal(o.seed+t.cases)
        <<",\"activeSeed\":"<<decimal(activeSeed)<<",\"activeTick\":"<<decimal(activeTick)<<",\"requestedSeconds\":"<<decimal(o.seconds)<<",\"minimumCases\":"<<decimal(o.cases)<<",\"elapsedMs\":"<<decimal(elapsed)
        <<",\"cases\":"<<decimal(t.cases)<<",\"ticks\":"<<decimal(t.ticks)<<",\"checkpointsChecked\":"<<decimal(t.checkpoints)<<",\"eventsChecked\":"<<decimal(t.events)<<",\"restoredTicks\":"<<decimal(t.restored)
        <<",\"rollbacks\":"<<decimal(t.rollbacks)<<",\"replayedTicks\":"<<decimal(t.replayed)<<",\"duplicates\":"<<decimal(t.duplicates)<<",\"lastHash\":"<<decimal(t.lastHash)
        <<",\"terminalBodyChecks\":"<<decimal(t.terminalBodyChecks)<<",\"terminalBodyChecksScope\":\"live baseline only; actual original-input recorder also validates the set without adding to this count\",\"oracleScope\":\"hand-derived motion, controller, stats and events; exact first-terminal-contact bodyY is incoming or +0; complete canonical CP bytes compare actual lanes, not an independent full-byte golden\""
        <<",\"features\":{\"jumps\":"<<decimal(t.jumps)<<",\"buffered\":"<<decimal(t.buffered)<<",\"coyote\":"<<decimal(t.coyote)<<",\"drops\":"<<decimal(t.drops)<<",\"crouchChanges\":"<<decimal(t.crouch)<<",\"spawns\":"<<decimal(t.spawns)<<",\"despawns\":"<<decimal(t.despawns)<<'}'
        <<",\"longBoundaries\":{\"requested\":"<<(o.longWindows?"true":"false")<<",\"templates\":"<<decimal(boundary.templates)<<",\"ticks\":"<<decimal(boundary.ticks)<<",\"checkpoints\":"<<decimal(boundary.checkpoints)<<",\"lastHash\":"<<decimal(boundary.lastHash)<<'}'
        <<",\"replayRepresentatives\":{\"passed\":"<<decimal(t.samples)<<",\"limit\":2,\"skippedAfterLimit\":"<<decimal(t.samplesSkipped)<<",\"ticks\":"<<decimal(t.replayTicks)<<",\"checkpoints\":"<<decimal(t.replayChecks)<<",\"events\":"<<decimal(t.replayEvents)<<",\"archivePhysicalSegments\":"<<decimal(t.replayFiles)
        <<",\"variant\":\"original-input .rpl; separate single-member-remapped .rpi\",\"transportAuthentication\":false}"
        <<",\"countsAreUnique\":false,\"output\":"<<quote(run.generic_string())<<"}\n";return out.str();}
std::string failure(const CaseFailure& e,const fs::path& exe){std::ostringstream out;out<<"{\"schema\":1,\"status\":\"failed\",\"seed\":"<<decimal(e.seed)<<",\"tick\":"<<decimal(e.tick)<<",\"requestedTicks\":"<<e.requestedTicks<<",\"phase\":"<<quote(e.phase)<<",\"error\":"<<quote(e.what())<<",\"executable\":"<<quote(exe.generic_string())
        <<",\"recipe\":[\"--seed\","<<decimal(e.seed)<<",\"--cases\",\"1\",";
    if(e.phase.starts_with("long-"))out<<"\"--long-boundaries\",";else if(e.requestedTicks>=192 && e.requestedTicks<=1024)out<<"\"--ticks\","<<quote(std::to_string(e.requestedTicks))<<',';
    out<<"\"--output\",\"EXISTING_NEW_PARENT_DIRECTORY\"]";
    if(e.inputMember)out<<",\"inputMember\":"<<*e.inputMember;
    if(e.difference){const auto& d=*e.difference;out<<",\"firstDifference\":{\"tick\":"<<decimal(d.tick)<<",\"section\":"<<quote(d.section)<<",\"entity\":"<<decimal(d.entity)<<",\"component\":"<<d.component<<",\"field\":"<<d.field<<",\"lane\":"<<d.lane<<",\"expected\":"<<decimal(d.expected)<<",\"actual\":"<<decimal(d.actual)<<'}';}
    out<<",\"witnessScope\":\"full-case recipe; checkpoint witnesses are not independent execution authentication or guaranteed one-step reproduction\",\"artifacts\":{\"before\":"<<(e.before.empty()?"null":quote("before.checkpoint"))<<",\"expected\":"<<(e.expected.empty()?"null":quote("expected.checkpoint"))
        <<",\"actual\":"<<(e.actual.empty()?"null":quote("actual.checkpoint"))<<",\"canonicalInput\":"<<(e.failingInput.empty()?"null":quote("input.bin"))
        <<",\"submittedOriginalInputs\":"<<(e.submittedOriginalInputs.empty()?"null":quote("original-submitted-canonical-frames.bin"))<<",\"submittedVariantInputs\":"<<(e.submittedVariantInputs.empty()?"null":quote("variant-submitted-canonical-frames.bin"))
        <<"},\"inputFrameFormat\":\"repeated uint32 little-endian length plus canonical encodeDetInput bytes; submitted frames, not proof of accepted execution\"}\n";return out.str();}
void saveFailure(const fs::path& run,const CaseFailure& e,const fs::path& exe){binary(run/"before.checkpoint",e.before);binary(run/"expected.checkpoint",e.expected);binary(run/"actual.checkpoint",e.actual);binary(run/"input.bin",e.failingInput);binary(run/"original-submitted-canonical-frames.bin",e.submittedOriginalInputs);binary(run/"variant-submitted-canonical-frames.bin",e.submittedVariantInputs);write(run/"failure.json",failure(e,exe));}
} // namespace
int main(int argc,char** argv){Options args;try{args=options(argc,argv);}catch(const std::exception& e){std::cerr<<e.what()<<"\nUsage: --output EXISTING_PARENT [--seed UINT64] [--cases1..1000000] [--seconds0..86400] [--ticks192..1024] [--long-boundaries]\n";return 2;}
    fs::path run;const auto start=Clock::now();const auto deadline=start+std::chrono::seconds(args.seconds);auto nextUpdate=start;
    Totals totals;BoundaryResult boundaries;std::uint64_t activeSeed=args.seed,activeTick=0;std::string phase="configuration";
    auto emit=[&](std::string_view state){const auto text=status(args,totals,boundaries,start,state,phase,activeSeed,activeTick,run);writeProgress(run/"progress.json",text);std::cout<<text<<std::flush;};
    Pulse pulse=[&](std::uint64_t tick,std::string_view lane){activeTick=tick;phase=std::string(lane);if(Clock::now()>=nextUpdate){emit("running");nextUpdate=Clock::now()+std::chrono::seconds(15);}};
    try{run=fresh(args.output);emit("running");nextUpdate=Clock::now()+std::chrono::seconds(15);phase="expected-crush-template";expectedCrush(args.seed);
        if(args.longWindows){boundaries=longBoundaries(pulse);std::ostringstream report;report<<"{\"schema\":1,\"status\":\"passed\",\"scope\":\"seven streamed65535-boundary templates only\",\"wholeCampaignCompleted\":false,\"templates\":"<<decimal(boundaries.templates)<<",\"ticks\":"<<decimal(boundaries.ticks)<<",\"checkpoints\":"<<decimal(boundaries.checkpoints)<<",\"lastHash\":"<<decimal(boundaries.lastHash)<<"}\n";write(run/"long-boundaries.json",report.str());}
        while(totals.cases<args.cases || Clock::now()<deadline){activeSeed=args.seed+totals.cases;activeTick=0;phase="seeded-case";
            const auto result=runCase(activeSeed,args.ticks,pulse);totals.add(result);
            if(totals.samples<2){const auto dir=run/("replay-sample-"+std::to_string(totals.samples));if(!fs::create_directory(dir))throw std::runtime_error("Cannot create fresh representative directory");
                totals.add(replayRepresentative(activeSeed,std::uint32_t(result.ticks),dir,pulse));}
            else ++totals.samplesSkipped;
            if(Clock::now()>=nextUpdate){emit("running");nextUpdate=Clock::now()+std::chrono::seconds(15);}}
        phase="complete";activeTick=totals.lastTicks;const auto text=status(args,totals,boundaries,start,"passed",phase,activeSeed,activeTick,run);write(run/"summary.json",text);writeProgress(run/"progress.json",text);std::cout<<text;return 0;
    }catch(const CaseFailure& e){try{if(!run.empty()){saveFailure(run,e,fs::absolute(argv[0]));phase=e.phase;activeSeed=e.seed;activeTick=e.tick;emit("failed");}}catch(const std::exception& artifact){std::cerr<<artifact.what()<<'\n';return 3;}std::cerr<<failure(e,fs::absolute(argv[0]));return 1;
    }catch(const std::exception& e){try{if(!run.empty()){CaseFailure error(activeSeed,activeTick,args.ticks?args.ticks:256,phase,e.what());saveFailure(run,error,fs::absolute(argv[0]));emit("failed");}}catch(const std::exception& artifact){std::cerr<<artifact.what()<<'\n';return 3;}std::cerr<<e.what()<<'\n';return 1;}}
