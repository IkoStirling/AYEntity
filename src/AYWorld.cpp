// AYWorld.cpp - World implementation

#include <AYEntity/World.h>
#include <AYEntity/EntityImpl.h>
#include <algorithm>
#include <cstdio>
#include <cstring>

namespace ayt::entity
{

namespace {

World* g_activeWorld = nullptr;

} // namespace

World::World() = default;

World::~World() {
    // If this World was still the active redirect target (e.g. Scene deleted
    // without SceneManager::setCurrent first), drop the dangling pointer.
    if (g_activeWorld == this) {
        g_activeWorld = nullptr;
    }
    shutdown();
}

World& World::processWorld() {
    // MSVC cannot emit a function-local `static World` when the default
    // ctor is private (helper is outside the class). Heap singleton is
    // equivalent for the process-wide World.
    static World* world = nullptr;
    if (world == nullptr) {
        world = new World();
    }
    return *world;
}

World& World::instance() {
    return g_activeWorld ? *g_activeWorld : processWorld();
}

void World::setActiveWorld(World* world) noexcept
{
    g_activeWorld = world;
}

World* World::activeWorld() noexcept
{
    return g_activeWorld;
}

bool World::initialize() {
    if (_initialized) return true;
    _initialized = true;
    ::printf("[World] Initialized\n");
    return true;
}

void World::shutdown() {
    if (!_initialized) return;

    // F7 — call removeAllComponents BEFORE onDetachFromWorld so each
    // entity's per-type storages can drop the (id → T*) entry. After
    // detach _world is null and SparseSet._dense would keep a
    // dangling pointer that the next query<T>() dereferences.
    for (auto& entity : _entities) {
        if (entity) {
            entity->removeAllComponents();
            entity->onDetachFromWorld();
        }
    }

    _entities.clear();
    _entityPool.clear();
    _entityNameMap.clear();
    _componentStorages.clear();
    _systems.clear();
    _nextEntityId = 1;
    _initialized = false;
    _systemsStarted = false;

    // Shared EntityHandlePool must not be wiped by Scene-owned Worlds —
    // another World (fallback or Edit) may still hold live handles (LM-1).
    if (this == &processWorld()) {
        EntityHandlePool::instance().reset();
    }

    ::printf("[World] Shutdown\n");
}

void World::update(float dt) {
    // onStart() 仅在首次更新时调用一次
    if (!_systemsStarted) {
        for (auto& system : _systems) {
            system->onStart();
        }
        _systemsStarted = true;
    }

    for (auto& system : _systems) {
        system->onUpdate(dt);
    }

    for (auto& entity : _entities) {
        if (entity && entity->isValid()) {
            entity->onUpdate(dt);
        }
    }
}

Entity* World::createEntity() {
    return createEntityInternal();
}

Entity* World::createEntityInternal() {
    auto* entity = new Entity();
    entity->onAttachToWorld(this, _nextEntityId);

    if (_nextEntityId >= _entityPool.size()) {
        _entityPool.resize(_nextEntityId + 1);
    }

    _entityPool[_nextEntityId] = EntityHandlePool::instance().allocate(_nextEntityId);
    _entities.push_back(std::unique_ptr<Entity>(entity));

    uint32_t entityId = _nextEntityId++;
    ::printf("[World] Created entity %u\n", entityId);

    return entity;
}

void World::destroyEntity(Entity* e) {
    // After shutdown(), entity storage is freed. Callers may still hold
    // raw Entity* (EditorPlayRuntime clear* during ~dtor after tests call
    // World::shutdown). Must not touch e when the world is down.
    if (!_initialized || !e) return;
    if (!e->isValid()) return;
    destroyEntityInternal(e);
}

void World::destroyEntityInternal(Entity* e) {
    uint32_t id = e->getId();

    // F7 — drop the entity's component storage entries BEFORE
    // onDetachFromWorld nulls _world. Without this the storages
    // would keep dangling T* pointers in their _dense arrays.
    e->removeAllComponents();
    e->onDetachFromWorld();

    if (id < _entityPool.size()) {
        EntityHandlePool::instance().release(_entityPool[id]);
    }

    auto it = std::find_if(_entities.begin(), _entities.end(),
        [id](const std::unique_ptr<Entity>& ptr) {
            return ptr->getId() == id;
        });

    if (it != _entities.end()) {
        _entities.erase(it);
    }

    ::printf("[World] Destroyed entity %u\n", id);
}

Entity* World::findEntity(const char* name) const {
    if (!name) return nullptr;
    auto it = _entityNameMap.find(name);
    return (it == _entityNameMap.end()) ? nullptr : findEntity(it->second);
}

Entity* World::findEntity(uint32_t id) const {
    if (id == INVALID_ID) return nullptr;
    for (auto& entity : _entities) {
        if (entity->getId() == id) {
            return entity.get();
        }
    }
    return nullptr;
}

std::vector<Entity*> World::getAllEntities() const {
    std::vector<Entity*> result;
    result.reserve(_entities.size());
    for (auto& entity : _entities) {
        if (entity && entity->isValid()) {
            result.push_back(entity.get());
        }
    }
    return result;
}

std::vector<Entity*> World::queryByNames(const std::vector<const char*>& componentNames) {
    std::vector<Entity*> result;
    for (auto& entity : _entities) {
        if (!entity || !entity->isValid()) continue;
        bool match = true;
        for (const char* name : componentNames) {
            if (!entity->hasComponentByName(name)) {
                match = false;
                break;
            }
        }
        if (match) {
            result.push_back(entity.get());
        }
    }
    return result;
}

Entity* World::getEntityByHandle(const EntityHandle& handle) {
    if (!EntityHandlePool::instance().isValid(handle)) return nullptr;
    return findEntity(handle.id);
}

EntityHandle World::getEntityHandle(uint32_t id) {
    if (id >= _entityPool.size()) return EntityHandle{};
    return _entityPool[id];
}

int32_t World::getSystemPriorityAt(size_t index) const
{
    if (index >= _systems.size()) return 0;
    return _systems[index]->getPriority();
}

const char* World::getSystemNameAt(size_t index) const
{
    if (index >= _systems.size()) return "";
    return _systems[index]->getName();
}

ISystem* World::findSystemByName(const char* name) const
{
    if (!name) return nullptr;
    for (const auto& sys : _systems) {
        if (sys && sys->getName()
            && std::strcmp(sys->getName(), name) == 0) {
            return sys.get();
        }
    }
    return nullptr;
}

} // namespace ayt::entity