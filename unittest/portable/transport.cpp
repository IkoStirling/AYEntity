// design reference: AYEntity/design.md, Stage16 bounded external egress.
#include <AYEntity/DeterministicTransport.h>
#include <iostream>
#include <stdexcept>
using namespace ayt::entity;
namespace {
void check(bool value,const char* text){if(!value)throw std::runtime_error(text);}
DetTransportConfig budget() {
    DetTransportConfig c;c.peers={2,3};c.maxPacketBytes=64;c.maxPackets=64;c.maxBufferedBytes=4096;
    c.burstBytes=64;c.bytesPerPump=64;c.packetsPerPump=1;c.bytesPerSecond=64000;c.resendMs=100;c.retryMs=5;return c;
}
DetTransportPacket packet(unsigned id,DetTransportPriority lane=DetTransportPriority::Realtime) {
    return {std::vector<std::uint8_t>(64,static_cast<std::uint8_t>(id)),lane};
}
}
void transportChecks() {
    auto c=budget();auto invalid=c;invalid.peers={2,2};bool threw=false;
    try{DetTransportScheduler bad(invalid);}catch(const std::invalid_argument&){threw=true;}check(threw,"duplicate egress member rejected");
    invalid=c;invalid.bytesPerPump=63;threw=false;
    try{DetTransportScheduler bad(invalid);}catch(const std::invalid_argument&){threw=true;}check(threw,"unsendable per-pump budget rejected");
    DetTransportScheduler s(c);std::vector<DetTransportPacket> set{packet(1)};
    check(s.sync(set),"initial sync");
    unsigned peer2=0,peer3=0;
    check(s.pump(0,[&](auto member,auto){if(member==2){++peer2;return DetTransportSendResult::RetryLater;}
        ++peer3;return DetTransportSendResult::Accepted;}),"per-peer backpressure");
    auto d=s.diagnostics();check(d.peers[0].pendingPackets==1 && d.peers[0].retryLater==1
        && d.peers[1].pendingPackets==0 && peer2==1 && peer3==1,"failure never marked sent and other peer progresses");
    auto accept=[](auto,auto){return DetTransportSendResult::Accepted;};
    check(s.sync(set)&&s.pump(4,accept),"unchanged set preserves backoff");
    check(s.diagnostics().peers[0].pendingPackets==1,"retry backoff honored");
    check(s.pump(5,accept)&&s.diagnostics().peers[0].pendingPackets==0,"unsent retries before periodic resend");
    check(s.pump(99,accept)&&s.diagnostics().peers[1].accepted==1,"no eager retained resend");
    check(s.pump(100,accept)&&s.diagnostics().peers[1].retransmits==1,"periodic repair");
    auto huge=set;huge[0].bytes.resize(65);
    check(!s.sync(huge)&&s.diagnostics().retainedBytes==64,"bad sync atomic");
    huge.assign(64,packet(2));for(unsigned i=0;i<64;++i)huge[i]=packet(i);
    auto small=c;small.maxBufferedBytes=128;DetTransportScheduler cap(small);check(cap.sync(set),"small budget");
    check(!cap.sync(huge)&&cap.diagnostics().retainedPackets==1,"retained byte budget atomic");
    set.push_back(packet(1,DetTransportPriority::Recovery));check(!s.sync(set),"priority conflict rejected");set.pop_back();
    check(!s.pump(99,accept)&&!s.faulted(),"backward clock rejects without owner fault");
    check(s.sync({})&&s.diagnostics().retainedBytes==0,"obsolete epoch/history dropped");
    check(s.sync(set)&&s.pump(101,[](auto,auto){return DetTransportSendResult::Disconnected;}),"disconnect parks");
    check(s.pump(102,accept)&&s.diagnostics().peers[0].disconnected,"no automatic reconnection");
    check(s.resumePeer(2)&&s.pump(103,accept)&&!s.diagnostics().peers[0].disconnected,"explicit admitted rebind");
    check(s.pump(UINT64_MAX,accept),"saturating token refill on large monotonic jump");
    DetTransportScheduler fairness(c);set.clear();
    for(unsigned i=0;i<20;++i)set.push_back(packet(i));
    for(unsigned i=20;i<40;++i)set.push_back(packet(i,DetTransportPriority::Recovery));
    check(fairness.sync(set),"fairness sync");unsigned real=0,bulk=0,lastBulk=0;
    for(unsigned frame=0;frame<40;++frame)check(fairness.pump(frame,[&](auto member,auto bytes){
        if(member==2){if(bytes[0]<20)++real;else {++bulk;check(frame-lastBulk<=5 || real==20,"bulk not starved");lastBulk=frame;}}
        return DetTransportSendResult::Accepted;}),"fair weighted pump");
    d=fairness.diagnostics();check(real==20&&bulk==20&&d.peers[0].pendingPackets==0&&d.peers[1].accepted==40,"all retained entries/peers served under one-packet budget");
    DetTransportScheduler rejected(c);check(rejected.sync(set),"rejection sync");
    check(!rejected.pump(0,[](auto,auto){return DetTransportSendResult::Rejected;})&&rejected.faulted(),"unsupported sender terminal");
    DetTransportScheduler reentry(c);check(reentry.sync(set),"reentry sync");
    check(reentry.pump(0,[&](auto,auto){check(!reentry.sync({})&&!reentry.resumePeer(2),"callback cannot mutate scheduler");return DetTransportSendResult::Accepted;}),"reentry protected");
    std::cout<<"PASS egress budgets, atomic sync, independent peers, retry/resend, weighted fairness, clock/rebind/reentry\n";
}
