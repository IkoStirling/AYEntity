// design reference: AYEntity/design.md; actual Host + AYNetwork rollback acceptance.
#include "DetHostFixture.h"
#include <AYNetwork.h>
#include <chrono>
#include <fstream>
#include <iostream>
#include <thread>
#include <cfenv>
#include <set>
using namespace dethost_test;
using namespace ayt::net;
int main(int argc,char** argv) {
    if(argc!=5)return 2;
    const auto member=static_cast<std::uint32_t>(std::stoul(argv[1]));
    const auto port=static_cast<std::uint16_t>(std::stoul(argv[2]));
    const std::string output=argv[3];
    if(!registerEntityCoreComponents(ComponentRegistry::instance()))return 3;
    try {
        Fixture f;std::fesetround(member==1?FE_TONEAREST:FE_UPWARD);
        auto recipe=Fixture::recipe();DetRollbackNetworkConfig cfg;
        cfg.sessionId=95;cfg.localMember=member;cfg.recoveryMember=1;cfg.rollback.members={1,2};
        cfg.rollback.historyTicks=16;cfg.rollback.maxPredictionTicks=8;
        cfg.rollback.prediction={{1,DetPredictionMode::Hold}};recipe.rollback=cfg;
        recipe.configure=[member](auto& s){return dettyped_scenario::configure(s,member==2);};
        std::uint64_t samples=0,rollbacks=0;
        recipe.input=[&](const auto& request,auto& in){++samples;in=detsession_scenario::input(request.tick,member==2);
            std::erase_if(in.commands,[&](const auto& c){return c.source!=member;});return true;};
        if(!f.bind(recipe))throw std::runtime_error(f.controller.error());
        DeterministicSession baseline({1,1,1,64,0});if(!dettyped_scenario::configure(baseline))throw std::runtime_error("baseline configure");
        std::vector<DetConfirmedEvent> expected,actual;
        for(std::uint64_t tick=0;tick<10000;++tick){if(!baseline.advance(detsession_scenario::input(tick)))throw std::runtime_error(baseline.error());
            const auto cp=baseline.checkpoint();for(const auto& c:cp->pendingEvents)expected.push_back({tick<5000?1u:2u,tick,c});}
        auto transport=createNetworkSubSystem();auto* net=transport.get();
        if(!net || !net->initialize())throw std::runtime_error("network init");
        struct Shutdown{INetworkSubSystem* net;~Shutdown(){if(net)net->shutdown();}}cleanup{net};
        using Clock=std::chrono::steady_clock;
        struct Pending{Clock::time_point at;std::vector<std::uint8_t> bytes;};std::vector<Pending> pending;
        std::uint64_t received=0;
        net->onMessage(CHANNEL_RELIABLE,[&](NetConnection*,std::uint8_t,const void* data,std::size_t size){
            if(++received%37==0)return;
            const auto* begin=static_cast<const std::uint8_t*>(data);
            pending.push_back({Clock::now()+std::chrono::milliseconds(received%3),{begin,begin+size}});});
        if(member==1)net->listen(port);else net->connect("127.0.0.1",port);
        const auto deadline=Clock::now()+std::chrono::seconds(300);
        auto lastResend=Clock::time_point{},completed=Clock::time_point{};
        std::set<std::vector<std::uint8_t>> sent;
        std::uint64_t frame=0,lastTick=UINT64_MAX,lastSamples=UINT64_MAX,lastReport=0;
        while(Clock::now()<deadline) {
            net->update(0.001f);
            for(auto it=pending.end();it!=pending.begin();) {--it;if(it->at>Clock::now())continue;
                (void)f.controller.receiveNetwork(member==1?2:1,it->bytes);it=pending.erase(it);}
            if(f.controller.state()==DetHostState::Faulted)throw std::runtime_error(f.controller.error());
            const auto before=f.controller.session()->nextTick();
            const auto cap=f.controller.networkEpoch()==1?5000u:10000u;
            if(before<cap)f.frame(member==1?1.0f/64.0f:1.0f/128.0f,++frame);
            auto effects=f.controller.takeConfirmedEvents();actual.insert(actual.end(),effects.begin(),effects.end());
            const auto tick=f.controller.session()->nextTick();
            if(member==1 && tick==5000 && f.controller.networkEpoch()==1 && f.controller.networkSynchronized()) {
                rollbacks+=f.controller.rollbackCount();
                if(!f.controller.beginNetworkRecovery(2))throw std::runtime_error(f.controller.error());
            }
            const bool resend=Clock::now()-lastResend>std::chrono::milliseconds(100);
            if(tick!=lastTick || samples!=lastSamples || resend) {
                auto packets=f.controller.networkPackets();if(member==2)std::reverse(packets.begin(),packets.end());
                auto send=[&](NetConnection* c){for(const auto& p:packets)if(resend || !sent.contains(p))
                    net->sendTo(c,CHANNEL_RELIABLE,p.data(),p.size());};
                if(member==1){for(auto* c:net->getConnections())send(c);}else if(auto* c=net->getConnection())send(c);
                sent={packets.begin(),packets.end()};
                if(resend)lastResend=Clock::now();lastTick=tick;lastSamples=samples;
            }
            if(tick>=lastReport+1000){lastReport=tick;std::cout<<"PROGRESS member="<<member<<" tick="<<tick<<" verified="<<f.controller.networkVerifiedNextTick()<<" epoch="<<f.controller.networkEpoch()<<std::endl;}
            if(tick==10000 && f.controller.networkSynchronized()) {
                if(completed==Clock::time_point{})completed=Clock::now();
                if(Clock::now()-completed>std::chrono::seconds(1))break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        if(f.controller.session()->nextTick()!=10000 || !f.controller.networkSynchronized() || samples!=10000)
            throw std::runtime_error("predictive Host/network timeout or sampling tick="+std::to_string(f.controller.session()->nextTick())+
                " verified="+std::to_string(f.controller.networkVerifiedNextTick())+" samples="+std::to_string(samples));
        if(actual!=expected)throw std::runtime_error("confirmed effects repeated/missing/incorrect");
        const auto bytes=encodeDetCheckpoint(*f.controller.checkpoint());
        if(bytes!=encodeDetCheckpoint(*baseline.checkpoint()))throw std::runtime_error("corrected full baseline bytes");
        {std::ofstream state(output,std::ios::binary);state.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());}
        {std::ofstream trace(output+".trace");for(const auto& e:actual)trace<<e.epoch<<' '<<e.tick<<' '<<e.command.source<<' '<<e.command.sequence<<' '<<e.command.type<<'\n';}
        rollbacks+=f.controller.rollbackCount();
        std::cout<<"PASS actual Host/AYNetwork rollback 10000 ticks, samples="<<samples<<", rollbacks="<<rollbacks<<", confirmed effects="<<actual.size()<<", recovery epoch=2\n";
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
