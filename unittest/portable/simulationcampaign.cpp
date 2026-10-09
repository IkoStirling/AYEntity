// design reference: AYEntity/design.md Stage20; bounded multi-seed simulation campaigns.
#include "../DetSimulationCampaign.h"
#include <charconv>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>

namespace {
using namespace detsimulation_campaign;
namespace fs=std::filesystem;
using Clock=std::chrono::steady_clock;
struct Options {std::uint64_t seed=1,cases=8,seconds=0;std::uint32_t ticks=0;fs::path output;bool contextOnly=false;};
std::string quote(std::string_view value){
    std::ostringstream out;out<<'"';constexpr char digits[]="0123456789abcdef";
    for(unsigned char c:value){if(c=='"' || c=='\\')out<<'\\'<<c;else if(c<32)out<<"\\u00"<<digits[c>>4]<<digits[c&15];else out<<c;}
    out<<'"';return out.str();
}
std::uint64_t number(std::string_view text,std::uint64_t lower,std::uint64_t upper,const char* name){
    std::uint64_t value=0;const auto [end,error]=std::from_chars(text.data(),text.data()+text.size(),value);
    if(text.empty() || error!=std::errc{} || end!=text.data()+text.size() || value<lower || value>upper)
        throw std::invalid_argument(std::string("Invalid ")+name);return value;
}
Options options(int argc,char** argv){
    Options result;
    for(int i=1;i<argc;++i){
        const std::string_view argument=argv[i];
        if(argument=="--context-only"){result.contextOnly=true;continue;}
        if(argument!="--seed" && argument!="--cases" && argument!="--seconds" && argument!="--ticks" && argument!="--output")
            throw std::invalid_argument("Unknown campaign option: "+std::string(argument));
        if(++i==argc)throw std::invalid_argument("Missing campaign option value");const std::string_view value=argv[i];
        if(argument=="--seed")result.seed=number(value,0,std::numeric_limits<std::uint64_t>::max(),"seed");
        if(argument=="--cases")result.cases=number(value,1,1000000,"cases (1..1000000)");
        if(argument=="--seconds")result.seconds=number(value,0,86400,"seconds (0..86400)");
        if(argument=="--ticks")result.ticks=static_cast<std::uint32_t>(number(value,16,8192,"ticks (16..8192)"));
        if(argument=="--output"){if(value.empty())throw std::invalid_argument("Empty output path");result.output=fs::path(argv[i]);}
    }
    if(result.output.empty())throw std::invalid_argument("--output is required");return result;
}
fs::path createRun(const fs::path& root){
    std::error_code error;const auto status=fs::symlink_status(root,error);
    if(!error && fs::exists(status) && (!fs::is_directory(status) || fs::is_symlink(status)))
        throw std::runtime_error("Campaign output must be an ordinary directory");
    fs::create_directories(root);const auto stamp=Clock::now().time_since_epoch().count();
    for(unsigned i=0;i<32;++i){auto path=root/("run-"+std::to_string(stamp)+"-"+std::to_string(i));
        if(fs::create_directory(path))return fs::absolute(path);}
    throw std::runtime_error("Cannot allocate fresh campaign output directory");
}
void write(const fs::path& path,std::string_view bytes){
    std::ofstream file(path,std::ios::binary|std::ios::trunc);file.write(bytes.data(),static_cast<std::streamsize>(bytes.size()));file.flush();
    if(!file)throw std::runtime_error("Cannot write campaign artifact: "+path.string());
}
void binary(const fs::path& path,const std::vector<std::uint8_t>& bytes){
    if(!bytes.empty())write(path,{reinterpret_cast<const char*>(bytes.data()),bytes.size()});
}
struct Totals {
    std::uint64_t cases=0,ticks=0,checkpoints=0,events=0,restored=0,rollbacks=0,replayed=0,duplicates=0,lastSeed=0,lastHash=0;
    void add(const CaseResult& result){++cases;ticks+=result.ticks;checkpoints+=result.checkpointsChecked;events+=result.eventsChecked;
        restored+=result.restoredTicks;rollbacks+=result.rollbacks;replayed+=result.replayedTicks;duplicates+=result.duplicates;
        lastSeed=result.seed;lastHash=result.finalHash;}
};
std::string progress(const Options& args,const Totals& total,const Clock::time_point start,const char* status,const fs::path& run){
    const auto elapsed=std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now()-start).count();
    std::ostringstream out;out<<"{\"schema\":1,\"status\":"<<quote(status)<<",\"baseSeed\":"<<quote(std::to_string(args.seed))
        <<",\"lastSeed\":"<<quote(std::to_string(total.lastSeed))<<",\"nextSeed\":"<<quote(std::to_string(args.seed+total.cases))
        <<",\"requestedSeconds\":"<<args.seconds<<",\"minimumCases\":"<<args.cases<<",\"elapsedMs\":"<<elapsed
        <<",\"cases\":"<<total.cases<<",\"ticks\":"<<total.ticks<<",\"checkpointsChecked\":"<<total.checkpoints
        <<",\"eventsChecked\":"<<total.events<<",\"restoredTicks\":"<<total.restored<<",\"rollbacks\":"<<total.rollbacks
        <<",\"replayedTicks\":"<<total.replayed<<",\"duplicates\":"<<total.duplicates
        <<",\"lastHash\":"<<quote(std::to_string(total.lastHash))<<",\"output\":"<<quote(run.generic_string())<<"}\n";return out.str();
}
std::string failure(const CaseFailure& error,const fs::path& executable){
    std::ostringstream out;out<<"{\"schema\":1,\"status\":\"failed\",\"seed\":"<<quote(std::to_string(error.seed))
        <<",\"tick\":"<<error.tick<<",\"ticks\":"<<error.requestedTicks<<",\"lane\":"<<quote(error.lane)
        <<",\"error\":"<<quote(error.what())<<",\"executable\":"<<quote(executable.generic_string())
        <<",\"recipe\":[\"--seed\","<<quote(std::to_string(error.seed))<<",\"--cases\",\"1\",\"--ticks\","
        <<quote(std::to_string(error.requestedTicks))<<",\"--output\",\"NEW_OUTPUT_DIRECTORY\"]";
    if(error.difference){const auto& d=*error.difference;
        out<<",\"difference\":{\"tick\":"<<d.tick<<",\"section\":"<<quote(d.section)<<",\"entity\":"<<quote(std::to_string(d.entity))
            <<",\"component\":"<<d.component<<",\"field\":"<<d.field<<",\"lane\":"<<d.lane
            <<",\"expected\":"<<quote(std::to_string(d.expected))<<",\"actual\":"<<quote(std::to_string(d.actual))<<'}';}
    out<<",\"artifacts\":{\"before\":"<<(!error.before.empty()?quote("before.checkpoint"):"null")
        <<",\"expected\":"<<(!error.expected.empty()?quote("expected.checkpoint"):"null")
        <<",\"actual\":"<<(!error.actual.empty()?quote("actual.checkpoint"):"null")
        <<",\"input\":"<<(!error.failingInput.empty()?quote("input.bin"):"null")<<"}}\n";return out.str();
}
void saveFailure(const fs::path& run,const CaseFailure& error,const fs::path& executable){
    binary(run/"before.checkpoint",error.before);binary(run/"expected.checkpoint",error.expected);
    binary(run/"actual.checkpoint",error.actual);binary(run/"input.bin",error.failingInput);
    write(run/"failure.json",failure(error,executable));
}
}
int main(int argc,char** argv){
    Options args;
    try {args=options(argc,argv);}catch(const std::exception& error){
        std::cerr<<error.what()<<"\nUsage: simulationcampaign --output DIRECTORY [--seed UINT64] [--cases 1..1000000]"
            <<" [--seconds 0..86400] [--ticks 16..8192] [--context-only]\n";return 2;}
    fs::path run;const auto start=Clock::now();Totals total;
    try{
        run=createRun(args.output);write(run/"progress.json",progress(args,total,start,"running",run));
        contextFaultChecks();
        if(!args.contextOnly){
            const auto deadline=start+std::chrono::seconds(args.seconds);auto nextProgress=Clock::now()+std::chrono::seconds(15);
            while(total.cases<args.cases || (args.seconds && Clock::now()<deadline)){
                const auto result=runCase(args.seed+total.cases,args.ticks);total.add(result);
                if(Clock::now()>=nextProgress){const auto text=progress(args,total,start,"running",run);
                    write(run/"progress.json",text);std::cout<<text<<std::flush;nextProgress=Clock::now()+std::chrono::seconds(15);}
            }
        }
        const auto text=progress(args,total,start,"passed",run);write(run/"progress.json",text);std::cout<<text;return 0;
    }catch(const CaseFailure& error){
        try {saveFailure(run,error,fs::absolute(argv[0]));write(run/"progress.json",progress(args,total,start,"failed",run));}
        catch(const std::exception& artifactError){std::cerr<<artifactError.what()<<'\n';return 3;}
        std::cerr<<failure(error,fs::absolute(argv[0]));return 1;
    }catch(const std::exception& error){
        std::cerr<<error.what()<<'\n';
        if(!run.empty())try {
            CaseFailure detail(args.seed+total.cases,0,args.ticks?args.ticks:256,"harness",error.what());
            saveFailure(run,detail,fs::absolute(argv[0]));write(run/"progress.json",progress(args,total,start,"failed",run));
        }catch(const std::exception& artifactError){std::cerr<<artifactError.what()<<'\n';return 3;}
        return 1;
    }
}
