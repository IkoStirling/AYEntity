#pragma once
// AYEntity.h - Entity and Query template implementations + entry point

#include <AYEntity/IEntity.h>
#include <AYEntity/SparseSet.h>
#include <AYEntity/EntityHandle.h>
#include <AYEntity/EntityImpl.h>
#include <AYEntity/World.h>
#include <AYEntity/components/TransformComponent.h>
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
        (addStorage(_world->getStorage<Components>()), ...);

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
    for (size_t h : _componentTypeHashes) {
        if (h == typeHash) return true;
    }
    return false;
}

template<typename T, typename... Args>
T* Entity::addComponent(Args&&... args) {
    World* world = getWorld();
    if (!world) return nullptr;

    size_t typeHash = typeid(T).hash_code();
    if (hasComponent<T>()) return getComponent<T>();

    T* component = new T(std::forward<Args>(args)...);
    component->onAttach(this);

    IComponentStorage* storage = world->getStorageBase<T>();
    if (!storage) {
        World::registerComponentType<T>(typeid(T).name());
        auto newStorage = SparseSetFactory::create<T>();
        size_t newHash = typeid(T).hash_code();
        world->_componentStorages[newHash] = std::move(newStorage);
        storage = world->_componentStorages[newHash].get();
    }

    storage->add(_id, component);
    _componentTypeHashes.push_back(typeHash);
    _components.push_back(component);
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
void Entity::removeComponent() {
    World* world = getWorld();
    if (!world) return;

    size_t typeHash = typeid(T).hash_code();
    IComponentStorage* storage = world->getStorageBase<T>();
    if (!storage) return;

    // _componentTypeHashes and _components are pushed in lockstep by
    // addComponent<T> (same index = same type). Reuse the hash we
    // already cached in _componentTypeHashes instead of recomputing
    // typeid(*_components[i]).hash_code() per element — both vectors
    // stay in sync, the hash lookup is O(N) either way.
    size_t matchIndex = static_cast<size_t>(-1);
    for (size_t i = 0; i < _componentTypeHashes.size(); ++i) {
        if (_componentTypeHashes[i] == typeHash) {
            matchIndex = i;
            break;
        }
    }
    if (matchIndex == static_cast<size_t>(-1)) return;

    _componentTypeHashes.erase(_componentTypeHashes.begin() + static_cast<long>(matchIndex));

    IComponent* component = _components[matchIndex];
    component->onDetach();
    delete component;
    _components.erase(_components.begin() + static_cast<long>(matchIndex));

    storage->remove(_id);
}

inline Entity* createEntity() { return World::instance().createEntity(); }
inline void destroyEntity(Entity* e) { if (e) World::instance().destroyEntity(e); }

} // namespace ayt::entity
