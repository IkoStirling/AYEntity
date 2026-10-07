#pragma once
#include <AYEntity/DeterministicLockstep.h>
#include <AYNetwork/INetwork.h>

namespace ayt::entity {
/** @brief Send the bounded lockstep retransmission set over admitted AYNetwork links.
 * @note Link AYNetwork + AYEntity::Determinism. Application owns connection/member
 * mapping, message handler and resend/stall schedule. Resolve stable membership
 * after admission; receive handler forwards authenticated ID to lockstep.receive.
 * Reserve an application channel and use the same channel on all peers. AYNetwork
 * sendTo applies its PacketCodec envelope; do not use sendEncodedTo here. Flush on
 * the owner thread after network ingress, never inside a Sim callback.
 */
inline void sendDetLockstepPackets(DeterministicLockstep& lockstep,net::INetworkSubSystem& network,
    std::span<net::NetConnection* const> admittedPeers,std::uint8_t channel=net::CHANNEL_RELIABLE) {
    for(const auto& packet:lockstep.packets())for(auto* peer:admittedPeers)
        if(peer)network.sendTo(peer,channel,packet.data(),packet.size());
}
} // namespace ayt::entity
