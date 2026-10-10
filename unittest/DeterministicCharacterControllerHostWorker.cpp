// design reference: AYEntity/design.md Stage21; actual standard Host movement record/live/restore/replay/seek.
#include "DetHostFixture.h"
#include "DetCharacterControllerScenario.h"
#include <cfenv>
#include <fstream>
#include <iostream>
#if defined(_M_X64) || defined(__x86_64__)
#include <xmmintrin.h>
#endif
using namespace dethost_test;
namespace scenario=detcharacter_scenario;
namespace {
constexpr std::uint64_t totalTicks=2048, middleTick=1024, rewindTick=1536;
void require(bool ok,const std::string& message){if(!ok)throw std::runtime_error(message);}
}
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
        Fixture fixture;
        auto recipe=Fixture::recipe(mode=="record"?DetHostMode::Record:mode=="live"?DetHostMode::Live:DetHostMode::Replay,path);
        recipe.checkpointInterval=128;
        recipe.configure=[&](auto& session){return scenario::configure(session,mode!="record");};
        recipe.input=[&](const auto& request,auto& packet){packet=scenario::input(request.tick,mode!="record");return true;};
        require(fixture.bind(recipe),fixture.controller.error());
        if(mode=="seek")require(fixture.controller.seek(middleTick),fixture.controller.error());
        require(fixture.controller.resume(),fixture.controller.error());
        std::vector<std::uint64_t> hashes;
        std::optional<DetSessionCheckpoint> restart;
        bool restored=false;
        std::uint64_t frame=0;
        while(fixture.controller.session()->nextTick()<totalTicks) {
            const auto before=fixture.controller.session()->nextTick();
            fixture.frame(mode=="record"?1.0f/64.0f:1.0f/128.0f,++frame);
            require(fixture.controller.state()!=DetHostState::Faulted,fixture.controller.error());
            if(fixture.controller.session()->nextTick()==before)continue;
            require(fixture.controller.session()->nextTick()==before+1,"Host duplicated controller tick");
            const auto checkpoint=fixture.controller.checkpoint();
            require(checkpoint.has_value(),fixture.controller.error());
            require(scenario::matchesGolden(*checkpoint),"Character controller golden mismatch at tick "+std::to_string(checkpoint->nextTick));
            hashes.push_back(detCheckpointHash(*checkpoint));
            if(mode=="live" && checkpoint->nextTick==middleTick)restart=*checkpoint;
            if(mode=="live" && !restored && checkpoint->nextTick==rewindTick) {
                fixture.controller.pause();fixture.frame(1.0f/64.0f,++frame);
                require(fixture.controller.session()->nextTick()==rewindTick && restart.has_value(),"Paused controller Host changed tick");
                require(fixture.controller.restore(*restart),fixture.controller.error());
                hashes.resize(middleTick);restored=true;
                require(fixture.controller.stepOnce() && fixture.controller.resume(),fixture.controller.error());
                require(scenario::matchesGolden(*fixture.controller.checkpoint()),"Character controller restore/step golden");
                hashes.push_back(detCheckpointHash(*fixture.controller.checkpoint()));
            }
        }
        const auto checkpoint=fixture.controller.checkpoint();
        require(checkpoint.has_value() && scenario::matchesGolden(*checkpoint),"Character controller final checkpoint");
        const auto bytes=encodeDetCheckpoint(*checkpoint);
        if(mode=="record")require(fixture.controller.stop(),fixture.controller.error());
        if(mode=="replay" || mode=="seek")require(fixture.controller.state()==DetHostState::Completed,"Controller replay did not complete");
        std::ofstream state(output+".state",std::ios::binary|std::ios::trunc);
        state.write(reinterpret_cast<const char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));
        require(static_cast<bool>(state),"Write controller state witness");
        std::ofstream trace(output+".trace",std::ios::trunc),tail(output+".tail.trace",std::ios::trunc);
        const auto first=mode=="seek"?middleTick+1:1;
        for(std::size_t index=0;index<hashes.size();++index) {
            const auto tick=first+index;
            trace<<tick<<' '<<std::hex<<hashes[index]<<std::dec<<'\n';
            if(tick>middleTick)tail<<tick<<' '<<std::hex<<hashes[index]<<std::dec<<'\n';
        }
        require(static_cast<bool>(trace) && static_cast<bool>(tail),"Write controller trace witnesses");
        std::cout<<"PASS Character Controller Host "<<mode<<": "<<hashes.size()<<" verified ticks; final="<<std::hex<<detCheckpointHash(*checkpoint)<<'\n';
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
    return 0;
}
