#pragma once
// AYEntity.h - Entity and Query template implementations + entry point

#include <AYEntity/IEntity.h>
#include <AYEntity/SparseSet.h>
#include <AYEntity/MultiSparseSet.h>
#include <AYEntity/EntityHandle.h>
#include <AYEntity/EntityImpl.h>
#include <AYEntity/World.h>
#include <AYEntity/ComponentRegistry.h>
#include <AYEntity/components/TransformComponent.h>
#include <AYEntity/components/SimTransformComponent.h>
#include <AYEntity/components/DetSimTransformComponent.h>
#include <AYEntity/components/DetSimStateComponent.h>
#include <AYEntity/DeterministicSystem.h>
#include <AYEntity/components/HealthComponent.h>
#include <AYEntity/components/MeshComponent.h>
#include <AYEntity/components/RigidBodyComponent.h>
#include <AYEntity/components/ColliderComponent.h>
#include <AYEntity/components/ScriptComponent.h>
#include <AYEntity/components/NetworkComponent.h>
#include <array>
#include <cstddef>
#include <vector>

#ifdef AY_ENTITY_PRECOMPILE_COMPONENTS
// 预编译组件已通过 CMake 定义启用
// 如需使用其他组件，请手动 #include
#endif
#include <string>
#include <memory>
#include <typeindex>

namespace ayt::entity
{

// =============================================================================
// Query - template implementation using fold expression
// =============================================================================
template<typename... Components>
class Query {
public:
    class Iterator {
    public:
        Iterator(size_t index, Query* query) : _index(index), _query(query) {
            advanceToMatch();
        }

        bool operator!=(const Iterator& other) const {
            return _index != other._index || _query != other._query;
        }

        Entity* operator*() const {
            return _query->_world->findEntity((*_query->_candidateIds)[_index]);
        }

        Iterator& operator++() {
            ++_index;
            advanceToMatch();
            return *this;
        }

    private:
        void advanceToMatch() {
            if (_query == nullptr || _query->_candidateIds == nullptr) return;
            while (_index < _query->_candidateIds->size()
                   && !_query->matches((*_query->_candidateIds)[_index])) {
                ++_index;
            }
        }

        size_t _index = 0;
        Query* _query = nullptr;
    };

    explicit Query(World* w) : _world(w) { initialize(); }
    Query() = default;

    Iterator begin() {
        return Iterator(0, this);
    }
    Iterator end() {
        return Iterator(_candidateIds != nullptr ? _candidateIds->size() : 0, this);
    }

private:
    static_assert(sizeof...(Components) > 0, "Query requires at least one component type");

    void initialize() {
        if (_world == nullptr) return;

        size_t storageIndex = 0;
        size_t smallestSize = static_cast<size_t>(-1);
        bool allStoragesPresent = true;
        auto addStorage = [&](auto* storage) {
            _storages[storageIndex++] = storage;
            if (storage == nullptr) {
                allStoragesPresent = false;
                return;
            }
            if (storage->size() < smallestSize) {
                smallestSize = storage->size();
                _candidateIds = &storage->getEntityIds();
            }
        };
        (addStorage(_world->getStorageBase<Components>()), ...);

        if (!allStoragesPresent) {
            _candidateIds = nullptr;
        }
    }

    bool matches(uint32_t entityId) const {
        if (_world->findEntity(entityId) == nullptr) return false;
        for (const IComponentStorage* storage : _storages) {
            if (storage == nullptr || !storage->has(entityId)) return false;
        }
        return true;
    }

    World* _world = nullptr;
    std::array<IComponentStorage*, sizeof...(Components)> _storages{};
    const std::vector<uint32_t>* _candidateIds = nullptr;
};

// =============================================================================
// Entity template implementations
// =============================================================================
template<typename T>
bool Entity::hasComponent() const {
    size_t typeHash = typeid(T).hash_code();
    for (const auto& instance : _componentInstances) {
        if (instance.typeHash == typeHash) return true;
    }
    return false;
}

template<typename T, typename... Args>
T* Entity::addComponent(Args&&... args) {
    const auto* descriptor = ComponentRegistry::instance().find<T>();
    if (!descriptor || descriptor->multiplicity != ComponentMultiplicity::Single)
        return nullptr;
    if (hasComponent<T>()) return getComponent<T>();
    return createComponent<T>(std::forward<Args>(args)...);
}

template<typename T, typename... Args>
T* Entity::createComponent(Args&&... args) {
    World* world = getWorld();
    const auto* descriptor = ComponentRegistry::instance().find<T>();
    if (!world || !descriptor) return nullptr;
    if (descriptor->multiplicity == ComponentMultiplicity::Single
        && hasComponent<T>()) return nullptr;

    size_t typeHash = typeid(T).hash_code();
    IComponentStorage* storage = world->getStorageBase<T>();
    if (!storage) {
        std::unique_ptr<IComponentStorage> newStorage;
        if (descriptor->multiplicity == ComponentMultiplicity::Multiple)
            newStorage = std::make_unique<MultiSparseSet<T>>();
        else
            newStorage = SparseSetFactory::create<T>();
        world->_componentStorages[typeHash] = std::move(newStorage);
        storage = world->_componentStorages[typeHash].get();
    }

    T* component = new T(std::forward<Args>(args)...);
    storage->add(_id, component);
    _componentInstances.push_back({component, typeHash, makeComponentInstanceId(), {}});
    component->onAttach(this);
    return component;
}

template<typename T>
T* Entity::getComponent() {
    World* world = getWorld();
    if (!world) return nullptr;
    IComponentStorage* storage = world->getStorageBase<T>();
    if (!storage) return nullptr;
    return static_cast<T*>(storage->get(_id));
}

template<typename T>
std::vector<T*> Entity::getComponents() const {
    std::vector<T*> result;
    const size_t typeHash = typeid(T).hash_code();
    for (const auto& instance : _componentInstances) {
        if (instance.typeHash == typeHash)
            result.push_back(static_cast<T*>(instance.component));
    }
    return result;
}

template<typename T>
void Entity::removeComponent() {
    World* world = getWorld();
    if (!world) return;
    const auto* descriptor = ComponentRegistry::instance().find<T>();
    if (!descriptor || descriptor->multiplicity != ComponentMultiplicity::Single)
        return;

    size_t typeHash = typeid(T).hash_code();
    IComponentStorage* storage = world->getStorageBase<T>();
    if (!storage) return;

    size_t matchIndex = static_cast<size_t>(-1);
    for (size_t i = 0; i < _componentInstances.size(); ++i) {
        if (_componentInstances[i].typeHash == typeHash) {
            matchIndex = i;
            break;
        }
    }
    if (matchIndex == static_cast<size_t>(-1)) return;

    IComponent* component = _componentInstances[matchIndex].component;
    component->onDetach();
    delete component;
    _componentInstances.erase(_componentInstances.begin() + static_cast<long>(matchIndex));

    storage->removeInstance(_id, component);
}

inline Entity* createEntity() { return World::instance().createEntity(); }
inline void destroyEntity(Entity* e) { if (e) World::instance().destroyEntity(e); }

} // namespace ayt::entity
