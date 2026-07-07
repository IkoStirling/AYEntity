#pragma once
// AYWorld.h - World class (non-template parts)

#include <IAYEntity.h>
#include <AYSparseSet.h>
#include <AYEntityHandle.h>
#include <unordered_map>
#include <vector>
#include <string>
#include <memory>
#include <typeindex>
#include <functional>
#include <algorithm>

namespace ayt::entity
{

// Forward declaration (Query is fully defined in AYEntity.h)
template<typename... Components>
class Query;

class World {
public:
    static World& instance();

    bool initialize();
    void shutdown();
    void update(float dt);

    Entity* createEntity();
    void destroyEntity(Entity* e);
    Entity* findEntity(const char* name) const;
    Entity* findEntity(uint32_t id) const;
    std::vector<Entity*> getAllEntities() const;

    template<typename... T>
    Query<T...> query() {
        return Query<T...>(this);
    }

    std::vector<Entity*> queryByNames(const std::vector<const char*>& componentNames);

    template<typename T>
    void registerSystem(int32_t priority = 0);

    // GL-01: introspection helpers for tick-order tests / diagnostics.
    // Returns systems sorted by priority ascending (the same order the
    // World ticks them). Lets unit tests assert that e.g. AnimationSystem
    // (priority 450) is registered before RenderSystem (priority 500)
    // without having to expose the system list directly.
    size_t systemCount() const { return _systems.size(); }
    int32_t getSystemPriorityAt(size_t index) const;
    const char* getSystemNameAt(size_t index) const;

    template<typename T>
    SparseSet<T>* getStorage();

    template<typename T>
    IComponentStorage* getStorageBase();

    template<typename T>
    static void registerComponentType(const char* name);

    Entity* getEntityByHandle(const EntityHandle& handle);
    EntityHandle getEntityHandle(uint32_t id);

private:
    World();
    ~World();
    World(const World&) = delete;
    World& operator=(const World&) = delete;

    Entity* createEntityInternal();
    void destroyEntityInternal(Entity* e);

    std::vector<EntityHandle> _entityPool;
    std::vector<std::unique_ptr<ISystem>> _systems;
    std::vector<std::unique_ptr<Entity>> _entities;
    std::unordered_map<size_t, std::unique_ptr<IComponentStorage>> _componentStorages;
    std::unordered_map<std::string, uint32_t> _entityNameMap;
    uint32_t _nextEntityId = 1;
    bool _initialized = false;
    bool _systemsStarted = false;

    static std::unordered_map<size_t, std::string>& getComponentTypeNames() {
        static std::unordered_map<size_t, std::string> s_names;
        return s_names;
    }

    friend class Entity;
};

// =============================================================================
// World template implementations
// =============================================================================
template<typename T>
void World::registerSystem(int32_t priority) {
    static_assert(std::is_base_of_v<ISystem, T>, "T must inherit ISystem");
    auto system = std::make_unique<T>();
    system->setPriority(priority);
    _systems.push_back(std::move(system));
    std::sort(_systems.begin(), _systems.end(),
        [](const std::unique_ptr<ISystem>& a, const std::unique_ptr<ISystem>& b) {
            return a->getPriority() < b->getPriority();
        });
}

template<typename T>
void World::registerComponentType(const char* name) {
    size_t typeHash = typeid(T).hash_code();
    getComponentTypeNames()[typeHash] = name;
}

template<typename T>
SparseSet<T>* World::getStorage() {
    size_t typeHash = typeid(T).hash_code();
    auto it = _componentStorages.find(typeHash);
    return (it != _componentStorages.end()) ? static_cast<SparseSet<T>*>(it->second.get()) : nullptr;
}

template<typename T>
IComponentStorage* World::getStorageBase() {
    size_t typeHash = typeid(T).hash_code();
    auto it = _componentStorages.find(typeHash);
    return (it != _componentStorages.end()) ? it->second.get() : nullptr;
}

} // namespace ayt::entity