// AYWorld.cpp - World implementation

#include <AYWorld.h>
#include <AYEntityImpl.h>
#include <cstdio>
#include <algorithm>

namespace ayt::entity
{

World::World() = default;

World::~World() {
    shutdown();
}

World& World::instance() {
    static World world;
    return world;
}

bool World::initialize() {
    if (_initialized) return true;
    _initialized = true;
    ::printf("[World] Initialized\n");
    return true;
}

void World::shutdown() {
    if (!_initialized) return;

    for (auto& entity : _entities) {
        if (entity) {
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

    EntityHandlePool::instance().reset();

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
    if (!e || !e->isValid()) return;
    destroyEntityInternal(e);
}

void World::destroyEntityInternal(Entity* e) {
    uint32_t id = e->getId();
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