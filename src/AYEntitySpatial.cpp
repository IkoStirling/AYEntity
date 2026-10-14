#include <AYEntity.h>
#include <AYMath/MathTransform.h>
#include <cmath>

namespace ayt::entity {
namespace {
bool fail(std::string* error, const char* message) {
    if (error) *error = message;
    return false;
}
bool finite(const math::Float4x4& m) {
    for (int i = 0; i < 16; ++i) if (!std::isfinite(m.ptr()[i])) return false;
    return true;
}
bool localTrs(const math::Float4x4& matrix, Transform& out) {
    if (!finite(matrix) || !matrix.decompose(out.position, out.rotation, out.scale)) return false;
    const auto rebuilt = math::Transform::getMatrix(out.position, out.rotation, out.scale);
    for (int i = 0; i < 16; ++i)
        if (std::fabs(rebuilt.ptr()[i] - matrix.ptr()[i]) > 1e-4f * std::max(1.f, std::fabs(matrix.ptr()[i]))) return false;
    return true;
}
bool depthFits(const Entity& root, const Entity* parent, bool ownership) {
    size_t ancestors = 0;
    for (auto* cursor = parent; cursor; cursor = ownership ? cursor->owner() : cursor->spatialParent())
        if (++ancestors > 256) return false;
    std::vector<std::pair<const Entity*,size_t>> pending{{&root,ancestors}};
    while (!pending.empty()) {
        const auto [entity,depth] = pending.back(); pending.pop_back();
        if (depth > 256) return false;
        const auto children = ownership ? entity->ownedEntities() : entity->spatialChildren();
        for (auto* child : children) pending.emplace_back(child,depth+1);
    }
    return true;
}
bool rootDriven(const Entity& entity) {
    if (entity.hasComponent<SimTransformComponent>() || entity.hasComponent<DetSimTransformComponent>()) return true;
    auto* rigid = const_cast<Entity&>(entity).getComponent<RigidBodyComponent>();
    return rigid && !rigid->isKinematic() && !rigid->isStatic();
}
}

Entity* World::createEntity(EntityKind kind) {
    auto* entity = createEntity();
    if (!entity) return nullptr;
    if (!entity->convertKind(kind)) { destroyEntity(entity); return nullptr; }
    entity->_legacySpatialPromotion = false;
    return entity;
}
bool World::canDestroyEntity(const Entity* entity, std::string* error) const {
    if (!entity || entity->getWorld() != this) return fail(error,"Entity does not belong to this World");
    std::vector<const Entity*> removal;
    const auto collect = [&](const auto& self, const Entity* item) -> void {
        removal.push_back(item); for (auto* child : item->ownedEntities()) self(self,child);
    };
    collect(collect,entity);
    for (const auto* item : removal) for (auto* child : item->spatialChildren()) {
        if (std::find(removal.begin(),removal.end(),child) != removal.end()) continue;
        Transform checked;
        if (!localTrs(child->worldMatrix(),checked)) return fail(error,"Detach child explicitly: deleting its parent cannot preserve a singular/sheared world pose as root TRS");
    }
    return true;
}
void World::clearEntities() {
    if (_deterministicOwner) throw std::logic_error("Cannot clear a session-owned World");
    for (auto& entity : _entities) if (entity) {
        const auto id = entity->getId();
        entity->removeAllComponents(); entity->onDetachFromWorld();
        if (id < _entityPool.size()) EntityHandlePool::instance().release(_entityPool[id]);
    }
    _entities.clear(); _entityNameMap.clear();
    // Preserve monotonically issued runtime IDs and vector indexing after a scene reload.
    _entities.resize(_nextEntityId - 1);
}
Entity* World::findEntityByPersistentId(const std::string& id) const {
    for (const auto& entity : _entities)
        if (entity && entity->persistentId() == id) return entity.get();
    return nullptr;
}
bool Entity::restorePersistentId(const std::string& id) {
    if (!_world || !isValidComponentInstanceId(id)) return false;
    if (_identityRestored) return _persistentId == id;
    auto* existing = _world->findEntityByPersistentId(id);
    if (existing && existing != this) return false;
    _persistentId = id; _identityRestored = true; return true;
}
Transform* Entity::acquireLegacyTransform() {
    if (_transform) return _transform.get();
    if (!_legacySpatialPromotion || !_world) return nullptr;
    return convertKind(EntityKind::Spatial) ? _transform.get() : nullptr;
}
bool Entity::prepareSpatialComponent(bool required, std::string* error) {
    if (!required || isSpatial()) return true;
    if (_legacySpatialPromotion) return convertKind(EntityKind::Spatial, error);
    return fail(error, "Data entities cannot have spatial components; explicitly convert to Spatial first");
}
bool Entity::convertKind(EntityKind kind, std::string* error) {
    if (!_world) return fail(error, "Entity is not in a World");
    if (kind != EntityKind::Data && kind != EntityKind::Spatial) return fail(error, "Unknown Entity kind");
    if (_world->_deterministicOwner && !_legacySpatialPromotion)
        return fail(error, "Entity kind is immutable while a deterministic session owns the World");
    if (kind == _kind) return true;
    if (kind == EntityKind::Data) {
        if (spatialParent() || !spatialChildren().empty()) return fail(error, "Detach spatial relations before converting to Data");
        for (const auto& instance : _componentInstances) {
            if (instance.component == _transform.get()) continue;
            const auto* descriptor = ComponentRegistry::instance().find(*instance.component);
            if (descriptor && descriptor->requiresSpatial) return fail(error, "Remove spatial components before converting to Data");
        }
        auto* pointer = _transform.get();
        _world->_componentStorages[typeid(Transform).hash_code()]->remove(_id);
        _componentInstances.erase(std::remove_if(_componentInstances.begin(), _componentInstances.end(),
            [pointer](const auto& instance) { return instance.component == pointer; }), _componentInstances.end());
        pointer->onDetach(); _transform.reset();
    } else {
        _transform = std::make_unique<Transform>();
        auto& storage = _world->_componentStorages[typeid(Transform).hash_code()];
        if (!storage) storage = std::make_unique<SparseSet<Transform>>();
        storage->add(_id, _transform.get());
        // A reserved compatibility reflection slot, not an optional component.
        _componentInstances.push_back({_transform.get(), typeid(Transform).hash_code(), makeComponentInstanceId(), "Spatial Transform"});
        _transform->onAttach(this);
    }
    _kind = kind; _legacySpatialPromotion = false; return true;
}
Entity* Entity::spatialParent() const { return _world ? _world->getEntityByHandle(_spatialParent) : nullptr; }
Entity* Entity::owner() const { return _world ? _world->getEntityByHandle(_owner) : nullptr; }
std::vector<Entity*> Entity::spatialChildren() const {
    std::vector<Entity*> result;
    if (_world) for (auto* entity : _world->getAllEntities()) if (entity->spatialParent() == this) result.push_back(entity);
    return result;
}
std::vector<Entity*> Entity::ownedEntities() const {
    std::vector<Entity*> result;
    if (_world) for (auto* entity : _world->getAllEntities()) if (entity->owner() == this) result.push_back(entity);
    return result;
}
bool Entity::setOwner(Entity* owner, std::string* error) {
    if (!_world || _world->_deterministicOwner) return fail(error, "Ownership cannot be edited in this World state");
    if (owner && owner->getWorld() != _world) return fail(error, "Owner must belong to the same World");
    for (auto* cursor = owner; cursor; cursor = cursor->owner())
        if (cursor == this) return fail(error, "Ownership cycle is not allowed");
    if (!depthFits(*this,owner,true)) return fail(error,"Ownership hierarchy depth exceeds 256");
    _owner = owner ? _world->getEntityHandle(owner->getId()) : EntityHandle{};
    return true;
}
math::Float4x4 Entity::worldMatrix(float alpha) const {
    std::vector<const Entity*> chain;
    for (auto* cursor = this; cursor; cursor = cursor->spatialParent()) chain.push_back(cursor);
    auto result = math::Float4x4::identity();
    for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
        const auto* pose = (*it)->transform();
        if (pose) result = result * math::Transform::getMatrix(pose->interpolatedPosition(alpha),
            pose->interpolatedRotation(alpha), pose->scale);
    }
    return result;
}
bool Entity::worldPose(math::FVector3& position, math::FQuaternion& rotation, float alpha) const {
    if (!isSpatial()) return false;
    const auto matrix = worldMatrix(alpha);
    if (!finite(matrix)) return false;
    position = {matrix(0,3), matrix(1,3), matrix(2,3)};
    std::vector<const Entity*> chain;
    for (auto* cursor = this; cursor; cursor = cursor->spatialParent()) chain.push_back(cursor);
    rotation = {0,0,0,1};
    for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
        auto q = (*it)->transform()->interpolatedRotation(alpha);
        if (!std::isfinite(q.dot(q)) || q.dot(q) < 1e-12f) return false;
        rotation = rotation * q.normalize();
    }
    rotation = rotation.normalize(); return true;
}
bool Entity::setWorldPose(const math::FVector3& position, const math::FQuaternion& rotation,
                          float alpha, std::string* error) {
    if (!isSpatial() || !std::isfinite(position.x) || !std::isfinite(position.y)
        || !std::isfinite(position.z) || !std::isfinite(rotation.dot(rotation))
        || rotation.dot(rotation) < 1e-12f) return fail(error,"Invalid Spatial presentation pose");
    auto localPosition = position; auto localRotation = rotation.normalize();
    if (auto* parent = spatialParent()) {
        Transform checked; const auto matrix = parent->worldMatrix(alpha);
        math::FVector3 parentPosition; math::FQuaternion parentRotation;
        if (!localTrs(matrix,checked) || !parent->worldPose(parentPosition,parentRotation,alpha))
            return fail(error,"Presentation parent pose is singular or sheared");
        const auto local = matrix.inverse() * math::FVector4{position.x,position.y,position.z,1};
        localPosition = {local.x,local.y,local.z};
        localRotation = (parentRotation.conjugate() * localRotation).normalize();
        if (!std::isfinite(localPosition.x) || !std::isfinite(localPosition.y) || !std::isfinite(localPosition.z))
            return fail(error,"Invalid world-to-local presentation position");
    }
    _transform->setPresentationPose(localPosition,localRotation); return true;
}
bool Entity::setWorldMatrix(const math::Float4x4& matrix, std::string* error) {
    if (!isSpatial()) return fail(error, "Data entities have no spatial pose");
    auto local = matrix;
    if (auto* parent = spatialParent()) {
        Transform checked;
        auto parentMatrix = parent->worldMatrix();
        if (!localTrs(parentMatrix, checked)) return fail(error, "Parent world transform is singular or sheared");
        local = parentMatrix.inverse() * matrix;
    }
    Transform checked;
    if (!localTrs(local, checked)) return fail(error, "Requested local transform is singular or cannot be represented as TRS");
    _transform->position = checked.position; _transform->rotation = checked.rotation; _transform->scale = checked.scale;
    _transform->hasPreviousSimulationPose = false; ++_transform->revision;
    return true;
}
bool Entity::setSpatialParent(Entity* parent, ReparentMode mode, std::string* error) {
    if (!_world || _world->_deterministicOwner) return fail(error, "Spatial hierarchy cannot be edited in this World state");
    if (!isSpatial() || (parent && !parent->isSpatial())) return fail(error, "Spatial attachment requires Spatial entities");
    if (parent && parent->getWorld() != _world) return fail(error, "Spatial parent must belong to the same World");
    if (parent && rootDriven(*this)) return fail(error, "Dynamic physics and authoritative Sim entities must remain spatial roots");
    size_t depth = 0;
    for (auto* cursor = parent; cursor; cursor = cursor->spatialParent()) {
        if (cursor == this) return fail(error, "Spatial cycle is not allowed");
        if (++depth > 256) return fail(error, "Spatial hierarchy depth exceeds 256");
    }
    if (mode != ReparentMode::KeepLocal && mode != ReparentMode::KeepWorld) return fail(error, "Unknown reparent mode");
    if (parent == spatialParent()) return true;
    if (!depthFits(*this,parent,false)) return fail(error,"Spatial hierarchy depth exceeds 256");
    if (parent && hasComponent<RigidBodyComponent>()) {
        Transform pose;
        if (!localTrs(parent->worldMatrix(),pose) || std::fabs(pose.scale.x-1)>1e-5f
            || std::fabs(pose.scale.y-1)>1e-5f || std::fabs(pose.scale.z-1)>1e-5f)
            return fail(error,"Physics attachments require an unscaled rigid parent pose");
    }
    Transform checked;
    if (mode == ReparentMode::KeepWorld) {
        auto local = worldMatrix();
        if (parent) {
            Transform parentCheck;
            const auto parentWorld = parent->worldMatrix();
            if (!localTrs(parentWorld, parentCheck)) return fail(error, "Parent world transform is singular or sheared");
            local = parentWorld.inverse() * local;
        }
        if (!localTrs(local, checked)) return fail(error, "Reparent would require singular or sheared local TRS");
    }
    _spatialParent = parent ? _world->getEntityHandle(parent->getId()) : EntityHandle{};
    if (mode == ReparentMode::KeepWorld) {
        _transform->position = checked.position; _transform->rotation = checked.rotation; _transform->scale = checked.scale;
    }
    _transform->hasPreviousSimulationPose = false; ++_transform->revision;
    return true;
}
} // namespace ayt::entity
