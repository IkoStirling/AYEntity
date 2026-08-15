// AYComponentFactory.cpp — P4-B scene component factory + reflect dispatch.

#include "AYEntity/ComponentFactory.h"
#include "AYEntity/EntityImpl.h"

#include <AYEntity/components/AnimationComponent.h>
#include <AYEntity/components/HealthComponent.h>
#include <AYEntity/components/MeshComponent.h>
#include <AYEntity/components/OrthoCameraComponent.h>
#include <AYEntity/components/SkeletonComponent.h>
#include <AYEntity/components/SpriteComponent.h>
#include <AYEntity/components/TilemapComponent.h>
#include <AYEntity/components/TransformComponent.h>

#include <AYSerializer.h>
#include <AYSerializer/SerializerForReflect.h>
#include <AYReflect.h>

#include <cstring>

using ayt::serializer::SerializerForReflect;

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
// Write Serialize fields inline via SerializerForReflect::applyFields (P4-E).
template<typename T>
void serializeTyped(ayt::serializer::ISerializer& s, IComponent& component) {
    SerializerForReflect<T>::applyFields(s, static_cast<T&>(component));
}

template<typename T>
void deserializeTyped(ayt::serializer::ISerializer& s, IComponent& component) {
    SerializerForReflect<T>::applyReadFields(s, static_cast<T&>(component));
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
        // CM-3 (2026-08-11) — 2D lane components (.ayscene wire types).
        {"TilemapComponent",
         addTyped<TilemapComponent>, getTyped<TilemapComponent>, hasTyped<TilemapComponent>,
         serializeTyped<TilemapComponent>, deserializeTyped<TilemapComponent>},
        {"SpriteComponent",
         addTyped<SpriteComponent>, getTyped<SpriteComponent>, hasTyped<SpriteComponent>,
         serializeTyped<SpriteComponent>, deserializeTyped<SpriteComponent>},
        {"OrthoCameraComponent",
         addTyped<OrthoCameraComponent>, getTyped<OrthoCameraComponent>, hasTyped<OrthoCameraComponent>,
         serializeTyped<OrthoCameraComponent>, deserializeTyped<OrthoCameraComponent>},
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
