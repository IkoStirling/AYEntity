// AYComponentFactory.cpp - registry-backed component/editor/scene dispatch.

#include <AYEntity/ComponentFactory.h>
#include <AYEntity/ComponentRegistry.h>
#include <AYEntity/EntityImpl.h>

namespace ayt::entity
{
namespace
{

const ComponentDescriptor* findEntry(const char* typeName) noexcept
{
    if (typeName == nullptr || typeName[0] == '\0') {
        return nullptr;
    }
    return ComponentRegistry::instance().find(typeName);
}

} // namespace

IComponent* ComponentFactory::addComponent(
    Entity& entity,
    const char* typeName)
{
    const ComponentDescriptor* entry = findEntry(typeName);
    return entry != nullptr && entry->add != nullptr
        ? entry->add(entity)
        : nullptr;
}

IComponent* ComponentFactory::getComponent(
    Entity& entity,
    const char* typeName)
{
    const ComponentDescriptor* entry = findEntry(typeName);
    return entry != nullptr && entry->get != nullptr
        ? entry->get(entity)
        : nullptr;
}

bool ComponentFactory::hasComponent(
    const Entity& entity,
    const char* typeName)
{
    const ComponentDescriptor* entry = findEntry(typeName);
    return entry != nullptr
        && entry->has != nullptr
        && entry->has(entity);
}

bool ComponentFactory::removeComponent(
    Entity& entity,
    const char* typeName)
{
    const ComponentDescriptor* entry = findEntry(typeName);
    if (entry == nullptr
        || entry->multiplicity != ComponentMultiplicity::Single
        || entry->has == nullptr
        || entry->remove == nullptr
        || !entry->has(entity)) {
        return false;
    }
    entry->remove(entity);
    return true;
}

const char* ComponentFactory::registeredTypeName(
    const IComponent& component)
{
    const ComponentDescriptor* entry =
        ComponentRegistry::instance().find(component);
    return entry != nullptr ? entry->name.c_str() : nullptr;
}

bool ComponentFactory::isSceneSerializable(const char* typeName)
{
    const ComponentDescriptor* entry = findEntry(typeName);
    return entry != nullptr
        && entry->sceneSerializable
        && entry->serialize != nullptr
        && entry->deserialize != nullptr;
}

void ComponentFactory::serializeComponent(
    ayt::serializer::ISerializer& serializer,
    const IComponent& component)
{
    const ComponentDescriptor* entry =
        ComponentRegistry::instance().find(component);
    if (entry != nullptr
        && entry->sceneSerializable
        && entry->serialize != nullptr) {
        entry->serialize(serializer, component);
    }
}

bool ComponentFactory::deserializeComponent(
    ayt::serializer::ISerializer& serializer,
    const char* typeName,
    IComponent& component)
{
    const ComponentDescriptor* entry = findEntry(typeName);
    const ComponentDescriptor* componentEntry =
        ComponentRegistry::instance().find(component);
    if (entry == nullptr
        || componentEntry != entry
        || !entry->sceneSerializable
        || entry->deserialize == nullptr) {
        return false;
    }
    entry->deserialize(serializer, component);
    return true;
}

void ComponentFactory::afterSceneDeserialize(
    Entity& entity,
    const char* typeName,
    IComponent& component)
{
    const ComponentDescriptor* entry = findEntry(typeName);
    const ComponentDescriptor* componentEntry =
        ComponentRegistry::instance().find(component);
    if (entry != nullptr && componentEntry == entry
        && entry->afterSceneDeserialize != nullptr) {
        entry->afterSceneDeserialize(entity, component);
    }
}

} // namespace ayt::entity
