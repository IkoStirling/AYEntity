#pragma once
// AYEntity.h - Entity and Query template implementations + entry point

#include <IAYEntity.h>
#include <AYSparseSet.h>
#include <AYEntityHandle.h>
#include <AYEntityImpl.h>
#include <AYWorld.h>
#include <components/AYTransformComponent.h>
#include <components/AYHealthComponent.h>
#include <components/AYMeshComponent.h>
#include <components/AYRigidBodyComponent.h>
#include <components/AYScriptComponent.h>
#include <components/AYNetworkComponent.h>
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
        Iterator(uint32_t id, World* w) : _id(id), _world(w) {}

        bool operator!=(const Iterator& other) const {
            return _id != other._id;
        }

        Entity* operator*() const {
            return _world->findEntity(_id);
        }

        Iterator& operator++() {
            do {
                _id++;
            } while (_id < MAX_ENTITIES && !hasAllComponents());
            return *this;
        }

    private:
        bool hasAllComponents() const {
            auto* e = _world->findEntity(_id);
            return e && (e->hasComponent<Components>() && ...);
        }

        uint32_t _id;
        World* _world;
    };

    Query(World* w) : _world(w), _first(findFirst()) {}
    Query() : _world(nullptr), _first(MAX_ENTITIES) {}

    Iterator begin() {
        if (!_world) return Iterator(MAX_ENTITIES, _world);
        return Iterator(_first, _world);
    }
    Iterator end() { return Iterator(MAX_ENTITIES, _world); }

private:
    uint32_t findFirst() const {
        if (!_world) return MAX_ENTITIES;
        for (uint32_t id = 1; id < MAX_ENTITIES; id++) {
            auto* e = _world->findEntity(id);
            if (e && (e->hasComponent<Components>() && ...)) {
                return id;
            }
        }
        return MAX_ENTITIES;
    }

    World* _world;
    uint32_t _first;
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

    for (size_t i = 0; i < _componentTypeHashes.size(); ++i) {
        if (_componentTypeHashes[i] == typeHash) {
            _componentTypeHashes.erase(_componentTypeHashes.begin() + static_cast<long>(i));
            break;
        }
    }

    for (size_t i = 0; i < _components.size(); ++i) {
        if (typeid(*_components[i]).hash_code() == typeHash) {
            _components[i]->onDetach();
            delete _components[i];
            _components.erase(_components.begin() + static_cast<long>(i));
            break;
        }
    }

    storage->remove(_id);
}

inline Entity* createEntity() { return World::instance().createEntity(); }
inline void destroyEntity(Entity* e) { if (e) World::instance().destroyEntity(e); }

} // namespace ayt::entity