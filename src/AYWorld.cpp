// AYWorld.cpp - World implementation

#include <AYEntity/World.h>
#include <AYEntity/EntityImpl.h>
#include <AYEntity/WorldLifecycle.h>
#include <AYEntity/EntitySimulationDriver.h>
#include <AYEntity/components/SimTransformComponent.h>
#include <AYEntity/components/DetSimTransformComponent.h>
#include <AYEntity/DeterministicSystem.h>
#include <stdexcept>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <utility>

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
    // The optional Host binding seals healthy recordings and releases its
    // session while registered state and ownership are still valid.
    if (auto* observer = std::exchange(_hostedSimulationObserver, nullptr))
        observer->worldShutdown(*this);
    _deterministicOwner = nullptr;

    // Integrations release World-owned state before systems disappear. The
    // callback registry belongs to AYEntityCore and therefore keeps World
    // independent of renderer, physics, scripting, and networking libraries.
    notifyWorldBeforeShutdown(*this);

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

    // Shared EntityHandlePool must not be wiped by Scene-owned Worlds —
    // another World (fallback or Edit) may still hold live handles (LM-1).
    if (this == &processWorld()) {
        EntityHandlePool::instance().reset();
    }

    ::printf("[World] Shutdown\n");
}

void World::update(float dt) {
    updatePresentation(dt, 1.0f);
}

void World::beginSimulationStep() {
    // Snapshot once per fixed tick, before any Sim system writes. This makes
    // interpolation independent of how many Sim systems touch the component.
    if (auto* storage = getStorage<SimTransformComponent>()) {
        for (SimTransformComponent* transform : storage->getDense()) {
            if (transform != nullptr) transform->beginSimulationStep();
        }
    }

    if (auto* storage = getStorage<DetSimTransformComponent>()) {
        for (auto* transform : storage->getDense()) {
            if (transform != nullptr) transform->beginSimulationStep();
        }
    }
}

void World::fixedUpdate(float fixedDt) {
    if (_deterministicOwner) throw std::logic_error("Session owns World Sim execution");
    beginSimulationStep();
    updateLane(SystemLane::Sim, fixedDt);
}

void World::fixedUpdate(math::DetFloat32 fixedDt) {
    if (_deterministicOwner) throw std::logic_error("Session owns World Sim execution");
    if (!fixedDt.isFinite() || !(fixedDt > math::DetFloat32{})) {
        throw std::invalid_argument("Deterministic World dt must be finite and positive");
    }
    beginSimulationStep();
    for (auto& system : _systems) {
        if (system->getLane() != SystemLane::Sim) continue;
        system->startOnce();
        if (auto* deterministic = dynamic_cast<IDeterministicSystem*>(system.get())) {
            deterministic->onDeterministicUpdate(fixedDt);
        } else {
            system->onUpdate(fixedDt.toFloat());
        }
    }
}

void World::updatePresentation(float dt, float interpolationAlpha) {
    if (interpolationAlpha < 0.0f) interpolationAlpha = 0.0f;
    if (interpolationAlpha > 1.0f) interpolationAlpha = 1.0f;

    // Bridge must publish float transforms before presentation systems submit
    // render work for this frame.
    updateLane(SystemLane::Bridge, interpolationAlpha);
    updateLane(SystemLane::Present, dt);

    for (auto& entity : _entities) {
        if (entity && entity->isValid()) {
            entity->onUpdate(dt);
        }
    }
}

void World::updateLane(SystemLane lane, float timeValue) {
    for (auto& system : _systems) {
        if (system->getLane() != lane) continue;
        system->startOnce();
        system->onUpdate(timeValue);
    }
}

Entity* World::createEntity() {
    if (_deterministicOwner) throw std::logic_error("Use session structural commands");
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
    if (_deterministicOwner) throw std::logic_error("Use session structural commands");
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
    const size_t entityIndex = static_cast<size_t>(id - 1u);
    if (entityIndex < _entities.size()
        && _entities[entityIndex].get() == e) {
        _entities[entityIndex].reset();
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
    const size_t entityIndex = static_cast<size_t>(id - 1u);
    return entityIndex < _entities.size() ? _entities[entityIndex].get() : nullptr;
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

SystemLane World::getSystemLaneAt(size_t index) const
{
    if (index >= _systems.size()) return SystemLane::Present;
    return _systems[index]->getLane();
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
