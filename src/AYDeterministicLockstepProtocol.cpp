#include <AYEntity/DeterministicLockstepProtocol.h>
#include <algorithm>
#include <stdexcept>

namespace ayt::entity {
namespace {
constexpr std::uint64_t maxPayload=65568,maxManifest=65536;
void put(std::vector<std::uint8_t>& b,std::uint64_t v,unsigned n){for(unsigned i=0;i<n;++i)b.push_back(static_cast<std::uint8_t>(v>>(8*i)));}
std::uint64_t checksum(std::span<const std::uint8_t> b){std::uint64_t h=14695981039346656037ull;for(auto v:b)h=(h^v)*1099511628211ull;return h;}
struct Reader {
    std::span<const std::uint8_t> bytes;std::size_t offset=0;
    std::uint64_t get(unsigned n){if(n>bytes.size()-offset)throw std::invalid_argument("truncated lockstep packet");
        std::uint64_t v=0;for(unsigned i=0;i<n;++i)v|=std::uint64_t(bytes[offset++])<<(8*i);return v;}
};
std::vector<std::uint8_t> hashBytes(std::uint64_t value){std::vector<std::uint8_t> b;put(b,value,8);return b;}
}
DetLockstepBarrier::DetLockstepBarrier(DetLockstepConfig c,std::vector<std::uint8_t> manifest,std::uint64_t hash,std::uint64_t tick)
    :_config(std::move(c)),_manifest(std::move(manifest)),_next(tick),_initialTick(tick),_initialHash(hash) {
    auto& members=_config.members;std::sort(members.begin(),members.end());
    if(!_config.sessionId || !_config.epoch || !_config.localMember || members.empty() || members.size()>8 || !members.front()
        || std::adjacent_find(members.begin(),members.end())!=members.end()
        || !std::binary_search(members.begin(),members.end(),_config.localMember) || _config.futureTicks>64 || !_config.historyTicks || _config.historyTicks>64
        || !_config.maxBufferedBytes || _config.maxBufferedBytes>16*1024*1024 || _manifest.empty() || _manifest.size()>maxManifest || tick==UINT64_MAX)
        throw std::invalid_argument("invalid lockstep configuration");
    for(auto id:members)_hello[id]=id==_config.localMember;
    _hashes[tick][_config.localMember]=hash;
}
bool DetLockstepBarrier::reject(std::string e,bool fault){_error=std::move(e);_faulted|=fault;return false;}
std::vector<std::uint8_t> DetLockstepBarrier::helloBody() const {
    std::vector<std::uint8_t> b;put(b,_initialHash,8);put(b,_config.futureTicks,4);put(b,_config.historyTicks,4);
    put(b,_config.maxBufferedBytes,8);put(b,_config.members.size(),4);for(auto id:_config.members)put(b,id,4);
    b.insert(b.end(),_manifest.begin(),_manifest.end());return b;
}
std::vector<std::uint8_t> DetLockstepBarrier::packet(std::uint32_t kind,std::uint64_t tick,std::span<const std::uint8_t> body) const {
    std::vector<std::uint8_t> b;put(b,0x4c534441,4);put(b,1,4);put(b,_config.sessionId,8);put(b,_config.epoch,4);
    put(b,_config.localMember,4);put(b,kind,4);put(b,tick,8);put(b,body.size(),4);b.insert(b.end(),body.begin(),body.end());put(b,checksum(b),8);return b;
}
bool DetLockstepBarrier::acceptInput(std::uint32_t member,std::uint64_t tick,std::span<const std::uint8_t> input) {
    const auto oldest=std::max(_initialTick,_next>_config.historyTicks?_next-_config.historyTicks:0);
    if(tick<oldest || (tick>_next && tick-_next>_config.futureTicks))return reject("lockstep input outside bounded window");
    if(input.size()>maxPayload)return reject("lockstep input size limit");
    auto frame=_inputs.find(tick);
    if(frame!=_inputs.end()){const auto it=frame->second.find(member);if(it!=frame->second.end()){
        if(std::equal(it->second.begin(),it->second.end(),input.begin(),input.end()))return true;
        return reject("conflicting input member="+std::to_string(member)+" tick="+std::to_string(tick),true);}}
    if(input.size()>_config.maxBufferedBytes-_bytes)return reject("lockstep buffered input limit");
    _inputs[tick][member]={input.begin(),input.end()};_bytes+=input.size();return true;
}
bool DetLockstepBarrier::acceptHash(std::uint32_t member,std::uint64_t tick,std::uint64_t hash) {
    const auto oldest=std::max(_initialTick,_next>_config.historyTicks?_next-_config.historyTicks:0);
    if(tick<oldest || (tick>_next && tick-_next>_config.futureTicks+1))return reject("lockstep hash outside bounded window");
    auto& frame=_hashes[tick];auto it=frame.find(member);
    if(it!=frame.end() && it->second!=hash)return reject("conflicting hash member="+std::to_string(member)+" tick="+std::to_string(tick),true);
    frame[member]=hash;
    auto local=frame.find(_config.localMember);
    if(local!=frame.end())for(const auto& [peer,value]:frame)if(value!=local->second)
        return reject("state divergence member="+std::to_string(peer)+" nextTick="+std::to_string(tick)+" expected="+std::to_string(local->second)+" actual="+std::to_string(value),true);
    return true;
}
bool DetLockstepBarrier::submit(std::uint64_t tick,std::span<const std::uint8_t> input) {
    if(_faulted)return false;if(tick<_next)return reject("local input already consumed");
    return acceptInput(_config.localMember,tick,input);
}
bool DetLockstepBarrier::receive(std::uint32_t member,std::span<const std::uint8_t> bytes) {
    if(_faulted)return false;
    try {
        if(bytes.size()<48 || bytes.size()>maxManifest+112)return reject("lockstep packet size limit");
        Reader trailer{bytes.last(8)};if(trailer.get(8)!=checksum(bytes.first(bytes.size()-8)))return reject("lockstep checksum mismatch");
        Reader r{bytes.first(bytes.size()-8)};
        if(r.get(4)!=0x4c534441 || r.get(4)!=1 || r.get(8)!=_config.sessionId || r.get(4)!=_config.epoch)return reject("foreign lockstep session/epoch/profile");
        const auto sender=r.get(4),kind=r.get(4),tick=r.get(8),size=r.get(4);
        if(sender!=member || member==_config.localMember || !_hello.contains(member))return reject("unbound lockstep sender");
        if(size!=r.bytes.size()-r.offset)return reject("lockstep payload shape");auto body=r.bytes.subspan(r.offset);
        if(kind==1){const auto expected=helloBody();
            if(tick!=_initialTick || !std::equal(expected.begin(),expected.end(),body.begin(),body.end()))return reject("lockstep handshake manifest/roster/state mismatch",true);
            _hello[member]=true;return _hashes.contains(_initialTick)?acceptHash(member,_initialTick,_initialHash):true;}
        if(kind==2)return acceptInput(member,tick,body);
        if(kind==3){if(body.size()!=8)return reject("lockstep hash shape");Reader h{body};return acceptHash(member,tick,h.get(8));}
        return reject("unknown lockstep packet kind");
    }catch(const std::exception& e){return reject(e.what());}
}
bool DetLockstepBarrier::ready() const {
    const auto input=_inputs.find(_next);
    return synchronized() && input!=_inputs.end() && input->second.size()==_hello.size();
}
bool DetLockstepBarrier::synchronized() const {
    if(_faulted || std::any_of(_hello.begin(),_hello.end(),[](const auto& p){return !p.second;}))return false;
    const auto hash=_hashes.find(_next);return hash!=_hashes.end() && hash->second.size()==_hello.size();
}
std::map<std::uint32_t,std::vector<std::uint8_t>> DetLockstepBarrier::inputs() const {
    if(!ready())throw std::logic_error("lockstep barrier not ready");return _inputs.at(_next);
}
bool DetLockstepBarrier::commit(std::uint64_t hash) {
    if(!ready() || _next==UINT64_MAX)return reject("lockstep commit not ready");++_next;
    const auto oldest=std::max(_initialTick,_next>_config.historyTicks?_next-_config.historyTicks:0);
    while(!_inputs.empty() && _inputs.begin()->first<oldest){for(const auto& [id,b]:_inputs.begin()->second)_bytes-=b.size();_inputs.erase(_inputs.begin());}
    while(!_hashes.empty() && _hashes.begin()->first<oldest)_hashes.erase(_hashes.begin());
    return acceptHash(_config.localMember,_next,hash);
}
std::vector<std::vector<std::uint8_t>> DetLockstepBarrier::packets() const {
    if(_faulted)return {};std::vector<std::vector<std::uint8_t>> result{packet(1,_initialTick,helloBody())};
    for(const auto& [tick,frame]:_inputs){const auto it=frame.find(_config.localMember);if(it!=frame.end())result.push_back(packet(2,tick,it->second));}
    for(const auto& [tick,frame]:_hashes){const auto it=frame.find(_config.localMember);if(it!=frame.end())result.push_back(packet(3,tick,hashBytes(it->second)));}
    return result;
}
void DetLockstepBarrier::disconnect(std::uint32_t member){if(_hello.contains(member))reject("lockstep member disconnected: "+std::to_string(member),true);}
std::vector<std::uint32_t> DetLockstepBarrier::missing() const {
    std::vector<std::uint32_t> out;
    const auto input=_inputs.find(_next);const auto hash=_hashes.find(_next);
    for(const auto& [id,hello]:_hello)if(!hello || input==_inputs.end() || !input->second.contains(id) || hash==_hashes.end() || !hash->second.contains(id))out.push_back(id);
    return out;
}
} // namespace ayt::entity
