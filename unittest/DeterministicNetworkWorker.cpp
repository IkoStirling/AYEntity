#include "DetSessionScenario.h"
#include <AYEntity/DeterministicNetwork.h>
#include <AYEntity/DeterministicReplay.h>
#include <AYEntity/EntityModule.h>
#include <AYEntity.h>
#include <AYNetwork.h>
#include <chrono>
#include <fstream>
#include <iostream>
#include <thread>

using namespace ayt::entity;
using namespace ayt::net;
int main(int argc,char** argv){
    if(argc!=5)return 2;const auto member=static_cast<std::uint32_t>(std::stoul(argv[1]));const auto port=static_cast<std::uint16_t>(std::stoul(argv[2]));
    const std::string output=argv[3],replay=argv[4];
    if(!registerEntityCoreComponents(ComponentRegistry::instance()))return 3;
    registerNetworkSubSystem();auto* network=findRegisteredNetworkSubSystem();if(!network || !network->initialize())return 4;
    auto& world=World::instance();world.initialize();int result=0;
    try {
        auto session=std::make_unique<DeterministicSession>(world);if(!detsession_scenario::configure(*session,member==2))throw std::runtime_error(session->error());
        DetReplayWriter writer;if(member==1 && !writer.begin(*session,replay,100))throw std::runtime_error(writer.error());
        DeterministicLockstep peers(*session,{17,1,member,{1,2},2,2},member==1?DeterministicLockstep::TickSink{[&](auto input){return writer.advance(std::move(input));}}:DeterministicLockstep::TickSink{});
        std::uint64_t received=0,submitted=UINT64_MAX;bool failed=false;
        network->onMessage(CHANNEL_RELIABLE,[&](NetConnection*,std::uint8_t,const void* data,std::size_t size){
            // Local test deliberately drops decoded application packets; retained
            // retransmission repairs this without touching the Sim clock/state.
            if(++received%37==0)return;
            if(!peers.receive(member==1?2:1,{static_cast<const std::uint8_t*>(data),size}) && peers.faulted())failed=true;
        });
        if(member==1)network->listen(port);else network->connect("127.0.0.1",port);
        using Clock=std::chrono::steady_clock;const auto deadline=Clock::now()+std::chrono::seconds(60);auto completed=Clock::time_point{};
        std::uint64_t loops=0;auto lastSend=Clock::time_point{};bool changed=true;
        while(Clock::now()<deadline){
            network->update(0.001f);if(failed || peers.faulted())throw std::runtime_error(peers.error());
            const auto tick=session->nextTick();
            if(tick<1000 && submitted!=tick){auto input=detsession_scenario::input(tick,member==2);
                std::erase_if(input.commands,[&](const auto& c){return c.source!=member;});
                if(!peers.submit(std::move(input)))throw std::runtime_error(peers.error());submitted=tick;changed=true;}
            std::vector<NetConnection*> connections;
            if(member==1)connections=network->getConnections();else if(auto* connection=network->getConnection())connections.push_back(connection);
            if(changed || Clock::now()-lastSend>std::chrono::milliseconds(50)){
                if(++loops%5){sendDetLockstepPackets(peers,*network,connections);
                    if(loops%11==0)sendDetLockstepPackets(peers,*network,connections);}
                lastSend=Clock::now();changed=false;
            }
            if(peers.ready() && tick<1000){if(!peers.advance())throw std::runtime_error(peers.error());changed=true;}
            if(session->nextTick()==1000 && peers.synchronized()){
                if(completed==Clock::time_point{})completed=Clock::now();
                if(Clock::now()-completed>std::chrono::seconds(1))break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        if(session->nextTick()!=1000 || !peers.synchronized())throw std::runtime_error("network lockstep timeout nextTick="+std::to_string(session->nextTick())+" received="+std::to_string(received)+" error="+peers.error());
        const auto final=*session->checkpoint();const auto bytes=encodeDetCheckpoint(final);
        {std::ofstream file(output,std::ios::binary);file.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());if(!file)throw std::runtime_error("checkpoint write failure");}
        if(member==1){if(!writer.finish())throw std::runtime_error(writer.error());
            // Replay under the same session after transport has finished; agreed
            // lockstep inputs are ordinary replay inputs, including no-op frames.
            DetReplayReader reader;if(!reader.open(writer.path()) || !reader.seek(*session,500))throw std::runtime_error(reader.error());
            while(!reader.atEnd())if(!reader.advance(*session))throw std::runtime_error(reader.error());
            if(firstDetDifference(final,*session->checkpoint()))throw std::runtime_error("network replay/seek mismatch");}
        std::cout<<"PASS AYNetwork lockstep member="<<member<<" nextTick=1000 hash="<<std::hex<<detCheckpointHash(final)<<" received="<<std::dec<<received<<'\n';
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';result=1;}
    network->shutdown();world.shutdown();return result;
}
