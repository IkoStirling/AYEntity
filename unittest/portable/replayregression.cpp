// design reference: AYEntity/design.md Stage19; actual parser/executor/repro tests.
#include "../DetTypedSessionScenario.h"
#include "../../src/detail/DetSessionWire.h"
#include <AYEntity/DeterministicReplayRegression.h>
#include <AYReplay/FileReplayRecorder.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <chrono>
using namespace ayt::entity;
namespace fs=std::filesystem;
namespace {
using Result=DetReplayRegressionResult;
void require(bool ok,const std::string& message){if(!ok)throw std::runtime_error(message);}
auto factory(bool fault=false,std::uint32_t version=1) {
    return [=]{auto s=std::make_unique<DeterministicSession>(DetSessionConfig{version,1,1,64,0});
        require(dettyped_scenario::configure(*s,true,fault,true),s->error());return s;};
}
DetTickInput input(unsigned tick){auto in=detsession_scenario::input(tick);std::erase_if(in.commands,[](const auto& c){return c.source!=1;});return in;}
std::vector<std::uint8_t> read(const fs::path& path){std::ifstream in(path,std::ios::binary);require(bool(in),"read test artifact");return {std::istreambuf_iterator<char>(in),std::istreambuf_iterator<char>()};}
void write(const fs::path& path,const std::vector<std::uint8_t>& bytes){std::ofstream out(path,std::ios::binary);out.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());require(bool(out),"write test artifact");}
std::string record(const fs::path& dir,unsigned changeInput) {
    require(fs::create_directory(dir),"fresh source directory");auto s=factory()();
    DetRollbackNetworkConfig cfg;cfg.sessionId=180;cfg.localMember=1;cfg.recoveryMember=1;cfg.rollback.members={1};cfg.rollback.historyTicks=16;cfg.rollback.maxPredictionTicks=4;
    DeterministicRollbackNetwork network(*s,cfg);DetRollbackReplayWriter writer;DetRollbackReplayArchiveOptions o;o.maxSegmentRecords=17;
    require(writer.begin(network,(dir/"sample.rpl").string(),o,13),writer.error());
    for(unsigned tick=0;tick<128;++tick) {
        if(tick==64){require(network.beginRecovery(2),network.error());require(writer.sync(network),writer.error());}
        auto in=input(tick);if(changeInput && tick==45){require(!in.commands.empty(),"input command at45");
            if(changeInput==1)++in.commands.front().type;else ++in.commands.front().payload.front();}
        require(network.submitLocal(std::move(in)) && network.advance(),network.error());require(writer.sync(network),writer.error());(void)network.takeConfirmedEvents();
    }
    require(writer.finish(network),writer.error());return writer.path();
}
// Deliberately valid/checksummed witnesses: semantic divergence cannot be hidden
// behind parser/checksum errors. Reuses the documented schema2 wire contract.
std::string shortFile(const fs::path& base,const DetSessionCheckpoint& before,const DetSessionCheckpoint& after,bool gap,const std::vector<DetConfirmedEvent>& repair={}) {
    using namespace ayt::entity::detwire;
    ayt::replay::FileReplayRecorder file(base.string());ayt::replay::ReplayFileHeader h{};
    h.magic=ayt::replay::kReplayMagic;h.version=ayt::replay::kReplayVersion;h.schemaVersion=2;
    require(file.beginSession(h),"short fixture begin");
    auto header=[](unsigned kind,unsigned epoch){Writer w;w.u32(0x52524441);w.u32(1);w.u32(kind);w.u32(epoch);return w;};
    auto emit=[&](Writer w,std::uint64_t tick){const auto b=w.finish();require(file.recordEvent(tick,0x20006,b.data(),b.size()),"short fixture record");};
    auto initial=header(0,1);initial.blob(encodeDetCheckpoint(before));emit(std::move(initial),before.nextTick);
    auto step=header(gap?2:1,gap?2:1);
    if(gap)step.u64(before.nextTick);else{step.u32(1);step.blob(encodeDetInput(input(static_cast<unsigned>(before.nextTick))));}
    step.blob(encodeDetCheckpoint(after));if(gap){step.u32(static_cast<unsigned>(repair.size()));for(const auto& e:repair){step.u32(e.epoch);step.u64(e.tick);command(step,e.command);}}emit(std::move(step),after.nextTick);
    auto seal=header(3,gap?2:1);seal.u64(after.nextTick);seal.u64(after.nextTick);seal.u64(detCheckpointHash(after));seal.u32(2);emit(std::move(seal),after.nextTick);
    require(file.flush() && file.endSession(),"short fixture seal");return ayt::replay::FileReplayRecorder::rotationPathFor(base.string(),0);
}
void save(const DetReplayRegressionReport& report,const fs::path& dir){std::string error;require(DetReplayRegression::writeArtifacts(report,dir.string(),error),error);require(fs::exists(dir/"report.json"),"sealed report publication");}
}
int main(int argc,char** argv){try {
    require(argc==3,"fixture and artifact root required");const std::string fixture=argv[1];const fs::path root=argv[2];fs::create_directories(root);
    const auto work=root/("api-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));require(fs::create_directory(work),"fresh test workspace");
    const auto good=DetReplayRegression::verify(fixture,factory());
    require(good.result==Result::Verified && good.ticksExecuted==128 && good.recordsChecked==130,"full Windows fixture execution");save(good,work/"verified");
    const auto changedLayout=record(work/"layout",false);const auto equal=DetReplayRegression::compare(fixture,changedLayout);
    require(equal.result==Result::Equal && equal.recordsChecked==130,"semantic equality across different physical layout/restart interval");save(equal,work/"equal");
    const auto changedInput=record(work/"input",true);require(DetReplayRegression::verify(changedInput,factory()).success(),"changed canonical input is executable");
    const auto differentInput=DetReplayRegression::compare(fixture,changedInput);
    require(differentInput.result==Result::InputMismatch && differentInput.tick==45 && differentInput.epoch==1 && differentInput.inputDifference && differentInput.inputDifference->field=="type" && differentInput.inputDifference->source==1,"first input command difference despite same state semantics");
    {auto last=factory()();DetRollbackReplayReader reader;require(reader.open(changedInput) && reader.seek(*last,2,128),reader.error());
        require(encodeDetCheckpoint(*last->checkpoint())==read(fs::path(fixture).parent_path()/"expected.state"),"different input can converge to identical final state");}
    save(differentInput,work/"input-difference");
    require(DetReplayRegression::compare((work/"input-difference/left_000.rpl").string(),(work/"input-difference/right_000.rpl").string()).result==Result::InputMismatch,"short two-sided input repro");
    const auto payloadChanged=record(work/"payload",2);const auto payload=DetReplayRegression::compare(fixture,payloadChanged);
    require(payload.result==Result::InputMismatch && payload.inputDifference && payload.inputDifference->field=="payload" && payload.inputDifference->payloadOffset==0,"first input byte precedes downstream state divergence");save(payload,work/"payload-difference");
    const auto fault=DetReplayRegression::verify(fixture,factory(true));
    require(fault.result==Result::StateMismatch && fault.tick==0 && fault.nextTick==1 && fault.difference && !fault.actualCheckpoint.empty(),"exact execution field divergence");
    save(fault,work/"state-failure");
    const auto rerun=DetReplayRegression::verify((work/"state-failure/left_000.rpl").string(),factory(true));
    require(rerun.result==fault.result && rerun.ticksExecuted==1 && rerun.difference->component==fault.difference->component && rerun.difference->field==fault.difference->field,"independent one-step execution repro");
    std::string error;require(!DetReplayRegression::writeArtifacts(fault,(work/"state-failure").string(),error),"existing output refused");
    auto tampered=fault;++tampered.leftRecordHash;require(!DetReplayRegression::writeArtifacts(tampered,(work/"tampered").string(),error),"source/report record identity checked");
    tampered=fault;++tampered.leftBeforeRecordHash;require(!DetReplayRegression::writeArtifacts(tampered,(work/"tampered-before").string(),error),"preceding checkpoint context identity checked");
    require(DetReplayRegression::verify(fixture,factory(false,2)).result==Result::ManifestMismatch,"application version rejected before execution");
    require(DetReplayRegression::verify(fixture,{}).result==Result::ExecutionFailed,"missing application factory");
    DetReplayRegressionOptions budget;budget.maxRecords=2;
    require(DetReplayRegression::verify(fixture,factory(),budget).result==Result::BudgetExceeded,"execute record budget");
    require(DetReplayRegression::compare(fixture,fixture,budget).result==Result::BudgetExceeded,"compare record budget");
    budget={};budget.maxArtifactBytes=1024;require(!DetReplayRegression::writeArtifacts(fault,(work/"small-budget").string(),error,budget),"repro byte budget");
    const auto recovered=DetRollbackReplayArchive::rebuildIndex(fixture,(work/"recovered").string());require(recovered.valid,"profile2 fixture");
    const auto recoveredReport=DetReplayRegression::verify(recovered.path,factory());require(recoveredReport.success() && recoveredReport.leftRecovery,"recovered-prefix remains marked after verification");
    const auto samePrefix=DetReplayRegression::compare(fixture,recovered.path);require(samePrefix.result==Result::Equal && samePrefix.rightRecovery && !samePrefix.leftRecovery,"semantic equality preserves independent source completion status");
    auto session=factory()();const auto before=*session->checkpoint();require(session->advance(input(0)),session->error());const auto normal=*session->checkpoint();
    const auto single=shortFile(work/"single.rpl",before,normal,false);
    auto event=normal;require(!event.pendingEvents.empty() && !event.pendingEvents.front().payload.empty(),"event fixture payload");++event.pendingEvents.front().payload.front();
    const auto events=shortFile(work/"events.rpl",before,event,false);
    const auto eventFailure=DetReplayRegression::verify(events,factory());require(eventFailure.result==Result::EventMismatch && eventFailure.difference->section=="events","event payload divergence");save(eventFailure,work/"event-failure");
    require(DetReplayRegression::verify((work/"event-failure/left_000.rpl").string(),factory()).result==Result::EventMismatch,"event repro reruns");
    require(DetReplayRegression::compare(single,events).result==Result::EventMismatch,"recorded event difference");
    require(DetReplayRegression::compare(single,fixture).result==Result::RangeMismatch,"different recording ranges");
    auto initial=before;++initial.globals.at(3).at(0);const auto differentStart=shortFile(work/"initial.rpl",initial,normal,false);
    require(DetReplayRegression::compare(single,differentStart).result==Result::InitialStateMismatch,"initial state mismatch distinct from executing equal inputs");
    auto versioned=factory(false,2)();const auto versionBefore=*versioned->checkpoint();require(versioned->advance(input(0)),versioned->error());
    const auto incompatible=shortFile(work/"version.rpl",versionBefore,*versioned->checkpoint(),false);
    const auto versions=DetReplayRegression::compare(single,incompatible);require(versions.result==Result::ManifestMismatch,"recorded code/application version incompatibility");save(versions,work/"version-difference");
    require(fs::exists(work/"version-difference/right-manifest.bin"),"both incompatible version manifests preserved");
    auto badSession=factory(true)();require(badSession->advance(input(0)),badSession->error());
    const auto wrong=shortFile(work/"wrong.rpl",before,*badSession->checkpoint(),false);
    const auto states=DetReplayRegression::compare(single,wrong);require(states.result==Result::StateMismatch,"same input recorded state difference");save(states,work/"recorded-state-difference");
    require(session->advance(input(1)) && session->advance(input(2)),session->error());const auto gap=shortFile(work/"gap.rpl",before,*session->checkpoint(),true);
    const auto gapReport=DetReplayRegression::verify(gap,factory());require(gapReport.result==Result::VerifiedWithGaps && gapReport.skippedTicks==3 && !gapReport.ticksExecuted,"trusted recovery gap never simulated");save(gapReport,work/"trusted-gap");
    require(DetReplayRegression::compare(gap,gap).skippedTicks==3,"equal recordings still declare trusted gaps");
    require(DetReplayRegression::compare(single,gap).result==Result::RecoveryMismatch,"recovery shape distinct from same-input divergence");
    const auto repairedGap=shortFile(work/"gap-repair.rpl",before,*session->checkpoint(),true,{{1,1,{10,0,1,{42}}}});
    const auto trusted=DetReplayRegression::verify(repairedGap,factory());require(trusted.result==Result::VerifiedWithGaps && trusted.trustedRecoveryEvents==1 && !trusted.eventsChecked,"recovery event repair is trusted, not executed event evidence");
    const auto repairDifference=DetReplayRegression::compare(gap,repairedGap);require(repairDifference.result==Result::EventMismatch,"first recovery event difference");save(repairDifference,work/"repair-difference");
    auto tiny=[](bool fault){auto s=std::make_unique<DeterministicSession>(DetSessionConfig{99,1,1,64,0});
        require(s->registerSystem(1,0,[fault](DetTickContext&){if(fault)throw std::runtime_error("Injected callback failure");return true;}) && s->seal(),s->error());return s;};
    auto tinyGood=tiny(false);const auto tinyBefore=*tinyGood->checkpoint();require(tinyGood->advance(input(0)),tinyGood->error());
    const auto faultSource=shortFile(work/"callback.rpl",tinyBefore,*tinyGood->checkpoint(),false);
    const auto callback=DetReplayRegression::verify(faultSource,[&]{return tiny(true);});require(callback.result==Result::ExecutionFailed && callback.ticksExecuted==1 && callback.actualCheckpoint.empty(),"callback faults distinct from a comparable divergent checkpoint");save(callback,work/"callback-failure");
    require(DetReplayRegression::verify((work/"callback-failure/left_000.rpl").string(),[&]{return tiny(true);}).result==Result::ExecutionFailed,"callback failure one-step repro");
    auto broken=read(single);broken.push_back(0);write(work/"broken.rpl",broken);
    const auto invalid=DetReplayRegression::verify((work/"broken.rpl").string(),factory());require(invalid.result==Result::InvalidFile && invalid.issue,"corrupt file distinct from Sim divergence");save(invalid,work/"invalid-file");
    std::cout<<"PASS regression: fixed Windows execution128 ticks, layout-independent compare, first input/state/event divergence, one-step repro rerun, manifest/gap/range distinctions, immutable source identity and budgets; artifacts="<<work.string()<<'\n';return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
