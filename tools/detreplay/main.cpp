#include <AYEntity/DeterministicRollbackReplay.h>
#include <iostream>
#include <sstream>
#include <iomanip>
using namespace ayt::entity;
namespace {
std::string quote(const std::string& value) {
    std::ostringstream out;out<<'"';
    for(unsigned char c:value) {switch(c){case '"':out<<"\\\"";break;case '\\':out<<"\\\\";break;
        default:if(c<32)out<<"\\u"<<std::hex<<std::setw(4)<<std::setfill('0')<<unsigned(c)<<std::dec;else out<<c;}}
    out<<'"';return out.str();
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
    if(argc<3){std::cerr<<"usage: ayreplay inspect|scan source.rpi|source.rpl\n       ayreplay recover|rebuild source.rpi[.partial] NEW_DIRECTORY\n";return 2;}
    try {
        const std::string command=argv[1];DetReplayArchiveReport report;
        if(command=="inspect" && argc==3)report=DetRollbackReplayArchive::inspect(argv[2]);
        else if(command=="scan" && argc==3)report=DetRollbackReplayArchive::scan(argv[2]);
        else if((command=="recover" || command=="rebuild") && argc==4)report=DetRollbackReplayArchive::rebuildIndex(argv[2],argv[3]);
        else {std::cerr<<"Invalid command/argument count\n";return 2;}
        print(report);return report.valid?0:1;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
