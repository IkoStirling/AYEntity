#include "DetSessionScenario.h"
#include <AYEntity/DeterministicReplay.h>
#include <AYEntity/EntityModule.h>
#include <AYEntity/World.h>
#include <cfenv>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#if defined(_M_X64) || defined(__x86_64__)
#include <xmmintrin.h>
#endif
using namespace detsession_scenario;
int main(int argc,char** argv) {
    if(argc!=4)return 2;
    const std::string mode=argv[1],path=argv[2],output=argv[3];
    if(mode!="record" && mode!="live" && mode!="replay" && mode!="seek")return 2;
    if(mode!="record") {
        if(std::fesetround(FE_UPWARD)!=0)return 3;
#if defined(_M_X64) || defined(__x86_64__)
        _mm_setcsr(_mm_getcsr()|0x8040u);
#endif
    }
    if(!registerEntityCoreComponents(ComponentRegistry::instance()))return 4;
    World& world=World::instance();world.initialize();registerEntityCoreSystems();
    try {
        DeterministicSession session(world);
        if(!configure(session,mode!="record"))throw std::runtime_error(session.error());
        DetReplayWriter writer;DetReplayReader reader;
        if(mode=="record" && !writer.begin(session,path,300))throw std::runtime_error(writer.error());
        if(mode=="replay" || mode=="seek") {
            if(!reader.open(path) || reader.tickCount()!=10000)throw std::runtime_error(reader.error());
            if(!reader.seek(session,mode=="seek"?5000:0))throw std::runtime_error(reader.error());
        }
        std::vector<std::uint64_t> hashes;
        std::optional<DetSessionCheckpoint> restart;
        bool restored=false;
        while(session.nextTick()<10000) {
            const auto tick=session.nextTick();bool ok=false;
            if(mode=="record")ok=writer.advance(input(tick));
            else if(mode=="live")ok=session.advance(input(tick,true));
            else ok=reader.advance(session);
            if(!ok)throw std::runtime_error(session.error()+" "+writer.error()+" "+reader.error());
            auto checkpoint=session.checkpoint();if(!checkpoint)throw std::runtime_error(session.error());
            hashes.push_back(detCheckpointHash(*checkpoint));
            // Deliberately different presentation cadence and alpha across processes.
            if(mode=="record" || tick%3==0)world.updatePresentation(mode=="record"?0.008f:0.031f,mode=="record"?0.2f:0.8f);
            if(mode=="live" && session.nextTick()==5000)restart=*checkpoint;
            if(mode=="live" && !restored && session.nextTick()==7500) {
                if(!session.restore(*restart))throw std::runtime_error(session.error());hashes.resize(5000);restored=true;
            }
        }
        if(mode=="record" && !writer.finish())throw std::runtime_error(writer.error());
        const auto final=encodeDetCheckpoint(*session.checkpoint());
        std::ofstream state(output+".state",std::ios::binary|std::ios::trunc);
        state.write(reinterpret_cast<const char*>(final.data()),final.size());if(!state)return 5;
        std::ofstream trace(output+".trace",std::ios::trunc),tail(output+".tail.trace",std::ios::trunc);
        const auto first=mode=="seek"?5001u:1u;
        for(std::size_t i=0;i<hashes.size();++i){trace<<first+i<<" "<<std::hex<<hashes[i]<<std::dec<<"\n";
            if(first+i>5000)tail<<first+i<<" "<<std::hex<<hashes[i]<<std::dec<<"\n";}
        if(!trace || !tail)return 5;
        std::cout<<"PASS "<<mode<<": "<<hashes.size()<<" verified ticks; final="<<std::hex<<detCheckpointHash(*session.checkpoint())<<"\n";
    }catch(const std::exception& e){std::cerr<<e.what()<<"\n";world.shutdown();return 1;}
    world.shutdown();return 0;
}
