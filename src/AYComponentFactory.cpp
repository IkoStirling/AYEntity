// AYComponentFactory.cpp — P4-B scene component factory + reflect dispatch.

#include "AYComponentFactory.h"
#include "AYEntityImpl.h"

#include <components/AYAnimationComponent.h>
#include <components/AYHealthComponent.h>
#include <components/AYMeshComponent.h>
#include <components/AYSkeletonComponent.h>
#include <components/AYTransformComponent.h>

#include <AYSerializer.h>
#include <ayserializer/SerializerForReflect.h>
#include <AYReflect.h>

#include <cstring>

using ayt::serializer::NameTable;
using ayt::serializer::PendingRef;
using ayt::serializer::detail::deserializeReflectFields;
using ayt::serializer::detail::serializeReflectField;

namespace ayt::entity
{
namespace
{

using SerializeFn = void (*)(ayt::serializer::ISerializer&, IComponent&);
using DeserializeFn = void (*)(ayt::serializer::ISerializer&, IComponent&);

struct ComponentWireEntry {
    const char* typeName;
    IComponent* (*add)(Entity&);
    IComponent* (*get)(Entity&);
    bool (*has)(const Entity&);
    SerializeFn serialize;
    DeserializeFn deserialize;
};

template<typename T>
IComponent* addTyped(Entity& entity) {
    return entity.addComponent<T>();
}

template<typename T>
IComponent* getTyped(Entity& entity) {
    return entity.getComponent<T>();
}

template<typename T>
bool hasTyped(const Entity& entity) {
    return entity.hasComponent<T>();
}

// Scene envelope already opened a component object and wrote "$type".
// Write Serialize fields inline — do not wrap with SerializerForReflect::apply's
// beginObject/endObject or the root envelope is replaced (see P4-B).
template<typename T>
void serializeTyped(ayt::serializer::ISerializer& s, IComponent& component) {
    auto* type = ayt::reflect::TypeRegistryImpl::instance().findType<T>();
    if (type == nullptr) {
        return;
    }
    T& obj = static_cast<T&>(component);
    const NameTable& nameTable = s.getNameTable();
    for (uint32_t i = 0; i < type->getFieldCount(); ++i) {
        auto* field = type->getField(i);
        if (!field->hasAttribute(ayt::reflect::FieldAttribute::Serialize)) {
            continue;
        }
        serializeReflectField(s, field, &obj, &nameTable);
    }
}

template<typename T>
void deserializeTyped(ayt::serializer::ISerializer& s, IComponent& component) {
    auto* type = ayt::reflect::TypeRegistryImpl::instance().findType<T>();
    if (type == nullptr) {
        return;
    }
    T& obj = static_cast<T&>(component);
    std::vector<PendingRef>& pending = s.pendingRefs();
    deserializeReflectFields(s, type, &obj, &pending);
    if (!pending.empty()) {
        const auto& lookup = s.getNameLookup();
        const auto& owners = s.getNameLookupOwners();
        if (!lookup.empty()) {
            ayt::serializer::resolveReferences(
                s, pending, lookup, owners.empty() ? nullptr : &owners);
        }
        pending.clear();
    }
}

const ComponentWireEntry* findEntry(const char* typeName) {
    if (typeName == nullptr || typeName[0] == '\0') {
        return nullptr;
    }

    static const ComponentWireEntry kEntries[] = {
        {"Transform",
         addTyped<Transform>, getTyped<Transform>, hasTyped<Transform>,
         serializeTyped<Transform>, deserializeTyped<Transform>},
        {"MeshComponent",
         addTyped<MeshComponent>, getTyped<MeshComponent>, hasTyped<MeshComponent>,
         serializeTyped<MeshComponent>, deserializeTyped<MeshComponent>},
        {"SkeletonComponent",
         addTyped<SkeletonComponent>, getTyped<SkeletonComponent>, hasTyped<SkeletonComponent>,
         serializeTyped<SkeletonComponent>, deserializeTyped<SkeletonComponent>},
        {"AnimationComponent",
         addTyped<AnimationComponent>, getTyped<AnimationComponent>, hasTyped<AnimationComponent>,
         serializeTyped<AnimationComponent>, deserializeTyped<AnimationComponent>},
        {"HealthComponent",
         addTyped<HealthComponent>, getTyped<HealthComponent>, hasTyped<HealthComponent>,
         serializeTyped<HealthComponent>, deserializeTyped<HealthComponent>},
    };

    for (const ComponentWireEntry& entry : kEntries) {
        if (std::strcmp(entry.typeName, typeName) == 0) {
            return &entry;
        }
    }
    return nullptr;
}

} // namespace

IComponent* ComponentFactory::addComponent(Entity& entity, const char* typeName) {
    const ComponentWireEntry* entry = findEntry(typeName);
    if (entry == nullptr) {
        return nullptr;
    }
    return entry->add(entity);
}

IComponent* ComponentFactory::getComponent(Entity& entity, const char* typeName) {
    const ComponentWireEntry* entry = findEntry(typeName);
    if (entry == nullptr) {
        return nullptr;
    }
    return entry->get(entity);
}

bool ComponentFactory::hasComponent(const Entity& entity, const char* typeName) {
    const ComponentWireEntry* entry = findEntry(typeName);
    if (entry == nullptr) {
        return false;
    }
    return entry->has(entity);
}

bool ComponentFactory::isSceneSerializable(const char* typeName) {
    return findEntry(typeName) != nullptr;
}

void ComponentFactory::serializeComponent(ayt::serializer::ISerializer& s, const IComponent& component) {
    const ComponentWireEntry* entry = findEntry(component.getName());
    if (entry == nullptr) {
        return;
    }
    entry->serialize(s, const_cast<IComponent&>(component));
}

bool ComponentFactory::deserializeComponent(ayt::serializer::ISerializer& s, const char* typeName,
                                          IComponent& component) {
    const ComponentWireEntry* entry = findEntry(typeName);
    if (entry == nullptr) {
        return false;
    }
    entry->deserialize(s, component);
    return true;
}

} // namespace ayt::entity
