#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace ayt::entity {
enum class DetTransportPriority : std::uint8_t { Realtime, Recovery };
enum class DetTransportSendResult : std::uint8_t { Accepted, RetryLater, Disconnected, Rejected };
struct DetTransportPacket {
    std::vector<std::uint8_t> bytes;
    DetTransportPriority priority=DetTransportPriority::Realtime;
};
struct DetTransportConfig {
    std::vector<std::uint32_t> peers; // Stable remote member IDs, never transport IDs.
    std::uint32_t maxPackets=4096,maxPacketBytes=70000;
    std::uint64_t maxBufferedBytes=8*1024*1024;
    std::uint32_t bytesPerSecond=128*1024,burstBytes=128*1024;
    std::uint32_t bytesPerPump=128*1024,packetsPerPump=64;
    std::uint32_t resendMs=250,retryMs=10,realtimeWeight=4;
};
struct DetTransportPeerDiagnostics {
    std::uint32_t member=0;
    std::uint64_t accepted=0,acceptedBytes=0,retransmits=0,retryLater=0;
    std::uint64_t pendingPackets=0,pendingBytes=0,lastPumpBytes=0,lastPumpPackets=0;
    bool backpressured=false,disconnected=false;
};
struct DetTransportDiagnostics {
    std::uint64_t retainedBytes=0,retainedPackets=0,peakBytes=0;
    std::vector<DetTransportPeerDiagnostics> peers;
};
/** @brief Owner-thread bounded egress scheduling outside deterministic Sim.
 * @note sync atomically replaces retained payloads and preserves per-peer accepted
 * timestamps for identical bytes. Payload budget excludes metadata/heap/wire;
 * replacement may hold two bounded sets. Pump once per external frame with caller
 * monotonic milliseconds, independent peer rate/burst and attempt budgets; failed
 * attempts consume budget too. Accepted means queued, not delivered. Weighted
 * realtime/recovery cursors serve unsent before resends within each lane; selected
 * large packets accumulate credit. Closed peers park until authenticated rebind
 * and resumePeer. No implicit timeout, recovery or Session mutation.
 */
class DetTransportScheduler {
public:
    explicit DetTransportScheduler(DetTransportConfig config);
    ~DetTransportScheduler();
    DetTransportScheduler(const DetTransportScheduler&)=delete;
    DetTransportScheduler& operator=(const DetTransportScheduler&)=delete;
    bool sync(std::span<const DetTransportPacket> retained);
    using Sender=std::function<DetTransportSendResult(std::uint32_t,std::span<const std::uint8_t>)>;
    bool pump(std::uint64_t nowMs,const Sender& send);
    bool resumePeer(std::uint32_t member);
    DetTransportDiagnostics diagnostics() const;
    const std::string& error() const;
    bool faulted() const;
private:
    struct Impl;std::unique_ptr<Impl> _impl;
};
/// Classify trusted local rollback packets: hello/input/hash are realtime,
/// recovery chunks are bulk. Checks profile/shape, not admission/authentication.
std::vector<DetTransportPacket> detRollbackTransportBatch(std::vector<std::vector<std::uint8_t>> packets);
/// Lockstep has no bulk lane; use the same scheduler and checked adapter.
std::vector<DetTransportPacket> detLockstepTransportBatch(std::vector<std::vector<std::uint8_t>> packets);
} // namespace ayt::entity
