#include <AYEntity/DeterministicRollbackReplay.h>
#include <AYEntity/DeterministicReplayRegression.h>
#include <AYPlatform/ChildProcess.h>
#include "detail/RunnerReport.h"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <fstream>
using namespace ayt::entity;
namespace {
std::string quote(const std::string& value) {
    return replay_tool::quoteRunnerBytes(value);
}
void print(const DetReplayArchiveReport& r) {
    std::cout<<"{\"valid\":"<<(r.valid?"true":"false")<<",\"state\":"
        <<quote(r.valid?(r.recovery?"recovered-prefix":"complete"):"unusable")<<",\"path\":"<<quote(r.path)
        <<",\"error\":"<<quote(r.error)<<",\"retainedSealHead\":"<<r.speculativeHead<<",\"recovery\":";
    if(r.recovery)std::cout<<"{\"reason\":"<<static_cast<unsigned>(r.recovery->reason)<<",\"stopOrdinal\":"<<r.recovery->stopOrdinal<<'}';else std::cout<<"null";
    std::cout<<",\"issue\":";
    if(r.issue){std::cout<<"{\"path\":"<<quote(r.issue->path)<<",\"offset\":";if(r.issue->offset)std::cout<<*r.issue->offset;else std::cout<<"null";std::cout<<'}';}
    else std::cout<<"null";
    std::cout<<",\"files\":[";
    for(std::size_t i=0;i<r.files.size();++i){const auto& f=r.files[i];if(i)std::cout<<',';
        std::cout<<"{\"ordinal\":"<<i<<",\"path\":"<<quote(f.path)<<",\"storedBytes\":"<<f.storedBytes
            <<",\"fileHash\":"<<quote(std::to_string(f.fileHash))<<",\"firstEpoch\":"<<f.firstEpoch<<",\"firstTick\":"<<f.firstTick
            <<",\"lastEpoch\":"<<f.lastEpoch<<",\"endTick\":"<<f.endTick<<",\"records\":"<<f.records
            <<",\"initialHash\":"<<quote(std::to_string(f.initialHash))<<",\"finalHash\":"<<quote(std::to_string(f.finalHash))<<'}';}
    std::cout<<"],\"epochs\":[";
    for(std::size_t i=0;i<r.segments.size();++i){const auto& s=r.segments[i];if(i)std::cout<<',';
        std::cout<<"{\"epoch\":"<<s.epoch<<",\"firstTick\":"<<s.firstTick<<",\"endTick\":"<<s.endTick<<",\"skippedTicks\":"<<s.skippedTicks<<'}';}
    std::cout<<"]}\n";
}
}
int main(int argc,char** argv) {
    if(argc<3){std::cerr<<"usage: ayreplay inspect|scan SOURCE\n       ayreplay recover|rebuild SOURCE NEW_DIRECTORY\n       ayreplay compare LEFT RIGHT NEW_DIRECTORY\n       ayreplay verify SOURCE RUNNER NEW_DIRECTORY [TIMEOUT_MS]\n       ayreplay reproduce PACKAGE_DIRECTORY RUNNER NEW_DIRECTORY [TIMEOUT_MS]\n";return 2;}
    try {
        const std::string command=argv[1];DetReplayArchiveReport report;
        if(command=="compare" && argc==5) {
            const auto result=DetReplayRegression::compare(argv[2],argv[3]);std::string error;
            if(!DetReplayRegression::writeArtifacts(result,argv[4],error)){std::cerr<<error<<'\n';std::cout<<DetReplayRegression::toJson(result)<<'\n';return 3;}
            std::cout<<DetReplayRegression::toJson(result)<<'\n';return result.success()?0:1;
        }
        if((command=="verify" || command=="reproduce") && (argc==5 || argc==6)) {
            std::uint64_t timeout=300000;
            if(argc==6){try{std::size_t parsed=0;const std::string value=argv[5];timeout=std::stoull(value,&parsed);
                if(parsed!=value.size() || timeout<1 || timeout>3600000)throw std::runtime_error("range");}
                catch(const std::exception&){std::cerr<<"Timeout must be 1..3600000 milliseconds\n";return 2;}}
            namespace fs=std::filesystem;fs::path source=argv[2];
            if(fs::exists(argv[4]))throw std::runtime_error("Runner output directory must be new");
            if(command=="reproduce"){source/="left_000.rpl";if(fs::is_symlink(source) || !fs::is_regular_file(source))throw std::runtime_error("Package has no regular left_000.rpl executable case");}
            ayt::platform::ProcessOptions options;options.executable=fs::absolute(argv[3]);
            options.arguments={"--ayreplay-verify",fs::absolute(source).string(),fs::absolute(argv[4]).string()};
            options.timeout=std::chrono::milliseconds(timeout);options.outputLimit=1024*1024;
            const auto reportPath=fs::absolute(argv[4])/"report.json";
            const auto process=ayt::platform::runProcess(options);
            auto stored=replay_tool::readRunnerReport(reportPath,options.arguments[1],process.exitCode);
            const bool reportPresent=stored.present;auto protocol=std::move(stored.protocol);
            auto reject=[&](std::string error){protocol.valid=false;protocol.error=std::move(error);};
            if(process.timedOut)reject("Runner deadline expired");
            else if(process.cancelled)reject("Runner was cancelled");
            else if(!process.launched || !process.error.empty())reject(process.error.empty()?"Runner was not launched":process.error);
            else if(process.outputTruncated)reject("Runner output exceeded capture budget");
            std::cout<<"{\"schema\":1,\"runner\":"<<quote(options.executable.string())<<",\"launched\":"<<(process.launched?"true":"false")
                <<",\"timedOut\":"<<(process.timedOut?"true":"false")<<",\"outputTruncated\":"<<(process.outputTruncated?"true":"false")
                <<",\"exitCode\":"<<process.exitCode<<",\"reportPresent\":"<<(reportPresent?"true":"false")<<",\"reportPath\":"<<quote(reportPath.string())
                <<",\"protocolValid\":"<<(protocol.valid?"true":"false")<<",\"protocolError\":"<<quote(protocol.error)
                <<",\"error\":"<<quote(process.error)<<",\"runnerOutput\":"<<quote(process.output)<<"}\n";
            return process.launched && !process.timedOut && !process.cancelled && !process.outputTruncated && process.error.empty() && protocol.valid && protocol.success && process.exitCode==0?0:1;
        }
        if(command=="inspect" && argc==3)report=DetRollbackReplayArchive::inspect(argv[2]);
        else if(command=="scan" && argc==3)report=DetRollbackReplayArchive::scan(argv[2]);
        else if((command=="recover" || command=="rebuild") && argc==4)report=DetRollbackReplayArchive::rebuildIndex(argv[2],argv[3]);
        else {std::cerr<<"Invalid command/argument count\n";return 2;}
        print(report);return report.valid?0:1;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
