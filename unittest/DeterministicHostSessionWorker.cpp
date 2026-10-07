#include "DetHostFixture.h"
#include <cfenv>
#include <fstream>
#include <iostream>
#if defined(_M_X64) || defined(__x86_64__)
#include <xmmintrin.h>
#endif
using namespace dethost_test;
int main(int argc,char** argv) {
    if(argc!=4)return 2;
    const std::string mode=argv[1],path=argv[2],output=argv[3];
    if(mode!="record" && mode!="live" && mode!="replay" && mode!="seek")return 2;
    if(!registerEntityCoreComponents(ComponentRegistry::instance()))return 3;
    if(mode!="record") {
        if(std::fesetround(FE_UPWARD)!=0)return 3;
#if defined(_M_X64) || defined(__x86_64__)
        _mm_setcsr(_mm_getcsr()|0x8040u);
#endif
    }
    try {
        Fixture f;auto r=Fixture::recipe(mode=="record"?DetHostMode::Record:
            mode=="live"?DetHostMode::Live:DetHostMode::Replay,path);
        r.checkpointInterval=300;
        r.configure=[&](auto& s){return detsession_scenario::configure(s,mode!="record");};
        r.input=[&](const auto& request,auto& packet){
            packet=detsession_scenario::input(request.tick,mode!="record");return true;};
        if(!f.bind(r))throw std::runtime_error(f.controller.error());
        if(mode=="seek" && !f.controller.seek(5000))throw std::runtime_error(f.controller.error());
        if(!f.controller.resume())throw std::runtime_error(f.controller.error());
        std::vector<std::uint64_t> hashes;std::optional<DetSessionCheckpoint> restart;
        bool restored=false;std::uint64_t frame=0;
        while(f.controller.session()->nextTick()<10000) {
            const auto before=f.controller.session()->nextTick();
            // Exact binary wall deltas: one tick per frame vs two presentation frames per tick.
            f.frame(mode=="record"?1.0f/64.0f:1.0f/128.0f,++frame);
            if(f.controller.state()==DetHostState::Faulted)throw std::runtime_error(f.controller.error());
            if(f.controller.session()->nextTick()==before)continue;
            if(f.controller.session()->nextTick()!=before+1)throw std::runtime_error("Host duplicated a tick");
            const auto checkpoint=f.controller.checkpoint();
            if(!checkpoint)throw std::runtime_error(f.controller.error());
            hashes.push_back(detCheckpointHash(*checkpoint));
            if(mode=="live" && checkpoint->nextTick==5000)restart=*checkpoint;
            if(mode=="live" && !restored && checkpoint->nextTick==7500) {
                f.controller.pause();f.frame(1.0f/64.0f,++frame);
                if(f.controller.session()->nextTick()!=7500 || !f.controller.restore(*restart))
                    throw std::runtime_error("Paused Host restore failed");
                hashes.resize(5000);restored=true;
                if(!f.controller.stepOnce() || !f.controller.resume())throw std::runtime_error("Host restart single step failed");
                hashes.push_back(detCheckpointHash(*f.controller.checkpoint()));
            }
        }
        const auto checkpoint=*f.controller.checkpoint();const auto final=encodeDetCheckpoint(checkpoint);
        if(mode=="record" && !f.controller.stop())throw std::runtime_error(f.controller.error());
        if((mode=="replay" || mode=="seek") && f.controller.state()!=DetHostState::Completed)
            throw std::runtime_error("Replay did not complete");
        std::ofstream state(output+".state",std::ios::binary|std::ios::trunc);
        state.write(reinterpret_cast<const char*>(final.data()),final.size());if(!state)return 5;
        std::ofstream trace(output+".trace",std::ios::trunc),tail(output+".tail.trace",std::ios::trunc);
        const auto first=mode=="seek"?5001u:1u;
        for(std::size_t i=0;i<hashes.size();++i){trace<<first+i<<" "<<std::hex<<hashes[i]<<std::dec<<"\n";
            if(first+i>5000)tail<<first+i<<" "<<std::hex<<hashes[i]<<std::dec<<"\n";}
        if(!trace || !tail)return 5;
        std::cout<<"PASS Host "<<mode<<": "<<hashes.size()<<" ticks; final="<<std::hex<<detCheckpointHash(checkpoint)<<"\n";
    }catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}
    return 0;
}
