#pragma once
// AYNetworkComponent.h - network replication component

#include <IAYEntity.h>

#include <cstdint>
#include <functional>
#include <vector>

namespace ayt::entity
{

class IReplicable;

// =============================================================================
// NetworkComponent - marks an entity for replication via INetworkSubSystem
// =============================================================================
// AYEntity does not link AYNetwork. The host (Editor Play, game exe) installs
// register/unregister callbacks that forward to ReplicationManager /
// EntityReplicationAdapter. bindReplication() consumes those callbacks.
class NetworkComponent : public IComponent {
public:
    using RegisterReplicationFn =
        std::function<bool(Entity* entity, uint32_t netId, IComponent* dataComponent,
                           const char* componentTypeName)>;
    using UnregisterReplicationFn = std::function<void(uint32_t netId)>;

    static void setReplicationCallbacks(RegisterReplicationFn registerFn,
                                        UnregisterReplicationFn unregisterFn);

    const char* getName() const override { return "NetworkComponent"; }

    void onAttach(Entity* entity) override;
    void onDetach() override;

    // ===== Network ID =====
    uint32_t getNetId() const { return _netId; }
    void setNetId(uint32_t id) { _netId = id; }

    // ===== Replication tuning =====
    float getReplicationPriority() const { return _replicationPriority; }
    void setReplicationPriority(float priority) { _replicationPriority = priority; }

    uint8_t getReplicationChannel() const { return _channel; }
    void setReplicationChannel(uint8_t channel) { _channel = channel; }

    bool isOwner() const { return _isOwner; }
    void setOwner(bool isOwner) { _isOwner = isOwner; }

    bool isStreamed() const { return _isStreamed; }
    void setStreamed(bool streamed) { _isStreamed = streamed; }

    bool isValid() const { return _netId != INVALID_NET_ID; }
    bool isReplicationBound() const { return _bound; }

    // Data component replicated under this entity's netId (one slot for R4.1).
    void setReplicatedComponent(IComponent* component, const char* componentTypeName);

    // Consume host callbacks installed via setReplicationCallbacks().
    bool bindReplication();
    void unbindReplication();

    virtual uint32_t getNetworkId() const { return _netId; }
    virtual void onReplicationStart() {}
    virtual void onReplicationEnd() {}

private:
    static constexpr uint32_t INVALID_NET_ID = 0xFFFFFFFF;

    struct ReplicatedTarget {
        IComponent* component = nullptr;
        const char* typeName = nullptr;
    };

    static RegisterReplicationFn _registerFn;
    static UnregisterReplicationFn _unregisterFn;

    Entity* _entity = nullptr;
    ReplicatedTarget _replicatedTarget{};
    uint32_t _netId = INVALID_NET_ID;
    float _replicationPriority = 1.0f;
    uint8_t _channel = 0;
    bool _isOwner = false;
    bool _isStreamed = true;
    bool _bound = false;
};

} // namespace ayt::entity
