#pragma once
#include <AYEntity/DeterministicSession.h>
#include <AYEntity/DeterministicLockstepProtocol.h>

namespace ayt::entity {
/** @brief Sole tick owner adapting a sealed deterministic session to a peer barrier.
 * @note Resolve admitted transport connections to stable members before receive.
 * Submit explicit tick inputs, including empty no-op frames; command source must
 * equal the submitting member. Send packets() reliably to all other members and
 * periodically retransmit when needed. Receive/advance on the session owner thread.
 * No wall clock or socket enters authoritative state. A failed executing tick or
 * hash disagreement stops this coordinator; recover all peers from an agreed
 * checkpoint and fresh epoch. Optional tick sink must invoke this session exactly
 * once (e.g. DetReplayWriter.advance); it cannot introduce a second tick owner.
 */
class DeterministicLockstep {
public:
    using TickSink=std::function<bool(DetTickInput)>;
    DeterministicLockstep(DeterministicSession& session,DetLockstepConfig config,TickSink sink={});
    bool submit(DetTickInput input);
    bool receive(std::uint32_t authenticatedMember,std::span<const std::uint8_t> packet);
    bool advance();
    bool ready() const {return !_faulted && _barrier.ready();}
    bool synchronized() const {return !_faulted && _barrier.synchronized();}
    bool faulted() const {return _faulted || _barrier.faulted();}
    const std::string& error() const {return _error.empty()?_barrier.error():_error;}
    std::vector<std::uint32_t> missing() const {return _barrier.missing();}
    std::vector<std::vector<std::uint8_t>> packets() const {return faulted()?std::vector<std::vector<std::uint8_t>>{}:_barrier.packets();}
    void disconnect(std::uint32_t member){_barrier.disconnect(member);}
private:
    bool reject(std::string error,bool fault=false){_error=std::move(error);_faulted|=fault;return false;}
    DeterministicSession& _session;
    DetLockstepBarrier _barrier;
    TickSink _sink;
    bool _faulted=false;
    std::string _error;
};
} // namespace ayt::entity
