#pragma once
#include <cstdint>
#include <map>
#include <span>
#include <string>
#include <vector>

namespace ayt::entity {
struct DetLockstepConfig {
    std::uint64_t sessionId=0;
    std::uint32_t epoch=1,localMember=0;
    std::vector<std::uint32_t> members;
    std::uint32_t futureTicks=8,historyTicks=8;
    std::uint64_t maxBufferedBytes=4*1024*1024;
};
/** @brief Transport-independent fixed-roster input and state-hash barrier.
 * @note Single owner thread. Identity passed to receive must come from admitted,
 * authenticated transport membership, never from packet claims. Explicit LE/FNV
 * bounds detect corruption, not authentication. Missing input/hash stalls. One
 * input contribution (possibly empty) per member/tick; identical duplicates are
 * idempotent, conflicts and manifest/hash mismatches fault. No prediction or
 * membership change. Recreate from an agreed checkpoint/new epoch for recovery.
 */
class DetLockstepBarrier {
public:
    DetLockstepBarrier(DetLockstepConfig config,std::vector<std::uint8_t> manifest,
        std::uint64_t initialHash,std::uint64_t initialTick=0);
    bool submit(std::uint64_t tick,std::span<const std::uint8_t> input);
    bool receive(std::uint32_t authenticatedMember,std::span<const std::uint8_t> packet);
    bool ready() const;
    bool synchronized() const;
    /// Current canonical contributions, available only when ready(); copies by member ID.
    std::map<std::uint32_t,std::vector<std::uint8_t>> inputs() const;
    /// Commit after the simulation accepted the current inputs. Publishes next hash.
    bool commit(std::uint64_t postTickHash);
    /// Bounded retransmission set: hello, retained local contributions and hashes.
    std::vector<std::vector<std::uint8_t>> packets() const;
    void disconnect(std::uint32_t member);
    std::vector<std::uint32_t> missing() const;
    std::uint64_t nextTick() const {return _next;}
    bool faulted() const {return _faulted;}
    const std::string& error() const {return _error;}
    const DetLockstepConfig& config() const {return _config;}
private:
    bool acceptInput(std::uint32_t member,std::uint64_t tick,std::span<const std::uint8_t> input);
    bool acceptHash(std::uint32_t member,std::uint64_t tick,std::uint64_t hash);
    bool reject(std::string error,bool fault=false);
    std::vector<std::uint8_t> packet(std::uint32_t kind,std::uint64_t tick,std::span<const std::uint8_t> body) const;
    std::vector<std::uint8_t> helloBody() const;
    DetLockstepConfig _config;
    std::vector<std::uint8_t> _manifest;
    std::uint64_t _next=0,_initialTick=0,_initialHash=0,_bytes=0;
    std::map<std::uint32_t,bool> _hello;
    std::map<std::uint64_t,std::map<std::uint32_t,std::vector<std::uint8_t>>> _inputs;
    std::map<std::uint64_t,std::map<std::uint32_t,std::uint64_t>> _hashes;
    bool _faulted=false;
    std::string _error;
};
} // namespace ayt::entity
