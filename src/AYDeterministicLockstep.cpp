#include <AYEntity/DeterministicLockstep.h>
#include <stdexcept>

namespace ayt::entity {
namespace {
DetSessionCheckpoint initial(DeterministicSession& s){auto cp=s.checkpoint();if(!cp)throw std::invalid_argument("Lockstep requires sealed quiescent valid session: "+s.error());return *cp;}
DetLockstepBarrier barrier(DeterministicSession& s,DetLockstepConfig c){const auto cp=initial(s);return {std::move(c),cp.manifest,detCheckpointHash(cp),cp.nextTick};}
}
DeterministicLockstep::DeterministicLockstep(DeterministicSession& s,DetLockstepConfig c,TickSink sink)
    :_session(s),_barrier(barrier(s,std::move(c))),_sink(std::move(sink)){}
bool DeterministicLockstep::submit(DetTickInput input) {
    if(faulted())return false;
    try {
        for(const auto& c:input.commands)if(c.source!=_barrier.config().localMember)return reject("Input source not owned by local member");
        std::string e;if(!canonicalizeDetInput(input,e))return reject(e);
        // Version is validated against the session when the frame is assembled.
        return _barrier.submit(input.tick,encodeDetInput(input));
    }catch(const std::exception& e){return reject(e.what());}
}
bool DeterministicLockstep::receive(std::uint32_t member,std::span<const std::uint8_t> packet) {
    return !faulted() && _barrier.receive(member,packet);
}
bool DeterministicLockstep::advance() {
    if(!ready())return false;
    if(_session.nextTick()!=_barrier.nextTick())return reject("Session advanced outside lockstep owner",true);
    try {
        DetTickInput merged;merged.tick=_barrier.nextTick();bool first=true;
        for(const auto& [member,bytes]:_barrier.inputs()){
            DetTickInput frame;std::string e;if(!decodeDetInput(bytes,frame,e))return reject("Malformed member input: "+e,true);
            if(frame.tick!=merged.tick)return reject("Member input tick mismatch",true);
            if(first){merged.version=frame.version;first=false;}else if(merged.version!=frame.version)return reject("Member input version mismatch",true);
            for(const auto& cmd:frame.commands){if(cmd.source!=member)return reject("Member forged command source",true);merged.commands.push_back(cmd);}
        }
        std::string e;if(!canonicalizeDetInput(merged,e))return reject(e,true);
        if(!(_sink?_sink(merged):_session.advance(merged)))return reject("Lockstep tick failed: "+_session.error(),true);
        if(_session.nextTick()!=merged.tick+1)return reject("Tick sink did not advance exactly once",true);
        auto cp=_session.checkpoint();if(!cp)return reject(_session.error(),true);
        if(!_barrier.commit(detCheckpointHash(*cp)))return reject(_barrier.error(),true);
        _error.clear();return true;
    }catch(const std::exception& e){return reject(e.what(),true);}
}
} // namespace ayt::entity
