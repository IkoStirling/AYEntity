// AYEntity.cpp - Entity non-template implementation

#include <AYEntity.h>
#include <AYComponentFactory.h>
#include <cstdio>

namespace ayt::entity
{

Entity::Entity() = default;

Entity::~Entity() {
    removeAllComponents();
}

void Entity::removeAllComponents() {
    // CRITICAL (F7): World::destroyEntityInternal and World::shutdown
    // both invoke this function — they MUST call it BEFORE
    // onDetachFromWorld(), otherwise _world is null and the per-type
    // storages would retain dangling T* in their _dense arrays. The
    // next world.query<T>() would walk into freed memory.
    //
    // Storage cleanup happens here while _world is still live so
    // each SparseSet sees the entity's id and drops the entry.
    World* world = getWorld();
    if (world) {
        for (size_t typeHash : _componentTypeHashes) {
            auto it = world->_componentStorages.find(typeHash);
            if (it != world->_componentStorages.end()) {
                it->second->remove(_id);
            }
        }
    }

    for (auto* component : _components) {
        component->onDetach();
        delete component;
    }
    _components.clear();
    _componentTypeHashes.clear();
}

void Entity::setName(const char* name) {
    // Rename: erase the old name from the map first so setName("A")
    // then setName("B") doesn't leave a stale ("A" -> id) entry that
    // would make findEntity("A") return an entity whose _name is "B".
    if (_world && !_name.empty() && _name != name) {
        _world->_entityNameMap.erase(_name);
    }
    _name = name;
    if (_world && !_name.empty()) {
        _world->_entityNameMap[_name] = _id;
    }
}

void Entity::onAttachToWorld(World* world, uint32_t id) {
    _world = world;
    _id = id;
}

void Entity::onDetachFromWorld() {
    if (!_name.empty() && _world) {
        _world->_entityNameMap.erase(_name);
    }
    _world = nullptr;
    _id = INVALID_ID;
}

void Entity::onUpdate(float dt) {
    (void)dt;
    for (auto* component : _components) {
        component->onUpdate(dt);
    }
}

void Entity::onStart() {
    for (auto* component : _components) {
        component->onStart();
    }
}

IComponent* Entity::addComponentByName(const char* typeName) {
    return ComponentFactory::addComponent(*this, typeName);
}

IComponent* Entity::getComponentByName(const char* typeName) {
    return ComponentFactory::getComponent(*this, typeName);
}

bool Entity::hasComponentByName(const char* typeName) const {
    return ComponentFactory::hasComponent(*this, typeName);
}

void Entity::removeComponentByName(const char* typeName) {
    (void)typeName;
}

std::vector<IComponent*> Entity::getComponents() const {
    return _components;
}

Entity* Entity::create() {
    return World::instance().createEntity();
}

void Entity::destroy(Entity* e) {
    if (e) {
        World::instance().destroyEntity(e);
    }
}

} // namespace ayt::entity