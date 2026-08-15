#include "AYEntity/components/NetworkComponent.h"

namespace ayt::entity
{

NetworkComponent::RegisterReplicationFn NetworkComponent::_registerFn;
NetworkComponent::UnregisterReplicationFn NetworkComponent::_unregisterFn;

void NetworkComponent::setReplicationCallbacks(RegisterReplicationFn registerFn,
                                               UnregisterReplicationFn unregisterFn)
{
    _registerFn = std::move(registerFn);
    _unregisterFn = std::move(unregisterFn);
}

void NetworkComponent::onAttach(Entity* entity)
{
    _entity = entity;
}

void NetworkComponent::onDetach()
{
    unbindReplication();
    _entity = nullptr;
}

void NetworkComponent::setReplicatedComponent(IComponent* component,
                                              const char* componentTypeName)
{
    _replicatedTarget = {component, componentTypeName};
}

bool NetworkComponent::bindReplication()
{
    if (_bound || _entity == nullptr || !isValid() || !_registerFn) {
        return false;
    }
    if (_replicatedTarget.component == nullptr || _replicatedTarget.typeName == nullptr) {
        return false;
    }
    if (!_registerFn(_entity, _netId, _replicatedTarget.component,
                     _replicatedTarget.typeName)) {
        return false;
    }
    _bound = true;
    onReplicationStart();
    return true;
}

void NetworkComponent::unbindReplication()
{
    if (!_bound) {
        return;
    }
    if (_unregisterFn && isValid()) {
        _unregisterFn(_netId);
    }
    _bound = false;
    onReplicationEnd();
}

} // namespace ayt::entity
