#pragma once
// AYEntity/EntityImpl.h - Entity class declaration (non-template)

#include <AYEntity/IEntity.h>
#include <AYEntity/SparseSet.h>
#include <AYEntity/EntityHandle.h>
#include <vector>
#include <string>
#include <memory>
#include <AYMath/MathTypes.h>

namespace ayt::entity
{

class World;
struct Transform;
/// Data entities carry logic/state; Spatial entities additionally own one intrinsic local pose.


constexpr uint32_t MAX_ENTITIES = 10000;

// Persistent, Entity-local identity. The value is independent of runtime
// EntityHandle generations and must never be used as a raw pointer handle.
std::string makeComponentInstanceId();
std::string legacyComponentInstanceId(const std::string& registeredTypeName);
bool isValidComponentInstanceId(const std::string& id) noexcept;

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
    struct ComponentInstance {
        IComponent* component = nullptr;
        size_t typeHash = 0;
        std::string id;
        std::string displayName;
        bool restoredIdentity = false;
    };

    static Entity* create();
    static void destroy(Entity* e);

    EntityKind kind() const noexcept { return _kind; }
    bool migratingLegacySpatialFields() const noexcept { return _migratingLegacySpatialFields; }
    void setLegacySpatialMigration(bool enabled) noexcept { _migratingLegacySpatialFields = enabled; }
    bool isSpatial() const noexcept { return _kind == EntityKind::Spatial; }
    /// Intrinsic LOCAL pose. Null for Data; never removable from Spatial entities.
    Transform* transform() noexcept { return _transform.get(); }
    const Transform* transform() const noexcept { return _transform.get(); }
    const std::string& persistentId() const noexcept { return _persistentId; }
    bool restorePersistentId(const std::string& id);
    /// Explicit conversion; failure does not change pose, components or relations.
    bool convertKind(EntityKind kind, std::string* error = nullptr);
    Entity* spatialParent() const;
    Entity* owner() const;
    std::vector<Entity*> spatialChildren() const;
    std::vector<Entity*> ownedEntities() const;
    /// Same-World, acyclic relations. Ownership is independent of spatial attachment.
    bool setSpatialParent(Entity* parent, ReparentMode mode = ReparentMode::KeepLocal,
                          std::string* error = nullptr);
    bool setOwner(Entity* owner, std::string* error = nullptr);
    /// Exact affine world composition, including nonuniform scale/shear.
    math::Float4x4 worldMatrix(float interpolationAlpha = 1.0f) const;
    /// Position plus scale-free orientation, useful for cameras and physics.
    bool worldPose(math::FVector3& position, math::FQuaternion& rotation,
                   float interpolationAlpha = 1.0f) const;
    /// Checked world-to-local conversion; rejects singular/sheared local TRS.
    bool setWorldMatrix(const math::Float4x4& matrix, std::string* error = nullptr);
    /// Compatibility acquisition for old addComponent<Transform> callers only.
    Transform* acquireLegacyTransform();
    bool prepareSpatialComponent(bool requiresSpatial, std::string* error = nullptr);

    uint32_t getId() const { return _id; }
    const char* getName() const { return _name.c_str(); }
    void setName(const char* name);

    template<typename T>
    bool hasComponent() const;

    template<typename T, typename... Args>
    T* addComponent(Args&&... args);

    /// Create one new instance. A Single type rejects a second instance.
    template<typename T, typename... Args>
    T* createComponent(Args&&... args);

    template<typename T>
    T* getComponent();

    template<typename T>
    std::vector<T*> getComponents() const;

    template<typename T>
    void removeComponent();

    IComponent* addComponentByName(const char* typeName);
    IComponent* getComponentByName(const char* typeName);
    bool hasComponentByName(const char* typeName) const;
    void removeComponentByName(const char* typeName);
    IComponent* findComponentById(const std::string& id) const noexcept;
    bool removeComponentById(const std::string& id);

    std::vector<IComponent*> getComponents() const;
    /// Runtime components with persistent Entity-local IDs and authoring labels.
    /// Pointers and references become invalid after structural mutation.
    const std::vector<ComponentInstance>& componentInstances() const noexcept {
        return _componentInstances;
    }
    const ComponentInstance* componentInstance(const IComponent* component) const noexcept;
    const ComponentInstance* findComponentInstance(const std::string& id) const noexcept;
    /// Restore a persisted ID before exposing a newly loaded component.
    /// Rejects IDs owned by any other component without changing either identity.
    /// Once restored, only an idempotent assignment of the same ID succeeds.
    /// Ordinary gameplay must not change an existing component's identity.
    bool setComponentInstanceId(const IComponent* component, const std::string& id);
    /// Rename the authoring label without changing the component's identity.
    bool setComponentDisplayName(const IComponent* component, std::string name);
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

    EntityKind _kind = EntityKind::Data;
    bool _legacySpatialPromotion = true;
    bool _identityRestored = false;
    bool _migratingLegacySpatialFields = false;
    std::string _persistentId;
    std::unique_ptr<Transform> _transform;
    EntityHandle _spatialParent{}, _owner{};
    uint32_t _id = INVALID_ID;
    std::string _name;
    World* _world = nullptr;
    std::vector<ComponentInstance> _componentInstances;

    friend class World;
};

} // namespace ayt::entity
