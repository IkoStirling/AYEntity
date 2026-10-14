#pragma once

#include <AYEntity.h>
#include <AYEntity/ComponentRegistry.h>
#include <AYSerializer/SerializerForReflect.h>

#include <concepts>
#include <string_view>
#include <utility>

namespace ayt::entity
{
namespace detail
{

template<std::derived_from<IComponent> T>
ComponentDescriptor makeComponentDescriptor(
    std::string_view name,
    std::string_view displayName,
    std::string_view category,
    ComponentMultiplicity multiplicity = ComponentMultiplicity::Single)
{
    ComponentDescriptor descriptor;
    descriptor.name = std::string(name);
    descriptor.displayName = displayName.empty()
        ? descriptor.name
        : std::string(displayName);
    descriptor.category = category.empty() ? "General" : std::string(category);
    descriptor.multiplicity = multiplicity;
    descriptor.type = std::type_index(typeid(T));
    descriptor.size = sizeof(T);
    descriptor.alignment = alignof(T);
    descriptor.intrinsic = std::is_same_v<T, Transform>;
    descriptor.editorAddable = !descriptor.intrinsic;
    // Standard spatial components. Custom component descriptors can explicitly opt in.
    descriptor.requiresSpatial = descriptor.intrinsic
        || name == "MeshComponent" || name == "SpriteComponent" || name == "TilemapComponent"
        || name == "PerspectiveCameraComponent" || name == "OrthoCameraComponent"
        || name == "RigidBodyComponent" || name == "ColliderComponent"
        || name == "SimTransformComponent" || name == "DetSimTransformComponent"
        || name == "SkeletonComponent" || name == "AnimationComponent" || name == "BlendSpaceComponent"
        || name == "AnimationStateMachineComponent" || name == "SpriteAnimationComponent" || name == "ParticleSystemComponent"
        || name == "AudioEmitterComponent" || name == "AudioListenerComponent"
        || name == "CameraControllerComponent" || name == "ParticleEmitterComponent"
        || name == "ParticleEffectComponent" || name == "ParticleSurface2DComponent";
    descriptor.add = multiplicity == ComponentMultiplicity::Multiple
        ? +[](Entity& entity) -> IComponent* { return entity.createComponent<T>(); }
        : +[](Entity& entity) -> IComponent* { return entity.addComponent<T>(); };
    descriptor.get = [](Entity& entity) -> IComponent* {
        return entity.getComponent<T>();
    };
    descriptor.has = [](const Entity& entity) {
        return entity.hasComponent<T>();
    };
    descriptor.remove = [](Entity& entity) {
        entity.removeComponent<T>();
    };
    return descriptor;
}

} // namespace detail

// Explicit runtime/editor registration without scene wire serialization.
template<std::derived_from<IComponent> T>
[[nodiscard]] ComponentRegistryResult registerComponent(
    ComponentRegistry& registry,
    std::string_view name,
    std::string_view displayName = {},
    std::string_view category = {},
    ComponentMultiplicity multiplicity = ComponentMultiplicity::Single)
{
    return registry.registerComponent(detail::makeComponentDescriptor<T>(
        name,
        displayName,
        category, multiplicity));
}

// Explicit registration for a component supported by .ayscene dispatch.
template<std::derived_from<IComponent> T>
[[nodiscard]] ComponentRegistryResult registerSceneComponent(
    ComponentRegistry& registry,
    std::string_view name,
    std::string_view displayName = {},
    std::string_view category = {},
    ComponentAfterSceneDeserializeFn afterSceneDeserialize = nullptr,
    ComponentMultiplicity multiplicity = ComponentMultiplicity::Single)
{
    ComponentDescriptor descriptor = detail::makeComponentDescriptor<T>(
        name,
        displayName,
        category, multiplicity);
    descriptor.sceneSerializable = true;
    descriptor.serialize = [](
        ayt::serializer::ISerializer& serializer,
        const IComponent& component) {
        auto& typed = const_cast<T&>(static_cast<const T&>(component));
        ayt::serializer::SerializerForReflect<T>::applyFields(
            serializer,
            typed);
    };
    descriptor.deserialize = [](
        ayt::serializer::ISerializer& serializer,
        IComponent& component) {
        ayt::serializer::SerializerForReflect<T>::applyReadFields(
            serializer,
            static_cast<T&>(component));
    };
    descriptor.afterSceneDeserialize = afterSceneDeserialize;
    return registry.registerComponent(std::move(descriptor));
}

} // namespace ayt::entity
