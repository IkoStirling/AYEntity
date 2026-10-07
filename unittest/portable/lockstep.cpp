#include <AYEntity/DeterministicLockstepProtocol.h>
#include <algorithm>
#include <iostream>
#include <stdexcept>
using namespace ayt::entity;
void require(bool b){if(!b)throw std::runtime_error("lockstep protocol oracle failed");}
DetLockstepBarrier peer(unsigned id,std::uint64_t hash=0){return {{17,1,id,{1,2,3},2,2,4096},{1,2,3},hash};}
void deliver(DetLockstepBarrier& a,DetLockstepBarrier& b,bool reversed=false){auto packets=a.packets();if(reversed)std::reverse(packets.begin(),packets.end());
    for(const auto& p:packets)require(b.receive(a.config().localMember,p));}
int main(){try {
    auto a=peer(1),b=peer(2),c=peer(3);std::uint64_t state=0;
    for(std::uint64_t tick=0;tick<10000;++tick){const std::vector<std::uint8_t> payload{static_cast<std::uint8_t>(tick)};
        require(a.submit(tick,payload));require(b.submit(tick,payload));require(c.submit(tick,payload));
        require(!a.ready()); // Drop the first transmission to A; retransmission repairs it.
        deliver(a,b,true);deliver(a,c);deliver(b,c,true);deliver(c,b);
        require(!a.ready());deliver(b,a,true);deliver(c,a);deliver(c,a,true);
        require(a.ready() && b.ready() && c.ready());require(a.inputs()==b.inputs() && a.inputs()==c.inputs());
        state=state*33+(tick&255)*3;require(a.commit(state));require(b.commit(state));require(c.commit(state));
    }
    require(a.nextTick()==10000 && !a.faulted());
    auto d=peer(1),e=peer(2);auto packet=e.packets().front();auto broken=packet;broken.back()^=1;
    require(!d.receive(2,broken) && !d.faulted());require(!d.receive(3,packet) && !d.faulted());
    require(d.receive(2,packet));require(!d.submit(3,{}));require(d.submit(0,std::vector<std::uint8_t>{1}));
    require(!d.submit(0,std::vector<std::uint8_t>{2}) && d.faulted());
    auto mismatch=peer(2,9),receiver=peer(1);require(!receiver.receive(2,mismatch.packets().front()) && receiver.faulted());
    auto disconnected=peer(1);disconnected.disconnect(2);require(disconnected.faulted());
    auto limited=peer(1);require(!limited.submit(0,std::vector<std::uint8_t>(4097)) && !limited.faulted());
    // Exact independent recurrence modulo 2^64 for the 10000 synchronized inputs.
    require(state==0x0152ad311d9348e8ull);
    std::cout<<"PASS lockstep: 10000 ticks / 3 peers, reorder/drop/duplicate recovery, identity, manifest, conflicts and bounds\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
