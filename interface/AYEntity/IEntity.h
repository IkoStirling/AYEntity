#pragma once
// AYEntity/IEntity.h - AYEntity main interfaces

#include <AYCore.h>
#include <AYGameLoop.h>
#include <functional>
#include <vector>
#include <string>
#include <cstdint>
#include <unordered_map>

namespace ayt::entity
{

// Promoted from ::ayt for use inside ayt::entity component headers.
using ::ayt::FieldAttribute;

// Compile-time field attrs for AY_PROPERTY (MSVC NTP requires integral constants).
inline constexpr uint32_t kAttrSerialize =
    static_cast<uint32_t>(::ayt::reflect::FieldAttribute::Serialize);
inline constexpr uint32_t kAttrSerializeNetReplicate =
    static_cast<uint32_t>(::ayt::reflect::FieldAttribute::Serialize) |
    static_cast<uint32_t>(::ayt::reflect::FieldAttribute::NetReplicate);

// =============================================================================
// Forward declarations
// =============================================================================
class Entity;
class World;
class IComponent;
class ISystem;
class IComponentStorage;
struct EntityHandle;

// =============================================================================
// Constants
// =============================================================================
constexpr uint32_t INVALID_ID = 0;
constexpr uint32_t INVALID_INDEX = UINT32_MAX;

// =============================================================================
// IComponent - Component base class
// =============================================================================
class IComponent {
public:
    virtual ~IComponent() = default;

    virtual const char* getName() const = 0;

    virtual void onAttach(Entity* entity) {}
    virtual void onDetach() {}
    virtual void onUpdate(float dt) {}
    virtual void onStart() {}
};

// =============================================================================
// ISystem - System interface
// =============================================================================
class ISystem {
public:
    virtual ~ISystem() = default;

    virtual const char* getName() const = 0;
    virtual void onUpdate(float dt) = 0;
    virtual void onStart() {}

    int32_t getPriority() const { return _priority; }
    void setPriority(int32_t priority) { _priority = priority; }

protected:
    int32_t _priority = 0;
};

// =============================================================================
// IComponentStorage - Component storage interface
// =============================================================================
class IComponentStorage {
public:
    virtual ~IComponentStorage() = default;

    virtual void* get(uint32_t entityId) = 0;
    virtual bool has(uint32_t entityId) const = 0;
    virtual void add(uint32_t entityId, void* component) = 0;
    virtual void remove(uint32_t entityId) = 0;
    virtual size_t size() const = 0;
    virtual void clear() = 0;

    virtual void forEach(std::function<void(uint32_t entityId, void* component)> callback) = 0;
};

// =============================================================================
// Macros
// =============================================================================
#define AY_COMPONENT(T) \
    static_assert(std::is_base_of_v<::ayt::entity::IComponent, T>, #T " must inherit IComponent"); \
    namespace { \
        struct AYT_ComponentRegistrar_##T { \
            AYT_ComponentRegistrar_##T() { \
                if (::ayt::entity::World::isComponentTypeRegistered<T>()) { \
                    return; \
                } \
                ::ayt::entity::World::registerComponentType<T>(#T); \
            } \
        }; \
        static AYT_ComponentRegistrar_##T AYT_g_component_registrar_##T; \
    }

#define AY_SYSTEM(T, priority) \
    static_assert(std::is_base_of_v<::ayt::entity::ISystem, T>, #T " must inherit ISystem"); \
    namespace { \
        struct AYT_SystemRegistrar_##T { \
            AYT_SystemRegistrar_##T() { \
                ::ayt::entity::World::instance().registerSystem<T>(priority); \
            } \
        }; \
        static AYT_SystemRegistrar_##T AYT_g_system_registrar_##T; \
    }

} // namespace ayt::entity