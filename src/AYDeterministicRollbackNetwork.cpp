#include <AYEntity/DeterministicRollback.h>
#include <algorithm>
#include <deque>
#include <stdexcept>
#include <tuple>

namespace ayt::entity {
namespace {
constexpr std::uint32_t chunkSize=16384, magic=0x42524441;
std::uint64_t hashBytes(std::span<const std::uint8_t> b) {
    std::uint64_t h=14695981039346656037ull;
    for(auto v:b){h^=v;h*=1099511628211ull;}return h;
}
void put(std::vector<std::uint8_t>& b,std::uint64_t v,unsigned n) {
    for(unsigned i=0;i<n;++i)b.push_back(static_cast<std::uint8_t>(v>>(8*i)));
}
struct Reader {
    std::span<const std::uint8_t> bytes;std::size_t offset=0;
    std::uint64_t get(unsigned n) {
        if(n>bytes.size()-offset)throw std::invalid_argument("Truncated rollback network packet");
        std::uint64_t v=0;for(unsigned i=0;i<n;++i)v|=std::uint64_t(bytes[offset++])<<(8*i);return v;
    }
};
}
struct DeterministicRollbackNetwork::Impl {
    DeterministicSession& session;
    DetRollbackNetworkConfig cfg;
    std::unique_ptr<DeterministicRollback> core;
    std::uint64_t initial=0,initialHash=0,verified=0,effectsThrough=0;
    std::uint64_t journalNext=0,journalStart=0;
    std::deque<DetConfirmedEvent> journal,recoveredEffects;
    std::map<std::uint32_t,bool> hello;
    std::map<std::uint64_t,std::map<std::uint32_t,std::uint64_t>> hashes;
    bool failed=false,busy=false;
    std::string error;
    struct Transfer {
        std::uint32_t epoch=0;
        std::uint64_t tick=0,hash=0;
        std::vector<std::uint8_t> bytes;
        std::vector<bool> seen;
    } incoming;
    std::vector<std::uint8_t> outgoing;
    Impl(DeterministicSession& s,DetRollbackNetworkConfig c):session(s),cfg(std::move(c)) {
        core=std::make_unique<DeterministicRollback>(s,cfg.rollback,cfg.epoch);cfg.rollback=core->config();
        if(!cfg.sessionId || !member(cfg.localMember) || !member(cfg.recoveryMember)
            || cfg.inputDelay>8 || cfg.inputDelay>cfg.rollback.futureTicks
            || cfg.maxTransferBytes<1024 || cfg.maxTransferBytes>16*1024*1024)
            throw std::invalid_argument("Invalid rollback network configuration");
        const auto cp=*s.checkpoint();initial=verified=effectsThrough=journalNext=journalStart=cp.nextTick;initialHash=detCheckpointHash(cp);
        initialize();
    }
    bool member(std::uint32_t id) const {return std::binary_search(cfg.rollback.members.begin(),cfg.rollback.members.end(),id);}
    bool reject(std::string text,bool fault=false){error=std::move(text);failed|=fault;return false;}
    bool faulted() const {return failed || core->faulted();}
    std::uint64_t localTick() const {
        if(session.nextTick()>UINT64_MAX-cfg.inputDelay)throw std::overflow_error("Delayed input tick overflow");
        return session.nextTick()+cfg.inputDelay;
    }
    void initialize() {
        hello.clear();hashes.clear();
        for(auto id:cfg.rollback.members)hello[id]=id==cfg.localMember;
        hashes[initial][cfg.localMember]=initialHash;
        if(initial>UINT64_MAX-cfg.inputDelay)throw std::invalid_argument("Bootstrap tick overflow");
        for(std::uint64_t tick=initial;tick<initial+cfg.inputDelay;++tick)
            if(!core->submit(cfg.localMember,{tick,session.inputVersion(),{}}))throw std::runtime_error(core->error());
    }
    std::vector<std::uint8_t> helloBody() const {
        std::vector<std::uint8_t> b;
        put(b,initialHash,8);put(b,cfg.recoveryMember,4);put(b,cfg.inputDelay,4);
        put(b,cfg.rollback.historyTicks,4);put(b,cfg.rollback.maxPredictionTicks,4);put(b,cfg.rollback.futureTicks,4);
        put(b,cfg.rollback.maxBufferedBytes,8);put(b,cfg.maxTransferBytes,4);
        put(b,cfg.rollback.members.size(),4);for(auto id:cfg.rollback.members)put(b,id,4);
        put(b,cfg.rollback.prediction.size(),4);
        for(const auto& r:cfg.rollback.prediction){put(b,r.type,4);put(b,static_cast<std::uint32_t>(r.mode),4);}
        put(b,session.manifest().size(),4);b.insert(b.end(),session.manifest().begin(),session.manifest().end());return b;
    }
    std::vector<std::uint8_t> packet(std::uint32_t kind,std::uint64_t tick,std::span<const std::uint8_t> body) const {
        std::vector<std::uint8_t> b;put(b,magic,4);put(b,1,4);put(b,cfg.sessionId,8);put(b,cfg.epoch,4);
        put(b,cfg.localMember,4);put(b,kind,4);put(b,tick,8);put(b,body.size(),4);
        b.insert(b.end(),body.begin(),body.end());put(b,hashBytes(b),8);return b;
    }
    bool handshaken() const {return std::all_of(hello.begin(),hello.end(),[](const auto& p){return p.second;});}
    bool update() {
        if(core->faulted())return reject(core->error(),true);
        const auto end=core->confirmedNextTick();
        while(journalNext<end) {
            const auto cp=core->checkpointAt(journalNext+1);
            if(!cp)return reject("Confirmed effect journal checkpoint was evicted",true);
            for(const auto& c:cp->pendingEvents)journal.push_back({cfg.epoch,journalNext,c});
            ++journalNext;
        }
        const std::uint64_t keep=cfg.rollback.historyTicks+cfg.rollback.futureTicks+cfg.rollback.maxPredictionTicks+cfg.inputDelay+1;
        journalStart=std::max(journalStart,end>keep?end-keep:0);
        while(!journal.empty() && journal.front().tick<journalStart)journal.pop_front();
        std::uint64_t journalBytes=0;
        for(const auto& e:journal)journalBytes+=28+e.command.payload.size();
        if(journalBytes>cfg.maxTransferBytes || journal.size()>65536)return reject("Confirmed recovery event journal budget",true);
        for(auto tick=std::max(initial,core->oldestTick());tick<=end;++tick) {
            if(!hashes[tick].contains(cfg.localMember)) {
                auto cp=core->checkpointAt(tick);if(cp)hashes[tick][cfg.localMember]=detCheckpointHash(*cp);
            }
            if(tick==UINT64_MAX)break;
        }
        for(const auto& [tick,values]:hashes)if(tick<=end && values.contains(cfg.localMember))
            for(const auto& [id,value]:values)if(value!=values.at(cfg.localMember))
                return reject("Confirmed state divergence at nextTick="+std::to_string(tick)+" member="+std::to_string(id),true);
        if(handshaken())while(verified<end) {
            auto f=hashes.find(verified+1);
            if(f==hashes.end() || f->second.size()!=hello.size())break;
            ++verified;
        }
        const auto floor=verified>cfg.rollback.historyTicks?verified-cfg.rollback.historyTicks:initial;
        while(!hashes.empty() && hashes.begin()->first<floor)hashes.erase(hashes.begin());
        if(handshaken())outgoing.clear();
        error.clear();return true;
    }
    bool recover(const DetSessionCheckpoint& cp,std::uint32_t epoch,std::uint64_t start,
        const std::deque<DetConfirmedEvent>& events) {
        if(cp.manifest!=session.manifest() || cp.nextTick<effectsThrough || epoch<=cfg.epoch)
            return reject("Recovery manifest/epoch/presentation frontier mismatch");
        if(effectsThrough<start)return reject("Recovery event journal is older than the undelivered frontier");
        if(encodeDetCheckpoint(cp).size()+std::uint64_t(cfg.inputDelay)*32>cfg.rollback.maxBufferedBytes)
            return reject("Recovery exceeds history budget");
        if(cp.nextTick>UINT64_MAX-cfg.inputDelay)return reject("Recovery tick overflow");
        std::deque<DetConfirmedEvent> pending;
        for(const auto& e:events)if(e.tick>=effectsThrough)pending.push_back(e);
        auto history=events;
        std::map<std::uint32_t,bool> newHello;
        for(auto id:cfg.rollback.members)newHello[id]=id==cfg.localMember;
        const auto newHash=detCheckpointHash(cp);
        std::map<std::uint64_t,std::map<std::uint32_t,std::uint64_t>> newHashes;
        newHashes[cp.nextTick][cfg.localMember]=newHash;
        const auto before=core->checkpointAt(session.nextTick());
        if(!before)return reject("Recovery pre-operation checkpoint unavailable");
        if(!session.restore(cp))return reject(session.error());
        try {
            auto candidate=std::make_unique<DeterministicRollback>(session,cfg.rollback,epoch);
            for(auto tick=cp.nextTick;tick<cp.nextTick+cfg.inputDelay;++tick)
                if(!candidate->submit(cfg.localMember,{tick,session.inputVersion(),{}}))throw std::runtime_error(candidate->error());
            core=std::move(candidate);cfg.epoch=epoch;initial=verified=cp.nextTick;initialHash=newHash;
            hello.swap(newHello);hashes.swap(newHashes);
            recoveredEffects=std::move(pending);journal=std::move(history);journalStart=start;journalNext=cp.nextTick;
            failed=false;error.clear();return true;
        }catch(const std::exception& e) {
            const std::string diagnostic=e.what();
            if(!session.restore(*before))return reject(diagnostic+"; recovery restoration failed: "+session.error(),true);
            return reject(diagnostic,true);
        }
    }
    bool chunk(std::uint32_t sender,std::uint32_t epoch,std::uint64_t tick,std::span<const std::uint8_t> b) {
        if(sender!=cfg.recoveryMember || sender==cfg.localMember)return reject("Untrusted recovery authority");
        if(epoch<=cfg.epoch)return true;
        Reader r{b};const auto total=r.get(4),hash=r.get(8),offset=r.get(4);
        const auto data=b.subspan(r.offset);
        if(total<32 || total>cfg.maxTransferBytes || offset>=total || offset%chunkSize
            || data.size()!=std::min<std::uint64_t>(chunkSize,total-offset))return reject("Invalid bounded checkpoint chunk");
        if(incoming.epoch && epoch<incoming.epoch)return true;
        if(incoming.epoch!=epoch) {
            Transfer t;t.epoch=epoch;t.tick=tick;t.hash=hash;t.bytes.resize(static_cast<std::size_t>(total));
            t.seen.resize(static_cast<std::size_t>((total+chunkSize-1)/chunkSize));incoming=std::move(t);
        }
        if(incoming.tick!=tick || incoming.hash!=hash || incoming.bytes.size()!=total)
            return reject("Conflicting recovery transfer",true);
        const auto index=static_cast<std::size_t>(offset/chunkSize);
        auto target=incoming.bytes.begin()+static_cast<std::ptrdiff_t>(offset);
        if(incoming.seen[index] && !std::equal(data.begin(),data.end(),target))return reject("Conflicting checkpoint chunk",true);
        std::copy(data.begin(),data.end(),target);incoming.seen[index]=true;
        if(!std::all_of(incoming.seen.begin(),incoming.seen.end(),[](bool v){return v;}))return true;
        if(hashBytes(incoming.bytes)!=hash){incoming={};return reject("Checkpoint transfer checksum mismatch");}
        Reader bundle{incoming.bytes};
        if(bundle.get(4)!=0x43524441 || bundle.get(4)!=1){incoming={};return reject("Invalid recovery bundle profile");}
        const auto cpSize=bundle.get(4);
        if(cpSize>bundle.bytes.size()-bundle.offset){incoming={};return reject("Truncated recovery checkpoint");}
        const auto cpBytes=bundle.bytes.subspan(bundle.offset,static_cast<std::size_t>(cpSize));bundle.offset+=static_cast<std::size_t>(cpSize);
        DetSessionCheckpoint cp;std::string diagnostic;
        if(!decodeDetCheckpoint(cpBytes,cp,diagnostic) || cp.nextTick!=tick
            || encodeDetCheckpoint(cp)!=std::vector<std::uint8_t>(cpBytes.begin(),cpBytes.end()))
            {incoming={};return reject("Invalid recovery checkpoint: "+diagnostic);}
        const auto start=bundle.get(8),count=bundle.get(4);
        if(start>tick || count>65536){incoming={};return reject("Invalid recovery journal bounds");}
        std::deque<DetConfirmedEvent> events;
        for(std::uint64_t i=0;i<count;++i) {
            DetConfirmedEvent e;e.epoch=static_cast<std::uint32_t>(bundle.get(4));e.tick=bundle.get(8);
            e.command.source=static_cast<std::uint32_t>(bundle.get(4));e.command.sequence=static_cast<std::uint32_t>(bundle.get(4));
            e.command.type=static_cast<std::uint32_t>(bundle.get(4));const auto length=bundle.get(4);
            if(!e.epoch || e.epoch>=epoch || e.tick<start || e.tick>=tick || !e.command.source || !e.command.type
                || length>65536 || length>bundle.bytes.size()-bundle.offset)return reject("Invalid recovery event");
            if(!events.empty()) {
                const auto& last=events.back();
                if(std::tuple(e.tick,e.command.source,e.command.sequence)<=std::tuple(last.tick,last.command.source,last.command.sequence))
                    return reject("Non-canonical recovery event order");
            }
            const auto payload=bundle.bytes.subspan(bundle.offset,static_cast<std::size_t>(length));bundle.offset+=static_cast<std::size_t>(length);
            e.command.payload.assign(payload.begin(),payload.end());events.push_back(std::move(e));
        }
        if(bundle.offset!=bundle.bytes.size())return reject("Recovery bundle trailing bytes");
        const auto ok=recover(cp,epoch,start,events);incoming={};return ok;
    }
};
DeterministicRollbackNetwork::DeterministicRollbackNetwork(DeterministicSession& s,DetRollbackNetworkConfig c)
    :_impl(std::make_unique<Impl>(s,std::move(c))) {}
DeterministicRollbackNetwork::~DeterministicRollbackNetwork()=default;
std::uint64_t DeterministicRollbackNetwork::localInputTick() const{return _impl->localTick();}
bool DeterministicRollbackNetwork::submitLocal(DetTickInput in) {
    auto& p=*_impl;if(p.faulted() || p.busy)return false;
    if(in.tick!=p.localTick())return p.reject("Local sample must target localInputTick");
    struct Guard{bool& b;~Guard(){b=false;}}guard{p.busy};p.busy=true;
    if(!p.core->submit(p.cfg.localMember,std::move(in)))return p.reject(p.core->error(),p.core->faulted());
    return p.update();
}
bool DeterministicRollbackNetwork::receive(std::uint32_t sender,std::span<const std::uint8_t> bytes) {
    auto& p=*_impl;if(p.busy)return p.reject("Rollback network reentry");
    struct Guard{bool& b;~Guard(){b=false;}}guard{p.busy};p.busy=true;
    try {
        if(bytes.size()<48 || bytes.size()>70000)return p.reject("Rollback packet size limit");
        Reader trailer{bytes.last(8)};if(trailer.get(8)!=hashBytes(bytes.first(bytes.size()-8)))return p.reject("Rollback packet checksum mismatch");
        Reader r{bytes.first(bytes.size()-8)};
        if(r.get(4)!=magic || r.get(4)!=1 || r.get(8)!=p.cfg.sessionId)return p.reject("Foreign rollback session/profile");
        const auto epoch=static_cast<std::uint32_t>(r.get(4));const auto claimed=r.get(4),kind=r.get(4),tick=r.get(8),size=r.get(4);
        if(claimed!=sender || sender==p.cfg.localMember || !p.member(sender))return p.reject("Unbound rollback sender");
        if(size!=r.bytes.size()-r.offset)return p.reject("Rollback packet payload shape");
        auto body=r.bytes.subspan(r.offset);
        if(kind==4)return p.chunk(sender,epoch,tick,body);
        if(epoch!=p.cfg.epoch)return true; // Retained old packets and future hellos wait for complete recovery.
        if(p.faulted())return false;
        if(kind==1) {
            const auto expected=p.helloBody();
            if(tick!=p.initial || !std::equal(expected.begin(),expected.end(),body.begin(),body.end()))
                return p.reject("Rollback handshake manifest/state/prediction mismatch",true);
            p.hello[sender]=true;if(p.hashes.contains(p.initial))p.hashes[p.initial][sender]=p.initialHash;
        }else if(kind==2) {
            DetTickInput in;std::string diagnostic;
            if(!decodeDetInput(body,in,diagnostic) || in.tick!=tick)return p.reject("Invalid rollback contribution: "+diagnostic);
            if(tick<p.core->oldestTick())return true;
            if(!p.core->submit(sender,std::move(in)))return p.reject(p.core->error(),p.core->faulted());
        }else if(kind==3) {
            if(body.size()!=8)return p.reject("Rollback hash shape");
            if(tick<p.core->oldestTick())return true;
            if(tick>p.session.nextTick() && tick-p.session.nextTick()>p.cfg.rollback.futureTicks+1)
                return p.reject("Rollback hash outside bounded window");
            Reader h{body};const auto value=h.get(8);auto& f=p.hashes[tick];
            if(f.contains(sender) && f.at(sender)!=value)return p.reject("Conflicting confirmed hash",true);
            f[sender]=value;
        }else return p.reject("Unknown rollback packet kind");
        return p.update();
    }catch(const std::exception& e){return p.reject(e.what());}
}
bool DeterministicRollbackNetwork::ready() const {
    const auto& p=*_impl;
    return !p.faulted() && !p.busy && p.handshaken() && p.core->hasInput(p.cfg.localMember,p.localTick())
        && p.session.nextTick()-p.verified<p.cfg.rollback.historyTicks-1 && p.core->ready();
}
bool DeterministicRollbackNetwork::advance() {
    auto& p=*_impl;if(!ready())return false;
    struct Guard{bool& b;~Guard(){b=false;}}guard{p.busy};p.busy=true;
    if(!p.core->advance())return p.reject(p.core->error(),p.core->faulted());return p.update();
}
bool DeterministicRollbackNetwork::synchronized() const {
    const auto& p=*_impl;return !p.faulted() && p.handshaken() && p.verified==p.session.nextTick();
}
std::uint64_t DeterministicRollbackNetwork::confirmedNextTick() const{return _impl->core->confirmedNextTick();}
std::uint64_t DeterministicRollbackNetwork::verifiedNextTick() const{return _impl->verified;}
std::vector<std::uint32_t> DeterministicRollbackNetwork::missing() const {
    const auto& p=*_impl;std::vector<std::uint32_t> out;
    for(const auto& [id,known]:p.hello)if(!known || !p.core->hasInput(id,p.session.nextTick()))out.push_back(id);return out;
}
std::vector<std::vector<std::uint8_t>> DeterministicRollbackNetwork::packets() const {
    const auto& p=*_impl;if(p.busy || p.faulted())return {};
    std::vector<std::vector<std::uint8_t>> out{p.packet(1,p.initial,p.helloBody())};
    for(const auto& input:p.core->actualInputs(p.cfg.localMember))out.push_back(p.packet(2,input.tick,encodeDetInput(input)));
    for(const auto& [tick,f]:p.hashes)if(f.contains(p.cfg.localMember)) {
        std::vector<std::uint8_t> b;put(b,f.at(p.cfg.localMember),8);out.push_back(p.packet(3,tick,b));
    }
    const auto transferHash=hashBytes(p.outgoing);
    if(!p.outgoing.empty())for(std::size_t offset=0;offset<p.outgoing.size();offset+=chunkSize) {
        std::vector<std::uint8_t> b;put(b,p.outgoing.size(),4);put(b,transferHash,8);put(b,offset,4);
        const auto count=std::min<std::size_t>(chunkSize,p.outgoing.size()-offset);
        b.insert(b.end(),p.outgoing.begin()+static_cast<std::ptrdiff_t>(offset),p.outgoing.begin()+static_cast<std::ptrdiff_t>(offset+count));
        out.push_back(p.packet(4,p.initial,b));
    }
    return out;
}
std::vector<DetConfirmedEvent> DeterministicRollbackNetwork::takeConfirmedEvents() {
    auto& p=*_impl;if(p.busy || p.faulted())return {};
    if(!p.handshaken())return {};
    std::vector<DetConfirmedEvent> result(p.recoveredEffects.begin(),p.recoveredEffects.end());
    auto fresh=p.core->takeConfirmedEvents(p.verified);result.insert(result.end(),fresh.begin(),fresh.end());
    p.recoveredEffects.clear();p.effectsThrough=std::max(p.effectsThrough,p.verified);return result;
}
bool DeterministicRollbackNetwork::beginRecovery(std::uint32_t epoch) {
    auto& p=*_impl;if(p.busy || p.cfg.localMember!=p.cfg.recoveryMember || epoch<=p.cfg.epoch)return false;
    struct Guard{bool& b;~Guard(){b=false;}}guard{p.busy};p.busy=true;
    try {
        auto cp=p.core->checkpointAt(p.core->confirmedNextTick());if(!cp)return p.reject("Confirmed recovery checkpoint not retained");
        const auto cpBytes=encodeDetCheckpoint(*cp);
        std::vector<std::uint8_t> bytes;put(bytes,0x43524441,4);put(bytes,1,4);put(bytes,cpBytes.size(),4);
        bytes.insert(bytes.end(),cpBytes.begin(),cpBytes.end());put(bytes,p.journalStart,8);put(bytes,p.journal.size(),4);
        for(const auto& e:p.journal) {
            put(bytes,e.epoch,4);put(bytes,e.tick,8);put(bytes,e.command.source,4);put(bytes,e.command.sequence,4);
            put(bytes,e.command.type,4);put(bytes,e.command.payload.size(),4);bytes.insert(bytes.end(),e.command.payload.begin(),e.command.payload.end());
        }
        if(bytes.size()>p.cfg.maxTransferBytes)return p.reject("Recovery transfer budget");
        const auto events=p.journal;
        if(!p.recover(*cp,epoch,p.journalStart,events))return false;p.outgoing=std::move(bytes);p.incoming={};return true;
    }catch(const std::exception& e){return p.reject(e.what(),true);}
}
void DeterministicRollbackNetwork::disconnect(std::uint32_t id) {if(_impl->member(id))_impl->reject("Rollback member disconnected: "+std::to_string(id),true);}
bool DeterministicRollbackNetwork::faulted() const{return _impl->faulted();}
const std::string& DeterministicRollbackNetwork::error() const{return _impl->error.empty()?_impl->core->error():_impl->error;}
const DetRollbackNetworkConfig& DeterministicRollbackNetwork::config() const{return _impl->cfg;}
const DeterministicRollback& DeterministicRollbackNetwork::history() const{return *_impl->core;}
DetRollbackDiagnostics DeterministicRollbackNetwork::diagnostics() const {
    auto d=_impl->core->diagnostics();d.verified=_impl->verified;return d;
}
std::uint64_t DeterministicRollbackNetwork::epochInitialTick() const{return _impl->initial;}
std::uint64_t DeterministicRollbackNetwork::recoveryJournalStart() const{return _impl->journalStart;}
std::vector<DetConfirmedEvent> DeterministicRollbackNetwork::recoveryJournal() const {
    return {_impl->journal.begin(),_impl->journal.end()};
}
} // namespace ayt::entity
