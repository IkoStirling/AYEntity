// AYEntity.cpp - Entity non-template implementation

#include <AYEntity.h>
#include <AYEntity/ComponentFactory.h>
#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdio>
#include <iomanip>
#include <random>
#include <sstream>

namespace ayt::entity
{

std::string makeComponentInstanceId() {
    static std::atomic<uint64_t> serial{1};
    std::random_device random;
    std::ostringstream out;
    out << std::hex << std::setfill('0') << std::setw(8) << random()
        << std::setw(8) << random() << std::setw(16) << serial.fetch_add(1);
    return out.str();
}

std::string legacyComponentInstanceId(const std::string& registeredTypeName) {
    // The same legacy slot keeps the same Entity-local identity on every load.
    constexpr uint64_t basis = 14695981039346656037ull;
    constexpr uint64_t prime = 1099511628211ull;
    uint64_t left = basis;
    uint64_t right = basis ^ 0x9e3779b97f4a7c15ull;
    for (unsigned char c : registeredTypeName) {
        left = (left ^ c) * prime;
        right = (right ^ c) * prime;
    }
    std::ostringstream out;
    out << std::hex << std::setfill('0') << std::setw(16) << left
        << std::setw(16) << right;
    return out.str();
}

bool isValidComponentInstanceId(const std::string& id) noexcept {
    return id.size() == 32 && std::all_of(id.begin(), id.end(), [](unsigned char c) {
        return std::isdigit(c) != 0 || (c >= 'a' && c <= 'f');
    });
}

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
        for (const auto& instance : _componentInstances) {
            const size_t typeHash = instance.typeHash;
            auto it = world->_componentStorages.find(typeHash);
            if (it != world->_componentStorages.end()) {
                it->second->remove(_id);
            }
        }
    }

    for (const auto& instance : _componentInstances) {
        instance.component->onDetach();
        delete instance.component;
    }
    _componentInstances.clear();
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
    for (const auto& instance : _componentInstances) {
        instance.component->onUpdate(dt);
    }
}

void Entity::onStart() {
    for (const auto& instance : _componentInstances) {
        instance.component->onStart();
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
    (void)ComponentFactory::removeComponent(*this, typeName);
}

IComponent* Entity::findComponentById(const std::string& id) const noexcept {
    const auto* instance = findComponentInstance(id);
    return instance ? instance->component : nullptr;
}

bool Entity::removeComponentById(const std::string& id) {
    if (!_world) return false;
    for (size_t i = 0; i < _componentInstances.size(); ++i) {
        if (_componentInstances[i].id != id) continue;
        const auto instance = _componentInstances[i];
        const auto storage = _world->_componentStorages.find(instance.typeHash);
        if (storage == _world->_componentStorages.end()
            || !storage->second->removeInstance(_id, instance.component)) return false;
        _componentInstances.erase(_componentInstances.begin() + static_cast<long>(i));
        instance.component->onDetach();
        delete instance.component;
        return true;
    }
    return false;
}

std::vector<IComponent*> Entity::getComponents() const {
    std::vector<IComponent*> result;
    result.reserve(_componentInstances.size());
    for (const auto& instance : _componentInstances) result.push_back(instance.component);
    return result;
}

const Entity::ComponentInstance* Entity::componentInstance(
    const IComponent* component) const noexcept {
    for (const auto& instance : _componentInstances) {
        if (instance.component == component) return &instance;
    }
    return nullptr;
}

const Entity::ComponentInstance* Entity::findComponentInstance(
    const std::string& id) const noexcept {
    for (const auto& instance : _componentInstances) {
        if (instance.id == id) return &instance;
    }
    return nullptr;
}

bool Entity::setComponentInstanceId(const IComponent* component, const std::string& id) {
    if (!isValidComponentInstanceId(id)) return false;
    for (auto& instance : _componentInstances) {
        if (instance.id == id && instance.component != component) return false;
        if (instance.component == component) {
            if (instance.restoredIdentity) return instance.id == id;
            instance.id = id;
            instance.restoredIdentity = true;
            return true;
        }
    }
    return false;
}

bool Entity::setComponentDisplayName(const IComponent* component, std::string name) {
    for (auto& instance : _componentInstances) {
        if (instance.component == component) {
            instance.displayName = std::move(name);
            return true;
        }
    }
    return false;
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
