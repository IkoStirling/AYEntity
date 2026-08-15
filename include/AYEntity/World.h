#pragma once
// AYEntity/World.h - World class (non-template parts)

#include <AYEntity/IEntity.h>
#include <AYEntity/SparseSet.h>
#include <AYEntity/EntityHandle.h>
#include <unordered_map>
#include <vector>
#include <string>
#include <memory>
#include <typeindex>
#include <functional>
#include <algorithm>

namespace ayt::scene
{
// AYScene PR-1: SceneAccessor is a thin friend accessor declared in
// AYScene. Forward-declared here so World can grant it friendship
// without dragging in AYScene.h (which transitively pulls in AYWorld).
struct SceneAccessor;
} // namespace ayt::scene

namespace ayt::entity
{

// Forward declaration (Query is fully defined in AYEntity.h)
template<typename... Components>
class Query;

class World {
public:
    /// Process fallback World (Meyers). When an active Scene World is set via
    /// `setActiveWorld`, `instance()` redirects there so EntitySubSystem /
    /// `bootstrapModule` / `Entity::create` all target the same sim authority.
    static World& instance();

    /// Always the process Meyers World (never redirected). Used by
    /// EntitySubSystem initialize/shutdown so Scene-owned Worlds stay RAII.
    static World& processWorld();

    /// Redirect `instance()` to `world`, or clear redirect with nullptr
    /// (falls back to `processWorld()`). SceneManager::setCurrent owns this.
    static void setActiveWorld(World* world) noexcept;
    static World* activeWorld() noexcept;

    bool initialize();
    void shutdown();
    bool isInitialized() const { return _initialized; }
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

    // LG-04 (S3.1): lookup a system by its getName() string. Returns
    // nullptr if no system with that name is registered. Linear scan
    // over _systems — system counts are small (tens), not a hot path.
    // Used by AYScript's System host tick path to map a Logia script
    // name to a registered C++ ISystem instance.
    ISystem* findSystemByName(const char* name) const;

    template<typename T>
    SparseSet<T>* getStorage();

    template<typename T>
    IComponentStorage* getStorageBase();

    template<typename T>
    static void registerComponentType(const char* name);

    template<typename T>
    static bool isComponentTypeRegistered() {
        const size_t typeHash = typeid(T).hash_code();
        return getComponentTypeNames().find(typeHash) != getComponentTypeNames().end();
    }

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
    // AYScene PR-1: Scene::Impl owns independent World instances to avoid
    // sharing the singleton across Edit/Play scenes (LM-1).
    // Friend declaration so Scene can access the private ctor/dtor;
    // World lifecycle ownership remains inside Scene::Impl (RAII via pimpl).
    // The forward declaration of ayt::scene::SceneAccessor lives at file
    // scope above (not inside this class body — that was a compile error).
    friend struct ayt::scene::SceneAccessor;
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