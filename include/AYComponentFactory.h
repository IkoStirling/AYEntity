#pragma once
// AYComponentFactory.h — P4-B component dispatch by Reflect type name.

#include <IAYEntity.h>
#include <ayserializer/SerializerCore.h>

namespace ayt::entity
{

class Entity;

// Scene v0 serializable components (see docs/serialization-conventions.md §4.3).
class ComponentFactory {
public:
    static IComponent* addComponent(Entity& entity, const char* typeName);
    static IComponent* getComponent(Entity& entity, const char* typeName);
    static bool hasComponent(const Entity& entity, const char* typeName);
    static bool isSceneSerializable(const char* typeName);

    static void serializeComponent(ayt::serializer::ISerializer& s, const IComponent& component);
    static bool deserializeComponent(ayt::serializer::ISerializer& s, const char* typeName,
                                   IComponent& component);
};

} // namespace ayt::entity
