// AYEntity.cpp - Entity non-template implementation

#include <AYEntity.h>
#include <cstdio>

namespace ayt::entity
{

Entity::Entity() = default;

Entity::~Entity() {
    removeAllComponents();
}

void Entity::removeAllComponents() {
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
    (void)typeName;
    return nullptr;
}

IComponent* Entity::getComponentByName(const char* typeName) {
    (void)typeName;
    return nullptr;
}

bool Entity::hasComponentByName(const char* typeName) const {
    (void)typeName;
    return false;
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