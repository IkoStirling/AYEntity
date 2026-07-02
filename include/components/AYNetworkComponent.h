#pragma once
// AYNetworkComponent.h - 网络复制组件

#include <IAYEntity.h>
#include <cstdint>

namespace ayt::entity
{

// =============================================================================
// 网络复制组件前置声明
// =============================================================================
// IReplicable 定义在 AYNetwork 中，当前为空接口用于编译
class IReplicable;

// =============================================================================
// NetworkComponent - 网络复制组件
// =============================================================================
// 注意：完整实现需要 AYNetwork 模块支持
// 当前为接口定义，AYNetwork 实现后可正常使用
class NetworkComponent : public IComponent {
public:
    const char* getName() const override { return "NetworkComponent"; }

    // ===== 网络ID =====
    uint32_t getNetId() const { return _netId; }
    void setNetId(uint32_t id) { _netId = id; }

    // ===== 复制优先级 =====
    float getReplicationPriority() const { return _replicationPriority; }
    void setReplicationPriority(float priority) { _replicationPriority = priority; }

    // ===== 复制通道 =====
    uint8_t getReplicationChannel() const { return _channel; }
    void setReplicationChannel(uint8_t channel) { _channel = channel; }

    // ===== 同步控制 =====
    bool isOwner() const { return _isOwner; }
    void setOwner(bool isOwner) { _isOwner = isOwner; }

    bool isStreamed() const { return _isStreamed; }
    void setStreamed(bool streamed) { _isStreamed = streamed; }

    // ===== 状态 =====
    bool isValid() const { return _netId != INVALID_NET_ID; }

    // IReplicable 方法（需要 AYNetwork 实现）
    // 这些方法在 AYNetwork 实现前仅作占位符
    virtual uint32_t getNetworkId() const { return _netId; }
    virtual void onReplicationStart() {}
    virtual void onReplicationEnd() {}

private:
    static constexpr uint32_t INVALID_NET_ID = 0xFFFFFFFF;

    uint32_t _netId = INVALID_NET_ID;
    float _replicationPriority = 1.0f;
    uint8_t _channel = 0;  // 0 = CHANNEL_RELIABLE
    bool _isOwner = false;
    bool _isStreamed = true;
};

AY_COMPONENT(NetworkComponent);

} // namespace ayt::entity