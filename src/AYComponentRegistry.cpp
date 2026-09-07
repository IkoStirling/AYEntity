#include <AYEntity/ComponentRegistry.h>
#include <AYEntity/IEntity.h>

#include <utility>

namespace ayt::entity
{
namespace
{

bool equivalent(
    const ComponentDescriptor& left,
    const ComponentDescriptor& right) noexcept
{
    return left.name == right.name
        && left.displayName == right.displayName
        && left.category == right.category
        && left.type == right.type
        && left.size == right.size
        && left.alignment == right.alignment
        && left.editorAddable == right.editorAddable
        && left.sceneSerializable == right.sceneSerializable
        && left.add == right.add
        && left.get == right.get
        && left.has == right.has
        && left.remove == right.remove
        && left.serialize == right.serialize
        && left.deserialize == right.deserialize;
}

} // namespace

ComponentRegistryResult ComponentRegistryResult::success()
{
    return {};
}

ComponentRegistryResult ComponentRegistryResult::failure(
    ComponentRegistryError code,
    std::string message)
{
    return ComponentRegistryResult(code, std::move(message));
}

ComponentRegistryResult::ComponentRegistryResult(
    ComponentRegistryError code,
    std::string message)
    : _code(code), _message(std::move(message))
{
}

bool ComponentRegistryResult::succeeded() const noexcept
{
    return _code == ComponentRegistryError::None;
}

ComponentRegistryResult::operator bool() const noexcept
{
    return succeeded();
}

ComponentRegistryError ComponentRegistryResult::code() const noexcept
{
    return _code;
}

const std::string& ComponentRegistryResult::message() const noexcept
{
    return _message;
}

ComponentRegistry& ComponentRegistry::instance()
{
    static ComponentRegistry registry;
    return registry;
}

ComponentRegistryResult ComponentRegistry::registerComponent(
    ComponentDescriptor descriptor)
{
    if (descriptor.name.empty()
        || descriptor.type == std::type_index(typeid(void))
        || descriptor.size == 0
        || descriptor.alignment == 0) {
        return ComponentRegistryResult::failure(
            ComponentRegistryError::InvalidDescriptor,
            "A component descriptor requires name, type, size, and alignment");
    }
    if (descriptor.sceneSerializable
        && (descriptor.add == nullptr
            || descriptor.get == nullptr
            || descriptor.has == nullptr
            || descriptor.serialize == nullptr
            || descriptor.deserialize == nullptr)) {
        return ComponentRegistryResult::failure(
            ComponentRegistryError::InvalidDescriptor,
            "Scene-serializable component '" + descriptor.name
                + "' is missing factory or serializer callbacks");
    }
    if (descriptor.editorAddable && descriptor.add == nullptr) {
        return ComponentRegistryResult::failure(
            ComponentRegistryError::InvalidDescriptor,
            "Editor-addable component '" + descriptor.name
                + "' is missing an add callback");
    }

    if (descriptor.displayName.empty()) {
        descriptor.displayName = descriptor.name;
    }
    if (descriptor.category.empty()) {
        descriptor.category = "General";
    }

    const ComponentDescriptor* sameName = find(descriptor.name);
    if (sameName != nullptr) {
        if (sameName->type != descriptor.type) {
            return ComponentRegistryResult::failure(
                ComponentRegistryError::DuplicateName,
                "Component name '" + descriptor.name
                    + "' is already bound to another C++ type");
        }
        if (!equivalent(*sameName, descriptor)) {
            return ComponentRegistryResult::failure(
                ComponentRegistryError::ConflictingRegistration,
                "Component '" + descriptor.name
                    + "' was registered again with different metadata");
        }
        return ComponentRegistryResult::success();
    }

    // Sealing prevents additions and metadata changes, but an equivalent
    // registration remains a valid no-op. This lets a second Host assemble
    // the same explicit module graph in one process without reopening the
    // registry or making module code branch on global startup history.
    if (_sealed) {
        return ComponentRegistryResult::failure(
            ComponentRegistryError::Sealed,
            "ComponentRegistry is sealed; component registration is closed");
    }

    if (const ComponentDescriptor* sameType = find(descriptor.type)) {
        return ComponentRegistryResult::failure(
            ComponentRegistryError::DuplicateType,
            "C++ component type is already registered as '" + sameType->name
                + "' and cannot also use '" + descriptor.name + "'");
    }

    _descriptors.push_back(std::move(descriptor));
    return ComponentRegistryResult::success();
}

void ComponentRegistry::seal() noexcept
{
    _sealed = true;
}

bool ComponentRegistry::isSealed() const noexcept
{
    return _sealed;
}

std::size_t ComponentRegistry::size() const noexcept
{
    return _descriptors.size();
}

const ComponentDescriptor* ComponentRegistry::find(
    std::string_view name) const noexcept
{
    for (const ComponentDescriptor& descriptor : _descriptors) {
        if (descriptor.name == name) {
            return &descriptor;
        }
    }
    return nullptr;
}

const ComponentDescriptor* ComponentRegistry::find(
    std::type_index type) const noexcept
{
    for (const ComponentDescriptor& descriptor : _descriptors) {
        if (descriptor.type == type) {
            return &descriptor;
        }
    }
    return nullptr;
}

const ComponentDescriptor* ComponentRegistry::find(
    const IComponent& component) const noexcept
{
    return find(std::type_index(typeid(component)));
}

const std::vector<ComponentDescriptor>& ComponentRegistry::descriptors()
    const noexcept
{
    return _descriptors;
}

} // namespace ayt::entity
