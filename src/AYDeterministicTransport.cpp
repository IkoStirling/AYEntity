#include <AYEntity/DeterministicTransport.h>
#include <algorithm>
#include <array>
#include <map>
#include <stdexcept>

namespace ayt::entity {
struct DetTransportScheduler::Impl {
    struct Delivery {bool accepted=false;std::uint64_t at=0;};
    struct Entry {DetTransportPriority lane;std::uint64_t serial;std::array<Delivery,8> delivery{};};
    struct Peer {
        DetTransportPeerDiagnostics stats;
        std::uint64_t credit=0,lastTime=0,retryAt=0,cursor[2]{};
        std::uint32_t realtimeRun=0;
    };
    DetTransportConfig cfg;std::map<std::vector<std::uint8_t>,Entry> entries;
    std::vector<Peer> peers;std::uint64_t bytes=0,peak=0,serial=0,time=0;
    bool clockStarted=false,busy=false,failed=false;std::string error;
    bool reject(std::string e,bool fatal=false){error=std::move(e);failed|=fatal;return false;}
    explicit Impl(DetTransportConfig c):cfg(std::move(c)) {
        std::sort(cfg.peers.begin(),cfg.peers.end());
        if(cfg.peers.empty() || cfg.peers.size()>8 || !cfg.peers.front()
            || std::adjacent_find(cfg.peers.begin(),cfg.peers.end())!=cfg.peers.end()
            || !cfg.maxPackets || cfg.maxPackets>65536 || !cfg.maxPacketBytes || cfg.maxPacketBytes>70000
            || cfg.maxBufferedBytes<cfg.maxPacketBytes || cfg.maxBufferedBytes>256ull*1024*1024
            || !cfg.bytesPerSecond || cfg.bytesPerSecond>64u*1024*1024
            || cfg.burstBytes<cfg.maxPacketBytes || cfg.burstBytes>16u*1024*1024
            || cfg.bytesPerPump<cfg.maxPacketBytes || cfg.bytesPerPump>16u*1024*1024
            || !cfg.packetsPerPump || cfg.packetsPerPump>4096 || !cfg.resendMs || !cfg.retryMs
            || !cfg.realtimeWeight || cfg.realtimeWeight>64)throw std::invalid_argument("Invalid deterministic transport budget/roster");
        for(auto id:cfg.peers){Peer p;p.stats.member=id;p.credit=uint64_t(cfg.burstBytes)*1000;peers.push_back(p);}
    }
};
DetTransportScheduler::DetTransportScheduler(DetTransportConfig c):_impl(std::make_unique<Impl>(std::move(c))){}
DetTransportScheduler::~DetTransportScheduler()=default;
bool DetTransportScheduler::sync(std::span<const DetTransportPacket> retained) {
    auto& p=*_impl;if(p.busy || p.failed)return p.reject("Transport sync while busy/faulted");
    if(retained.size()>p.cfg.maxPackets)return p.reject("Transport retained packet budget");
    try {
        decltype(p.entries) next;std::uint64_t bytes=0,serial=p.serial;
        for(const auto& packet:retained) {
            if(packet.bytes.empty() || packet.bytes.size()>p.cfg.maxPacketBytes
                || (packet.priority!=DetTransportPriority::Realtime && packet.priority!=DetTransportPriority::Recovery))
                return p.reject("Invalid transport packet size/priority");
            if(auto duplicate=next.find(packet.bytes);duplicate!=next.end()) {
                if(duplicate->second.lane!=packet.priority)return p.reject("Conflicting transport packet priority");
                continue;
            }
            bytes+=packet.bytes.size();if(bytes>p.cfg.maxBufferedBytes)return p.reject("Transport retained byte budget");
            auto old=p.entries.find(packet.bytes);
            if(old!=p.entries.end()) {
                if(old->second.lane!=packet.priority)return p.reject("Changed transport packet priority");
                next.emplace(packet.bytes,old->second);
            }else {
                if(serial==UINT64_MAX)return p.reject("Transport serial exhausted",true);
                next.emplace(packet.bytes,Impl::Entry{packet.priority,++serial,{}});
            }
        }
        p.entries.swap(next);p.serial=serial;p.bytes=bytes;p.peak=std::max(p.peak,bytes);p.error.clear();return true;
    }catch(const std::exception& e){return p.reject(e.what());}
}
bool DetTransportScheduler::pump(std::uint64_t now,const Sender& send) {
    auto& p=*_impl;if(p.busy || p.failed || !send)return p.reject("Transport pump busy/faulted or missing sender");
    if(p.clockStarted && now<p.time)return p.reject("Transport clock moved backwards");
    struct Guard{bool& b;~Guard(){b=false;}}guard{p.busy};p.busy=true;
    const bool first=!p.clockStarted;p.clockStarted=true;p.time=now;
    try {
        for(std::size_t index=0;index<p.peers.size();++index) {
            auto& peer=p.peers[index];auto& s=peer.stats;s.lastPumpBytes=0;s.lastPumpPackets=0;
            const auto elapsed=first?0:now-peer.lastTime;peer.lastTime=now;
            const auto capacity=uint64_t(p.cfg.burstBytes)*1000;
            // Clamp before multiplication, including callers jumping to UINT64_MAX.
            const auto refill=std::min(elapsed,(capacity+p.cfg.bytesPerSecond-1)/p.cfg.bytesPerSecond)*p.cfg.bytesPerSecond;
            peer.credit=std::min(capacity,peer.credit+refill);
            if(s.disconnected || now<peer.retryAt)continue;
            s.backpressured=false;
            auto pick=[&](unsigned lane)->decltype(p.entries.begin()) {
                for(bool unsent:{true,false}) {
                    auto later=p.entries.end(),wrap=p.entries.end();
                    for(auto it=p.entries.begin();it!=p.entries.end();++it) {
                        auto& e=it->second;const auto& d=e.delivery[index];
                        if(unsigned(e.lane)!=lane || (unsent?d.accepted:(!d.accepted || now-d.at<p.cfg.resendMs)))continue;
                        if(wrap==p.entries.end() || e.serial<wrap->second.serial)wrap=it;
                        if(e.serial>peer.cursor[lane] && (later==p.entries.end() || e.serial<later->second.serial))later=it;
                    }
                    if(later!=p.entries.end())return later;if(wrap!=p.entries.end())return wrap;
                }
                return p.entries.end();
            };
            while(s.lastPumpPackets<p.cfg.packetsPerPump) {
                auto realtime=pick(0),bulk=pick(1),chosen=p.entries.end();
                if(bulk!=p.entries.end() && (peer.realtimeRun>=p.cfg.realtimeWeight || realtime==p.entries.end()))chosen=bulk;
                else chosen=realtime;
                if(chosen==p.entries.end())break;
                const auto size=chosen->first.size();
                if(s.lastPumpBytes+size>p.cfg.bytesPerPump || peer.credit<size*1000)break;
                ++s.lastPumpPackets;s.lastPumpBytes+=size;peer.credit-=size*1000;
                const auto result=send(s.member,chosen->first);
                if(result==DetTransportSendResult::RetryLater) {
                    ++s.retryLater;s.backpressured=true;
                    peer.retryAt=now>UINT64_MAX-p.cfg.retryMs?UINT64_MAX:now+p.cfg.retryMs;break;
                }
                if(result==DetTransportSendResult::Disconnected){s.disconnected=true;break;}
                if(result!=DetTransportSendResult::Accepted)return p.reject("Transport sender rejected/unsupported",true);
                auto& e=chosen->second;auto& d=e.delivery[index];
                ++s.accepted;s.acceptedBytes+=size;if(d.accepted)++s.retransmits;
                d={true,now};peer.cursor[unsigned(e.lane)]=e.serial;
                peer.realtimeRun=e.lane==DetTransportPriority::Recovery?0:std::min(peer.realtimeRun+1,p.cfg.realtimeWeight);
            }
        }
        p.error.clear();return true;
    }catch(const std::exception& e){return p.reject(std::string("Transport sender threw: ")+e.what(),true);}
    catch(...){return p.reject("Transport sender threw",true);}
}
bool DetTransportScheduler::resumePeer(std::uint32_t member) {
    auto& p=*_impl;if(p.busy || p.failed)return false;
    for(std::size_t i=0;i<p.peers.size();++i)if(p.peers[i].stats.member==member) {
        auto& peer=p.peers[i];peer.stats.disconnected=false;peer.stats.backpressured=false;peer.retryAt=0;
        peer.cursor[0]=peer.cursor[1]=0;peer.realtimeRun=0;peer.credit=uint64_t(p.cfg.burstBytes)*1000;
        for(auto& [bytes,e]:p.entries){(void)bytes;e.delivery[i]={};}return true;
    }
    return p.reject("Unknown transport peer");
}
DetTransportDiagnostics DetTransportScheduler::diagnostics() const {
    const auto& p=*_impl;DetTransportDiagnostics d{p.bytes,p.entries.size(),p.peak,{}};
    for(std::size_t i=0;i<p.peers.size();++i) {
        auto s=p.peers[i].stats;s.pendingBytes=s.pendingPackets=0;
        for(const auto& [bytes,e]:p.entries)if(!e.delivery[i].accepted){++s.pendingPackets;s.pendingBytes+=bytes.size();}
        d.peers.push_back(s);
    }
    return d;
}
const std::string& DetTransportScheduler::error() const{return _impl->error;}
bool DetTransportScheduler::faulted() const{return _impl->failed;}
std::vector<DetTransportPacket> detRollbackTransportBatch(std::vector<std::vector<std::uint8_t>> packets) {
    std::vector<DetTransportPacket> out;out.reserve(packets.size());
    auto u32=[](const auto& b,std::size_t at){std::uint32_t v=0;for(unsigned i=0;i<4;++i)v|=std::uint32_t(b[at+i])<<(8*i);return v;};
    for(auto& bytes:packets) {
        if(bytes.size()<48 || bytes.size()>70000 || u32(bytes,0)!=0x42524441 || u32(bytes,4)!=1
            || u32(bytes,36)!=bytes.size()-48 || u32(bytes,24)<1 || u32(bytes,24)>4)
            throw std::invalid_argument("Expected trusted local rollback profile1 packet");
        const auto lane=u32(bytes,24)==4?DetTransportPriority::Recovery:DetTransportPriority::Realtime;
        out.push_back({std::move(bytes),lane});
    }
    return out;
}
std::vector<DetTransportPacket> detLockstepTransportBatch(std::vector<std::vector<std::uint8_t>> packets) {
    std::vector<DetTransportPacket> out;out.reserve(packets.size());
    for(auto& bytes:packets)out.push_back({std::move(bytes),DetTransportPriority::Realtime});return out;
}
} // namespace ayt::entity
