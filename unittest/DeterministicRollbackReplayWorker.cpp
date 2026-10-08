// design reference: AYEntity/design.md; offline reproduction of the network example.
#include "DetTypedSessionScenario.h"
#include <AYEntity/DeterministicRollbackReplay.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <iostream>
using namespace ayt::entity;
int main(int argc,char** argv) {
    if(argc!=3 && argc!=5){std::cerr<<"usage: recording.rpl|recording.rpi expected.state [epoch nextTick]\n";return 2;}
    try {
        DeterministicSession session({1,1,1,64,0});
        if(!dettyped_scenario::configure(session,true,false,true))throw std::runtime_error(session.error());
        DetRollbackReplayReader reader;
        auto check=[&](bool ok){if(!ok){if(const auto& d=reader.difference())
            std::cerr<<"DIFFERENCE tick="<<d->tick<<" entity="<<d->entity<<" schema="<<d->component
                <<" field="<<d->field<<" lane="<<d->lane<<" section="<<d->section<<'\n';
            throw std::runtime_error(reader.error());}};
        check(reader.open(argv[1]));check(reader.restoreInitial(session));
        std::ostringstream trace;std::size_t events=0;
        while(!reader.atEnd()) {
            check(reader.advance(session));
            for(const auto& e:reader.takeConfirmedEvents()){++events;
                trace<<e.epoch<<' '<<e.tick<<' '<<e.command.source<<' '<<e.command.sequence<<' '<<e.command.type<<'\n';}
        }
        auto read=[](const std::string& path){
            if(std::filesystem::file_size(path)>8*1024*1024)throw std::runtime_error("Example oracle file budget");
            std::ifstream f(path,std::ios::binary);if(!f)throw std::runtime_error("Cannot open example oracle");
            return std::vector<std::uint8_t>{std::istreambuf_iterator<char>(f),std::istreambuf_iterator<char>()};};
        const auto expected=read(argv[2]);
        if(encodeDetCheckpoint(*session.checkpoint())!=expected)throw std::runtime_error("Replay full-state oracle mismatch");
        const auto traceBytes=read(std::string(argv[2])+".trace");const auto actualTrace=trace.str();
        std::string expectedTrace(traceBytes.begin(),traceBytes.end());std::erase(expectedTrace,'\r');
        if(actualTrace!=expectedTrace)throw std::runtime_error("Replay event identity oracle mismatch");
        std::cout<<"FILES "<<reader.files().size()<<'\n';
        for(const auto& s:reader.segments())std::cout<<"SEGMENT epoch="<<s.epoch<<" first="<<s.firstTick<<" end="<<s.endTick<<" skipped="<<s.skippedTicks<<'\n';
        if(argc==5) {
            const auto epoch=std::stoul(argv[3]);if(epoch>UINT32_MAX)throw std::runtime_error("Invalid epoch");
            check(reader.seek(session,static_cast<std::uint32_t>(epoch),std::stoull(argv[4])));
            if(!reader.takeConfirmedEvents().empty())throw std::runtime_error("Seek emitted effects");
            while(!reader.atEnd()){check(reader.advance(session));(void)reader.takeConfirmedEvents();}
            if(encodeDetCheckpoint(*session.checkpoint())!=expected)throw std::runtime_error("Seek full-state oracle mismatch");
        }
        std::cout<<"PASS offline confirmed replay/epoch seek, end="<<reader.endTick()<<", speculativeHead="
            <<reader.speculativeHeadAtSeal()<<", exact effects="<<events<<'\n';return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
