#include "DetHostFixture.h"
#include "DetExtendedCollisionScenario.h"
#include <AYNetwork.h>
#include <AYNetwork/NetworkModule.h>
#include <chrono>
#include <fstream>
#include <iostream>
#include <thread>
#include <cfenv>
using namespace dethost_test;
using namespace ayt::net;
int main(int argc,char** argv){
    if(argc!=5)return 2;
    const auto member=static_cast<std::uint32_t>(std::stoul(argv[1]));
    const auto port=static_cast<std::uint16_t>(std::stoul(argv[2]));
    const std::string output=argv[3],replay=argv[4];
    if(!registerEntityCoreComponents(ComponentRegistry::instance()))return 3;
    try {
        Fixture f;std::fesetround(member==1?FE_TONEAREST:FE_UPWARD);
        auto recipe=Fixture::recipe(member==1?DetHostMode::Record:DetHostMode::Live,replay);
        recipe.checkpointInterval=300;recipe.lockstep=DetLockstepConfig{81,1,member,{1,2},2,2};
        recipe.configure=[member](auto& s){return detextendedcollision_scenario::configure(s,member==2);};
        std::uint64_t samples=0;
        recipe.input=[&](const auto& request,auto& packet){++samples;packet=detextendedcollision_scenario::input(request.tick,member==2);
            std::erase_if(packet.commands,[&](const auto& c){return c.source!=member;});return true;};
        if(!f.bind(recipe))throw std::runtime_error(f.controller.error());
        // Host registry is already frozen. Own the transport explicitly and pump
        // it at external boundaries without installing a second fixed Sim phase.
        auto transport=createNetworkSubSystem();auto* net=transport.get();
        if(!net || !net->initialize())throw std::runtime_error("network init");
        struct Shutdown{INetworkSubSystem* net;~Shutdown(){if(net)net->shutdown();}} cleanup{net};
        std::uint64_t received=0;
        net->onMessage(CHANNEL_RELIABLE,[&](NetConnection*,std::uint8_t,const void* data,std::size_t size){
            if(++received%37==0)return;
            (void)f.controller.receiveNetwork(member==1?2:1,{static_cast<const std::uint8_t*>(data),size});});
        if(member==1)net->listen(port);else net->connect("127.0.0.1",port);
        std::ofstream trace(output+".trace");
        using Clock=std::chrono::steady_clock;const auto deadline=Clock::now()+std::chrono::seconds(420);
        auto lastSend=Clock::time_point{},completed=Clock::time_point{};std::uint64_t frame=0,lastTick=UINT64_MAX,lastSamples=UINT64_MAX,loops=0;
        while(Clock::now()<deadline){
            net->update(0.001f);
            if(f.controller.state()==DetHostState::Faulted)throw std::runtime_error(f.controller.error());
            const auto before=f.controller.session()->nextTick();
            if(before<10000)f.frame(member==1?1.0f/64.0f:1.0f/128.0f,++frame);
            const auto tick=f.controller.session()->nextTick();
            if(tick!=before){const auto cp=*f.controller.checkpoint();
                if(tick!=before+1 || !detextendedcollision_scenario::matchesGolden(cp))throw std::runtime_error("Host network golden/tick mismatch");
                trace<<tick<<' '<<std::hex<<detCheckpointHash(cp)<<std::dec<<'\n';}
            if(tick!=lastTick || samples!=lastSamples || Clock::now()-lastSend>std::chrono::milliseconds(5)){
                if(++loops%5){auto packets=f.controller.networkPackets();if(member==2)std::reverse(packets.begin(),packets.end());
                    auto send=[&](NetConnection* connection){for(const auto& p:packets)net->sendTo(connection,CHANNEL_RELIABLE,p.data(),p.size());};
                    if(member==1){for(auto* c:net->getConnections())send(c);}else if(auto* c=net->getConnection())send(c);}
                lastSend=Clock::now();lastTick=tick;lastSamples=samples;
            }
            if(tick==10000 && f.controller.networkSynchronized()){
                if(completed==Clock::time_point{})completed=Clock::now();
                if(Clock::now()-completed>std::chrono::seconds(1))break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        if(f.controller.session()->nextTick()!=10000 || !f.controller.networkSynchronized() || samples!=10000)
            throw std::runtime_error("Host network timeout or duplicate input samples");
        const auto final=*f.controller.checkpoint();const auto bytes=encodeDetCheckpoint(final);
        {std::ofstream state(output,std::ios::binary);state.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());if(!state)throw std::runtime_error("write state");}
        net->shutdown();cleanup.net=nullptr;
        if(member==1){const auto recorded=f.controller.recordingPath();if(!f.controller.stop())throw std::runtime_error(f.controller.error());
            f.controller.disconnect();auto playback=recipe;playback.lockstep.reset();playback.mode=DetHostMode::Replay;playback.replayPath=recorded;
            if(!f.bind(playback) || !f.controller.seek(5000) || !f.controller.resume())throw std::runtime_error(f.controller.error());
            while(f.controller.session()->nextTick()<10000){f.frame(1.0f/64.0f,++frame);if(f.controller.state()==DetHostState::Faulted)throw std::runtime_error(f.controller.error());}
            if(encodeDetCheckpoint(*f.controller.checkpoint())!=bytes || samples!=10000)throw std::runtime_error("Host network replay/seek mismatch");}
        std::cout<<"PASS actual Host/network 10000 ticks, samples="<<samples<<" hash="<<std::hex<<detCheckpointHash(final)<<'\n';
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
