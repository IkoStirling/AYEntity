#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <type_traits>
#include <typeindex>
#include <utility>
#include <vector>

namespace ayt::serializer
{
class ISerializer;
}

namespace ayt::entity
{

inline constexpr std::string_view kComponentRegistryModuleService =
    "ayt.entity.ComponentRegistry";

class Entity;
class IComponent;

using ComponentAddFn = IComponent* (*)(Entity&);
using ComponentGetFn = IComponent* (*)(Entity&);
using ComponentHasFn = bool (*)(const Entity&);
using ComponentRemoveFn = void (*)(Entity&);
using ComponentSerializeFn = void (*)(
    ayt::serializer::ISerializer&,
    const IComponent&);
using ComponentDeserializeFn = void (*)(
    ayt::serializer::ISerializer&,
    IComponent&);
using ComponentAfterSceneDeserializeFn = void (*)(Entity&, IComponent&);

// Stable component identity is the name. type is process-local and is used
// only for fast typed lookup; it must never be serialized.
struct ComponentDescriptor
{
    std::string name;
    std::string displayName;
    std::string category;
    std::type_index type = typeid(void);
    std::size_t size = 0;
    std::size_t alignment = 0;
    bool editorAddable = false;
    bool sceneSerializable = false;

    ComponentAddFn add = nullptr;
    ComponentGetFn get = nullptr;
    ComponentHasFn has = nullptr;
    ComponentRemoveFn remove = nullptr;
    ComponentSerializeFn serialize = nullptr;
    ComponentDeserializeFn deserialize = nullptr;
    // Optional normalization hook invoked after one component has been read
    // from a Scene. It lets schema modules migrate legacy component-local
    // data into entity-level dependencies without coupling SceneSerializer to
    // optional component types.
    ComponentAfterSceneDeserializeFn afterSceneDeserialize = nullptr;
};

enum class ComponentRegistryError
{
    None,
    Sealed,
    InvalidDescriptor,
    DuplicateName,
    DuplicateType,
    ConflictingRegistration
};

class ComponentRegistryResult
{
public:
    [[nodiscard]] static ComponentRegistryResult success();
    [[nodiscard]] static ComponentRegistryResult failure(
        ComponentRegistryError code,
        std::string message);

    [[nodiscard]] bool succeeded() const noexcept;
    [[nodiscard]] explicit operator bool() const noexcept;
    [[nodiscard]] ComponentRegistryError code() const noexcept;
    [[nodiscard]] const std::string& message() const noexcept;

private:
    ComponentRegistryResult() = default;
    ComponentRegistryResult(ComponentRegistryError code, std::string message);

    ComponentRegistryError _code = ComponentRegistryError::None;
    std::string _message;
};

// Process-lifetime component metadata. Registration is intentionally
// single-threaded and explicit during module startup. After seal(), lookup is
// read-only and descriptor order/pointers remain stable for editor tooling.
class ComponentRegistry
{
public:
    ComponentRegistry() = default;

    [[nodiscard]] static ComponentRegistry& instance();

    [[nodiscard]] ComponentRegistryResult registerComponent(
        ComponentDescriptor descriptor);

    // Legacy typed registration bridge. It records identity only; dynamic
    // add/remove and scene serialization require ComponentRegistration.h.
    template<typename T>
    [[nodiscard]] ComponentRegistryResult registerType(std::string_view name)
    {
        static_assert(
            std::is_base_of_v<IComponent, T>,
            "T must inherit ayt::entity::IComponent");

        ComponentDescriptor descriptor;
        descriptor.name = std::string(name);
        descriptor.displayName = descriptor.name;
        descriptor.category = "Legacy";
        descriptor.type = std::type_index(typeid(T));
        descriptor.size = sizeof(T);
        descriptor.alignment = alignof(T);
        return registerComponent(std::move(descriptor));
    }

    void seal() noexcept;
    [[nodiscard]] bool isSealed() const noexcept;
    [[nodiscard]] std::size_t size() const noexcept;

    [[nodiscard]] const ComponentDescriptor* find(
        std::string_view name) const noexcept;
    [[nodiscard]] const ComponentDescriptor* find(
        std::type_index type) const noexcept;
    [[nodiscard]] const ComponentDescriptor* find(
        const IComponent& component) const noexcept;

    template<typename T>
    [[nodiscard]] const ComponentDescriptor* find() const noexcept
    {
        return find(std::type_index(typeid(T)));
    }

    [[nodiscard]] const std::vector<ComponentDescriptor>& descriptors()
        const noexcept;

private:
    std::vector<ComponentDescriptor> _descriptors;
    bool _sealed = false;
};

} // namespace ayt::entity
