#pragma once
#include <AYEntity/DeterministicLockstep.h>
#include <AYEntity/DeterministicRollback.h>
#include <AYEntity/DeterministicTransport.h>
#include <AYNetwork/INetwork.h>

namespace ayt::entity {
struct DetTransportPeer {std::uint32_t member;net::NetConnection* connection;};
/** @brief Flush one external frame of the scheduler through checked AYNetwork sends.
 * @note Map admitted authenticated stable member IDs on every call; missing/null
 * links retry later, actual closed links park until explicit scheduler.resumePeer.
 * Reserve the application channel. Unsupported/invalid adapters fault scheduling.
 * Ceiling is encoded pending + unacked bytes, separate from payload rate budgets.
 */
inline bool pumpDetTransport(DetTransportScheduler& scheduler,std::uint64_t nowMs,
    net::INetworkSubSystem& network,std::span<const DetTransportPeer> peers,
    std::uint8_t channel=net::CHANNEL_RELIABLE,std::uint32_t maxQueuedBytes=256*1024) {
    return scheduler.pump(nowMs,[&](auto member,auto bytes){
        for(const auto& peer:peers)if(peer.member==member) {
            if(!peer.connection)return DetTransportSendResult::RetryLater;
            switch(network.trySendTo(peer.connection,channel,bytes.data(),bytes.size(),maxQueuedBytes)) {
            case net::NetSendResult::Accepted:return DetTransportSendResult::Accepted;
            case net::NetSendResult::RetryLater:return DetTransportSendResult::RetryLater;
            case net::NetSendResult::Disconnected:return DetTransportSendResult::Disconnected;
            default:return DetTransportSendResult::Rejected;
            }
        }
        return DetTransportSendResult::RetryLater;
    });
}
/** @brief Send the bounded lockstep retransmission set over admitted AYNetwork links.
 * @note Link AYNetwork + AYEntity::Determinism. Application owns connection/member
 * mapping, message handler and resend/stall schedule. Resolve stable membership
 * after admission; receive handler forwards authenticated ID to lockstep.receive.
 * Reserve an application channel and use the same channel on all peers. AYNetwork
 * sendTo applies its PacketCodec envelope; do not use sendEncodedTo here. Flush on
 * the owner thread after network ingress, never inside a Sim callback. Legacy
 * unbudgeted convenience: prefer DetTransportScheduler + pumpDetTransport.
 */
inline void sendDetLockstepPackets(DeterministicLockstep& lockstep,net::INetworkSubSystem& network,
    std::span<net::NetConnection* const> admittedPeers,std::uint8_t channel=net::CHANNEL_RELIABLE) {
    for(const auto& packet:lockstep.packets())for(auto* peer:admittedPeers)
        if(peer)network.sendTo(peer,channel,packet.data(),packet.size());
}
/// Predictive counterpart; same admission/channel/owner-thread transport contract.
inline void sendDetRollbackPackets(DeterministicRollbackNetwork& owner,net::INetworkSubSystem& network,
    std::span<net::NetConnection* const> admittedPeers,std::uint8_t channel=net::CHANNEL_RELIABLE) {
    for(const auto& packet:owner.packets())for(auto* peer:admittedPeers)
        if(peer)network.sendTo(peer,channel,packet.data(),packet.size());
}
} // namespace ayt::entity
