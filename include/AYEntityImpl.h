#pragma once
// AYEntityImpl.h - Entity class declaration (non-template)

#include <IAYEntity.h>
#include <AYSparseSet.h>
#include <AYEntityHandle.h>
#include <vector>
#include <string>

namespace ayt::entity
{

class World;

constexpr uint32_t MAX_ENTITIES = 10000;

// =============================================================================
// Query - declaration
// =============================================================================
template<typename... Components>
class Query;

// =============================================================================
// Entity - declaration
// =============================================================================
class Entity {
public:
    static Entity* create();
    static void destroy(Entity* e);

    uint32_t getId() const { return _id; }
    const char* getName() const { return _name.c_str(); }
    void setName(const char* name);

    template<typename T>
    bool hasComponent() const;

    template<typename T, typename... Args>
    T* addComponent(Args&&... args);

    template<typename T>
    T* getComponent();

    template<typename T>
    void removeComponent();

    IComponent* addComponentByName(const char* typeName);
    IComponent* getComponentByName(const char* typeName);
    bool hasComponentByName(const char* typeName) const;
    void removeComponentByName(const char* typeName);

    std::vector<IComponent*> getComponents() const;
    bool isValid() const { return _id != INVALID_ID && _world != nullptr; }

    World* getWorld() const { return _world; }

    void onAttachToWorld(World* world, uint32_t id);
    void onDetachFromWorld();
    void onUpdate(float dt);
    void onStart();

private:
    Entity();
public:
    ~Entity();
private:
    void removeAllComponents();

    uint32_t _id = INVALID_ID;
    std::string _name;
    World* _world = nullptr;
    std::vector<IComponent*> _components;
    std::vector<size_t> _componentTypeHashes;

    friend class World;
};

} // namespace ayt::entity