// AYEntityReflection.cpp - AYEntityCore reflection + component registration.
//
// AY_FINALIZE_REGISTRATION_METADATA must live here only (not in component
// headers). Headers are included from many TUs via AYEntity.h; duplicate
// static finalizers corrupt the CRT debug heap.

#include "AYEntity/EntityModule.h"
#if AY_ENTITY_HAS_PARTICLE
#include <AYEntity/EntityParticleIntegrationModule.h>
#endif
#include "AYEntity/ComponentRegistration.h"

#include "AYEntity/components/HealthComponent.h"
#include "AYEntity/components/MeshComponent.h"
#include "AYEntity/components/SimTransformComponent.h"
#include "AYEntity/components/DetSimTransformComponent.h"
#include "AYEntity/components/DetSimStateComponent.h"
#include "AYEntity/components/TransformComponent.h"
#include "AYEntity/components/ActorInstanceComponent.h"
#include "AYEntity/components/PerspectiveCameraComponent.h"

#if AY_ENTITY_HAS_2D_SCHEMA
#include "AYEntity/Entity2DIntegrationModule.h"
#endif

namespace ayt::entity
{

// Reflect metadata (one static initializer per type, this TU only).
AY_FINALIZE_REGISTRATION_METADATA(Transform)
AY_FINALIZE_REGISTRATION_METADATA(HealthComponent)
AY_FINALIZE_REGISTRATION_METADATA(MeshComponent)
AY_FINALIZE_REGISTRATION_METADATA(ActorInstanceComponent)
AY_FINALIZE_REGISTRATION_METADATA(PerspectiveCameraComponent)

ComponentRegistryResult registerEntityCoreComponents(ComponentRegistry& registry)
{
#define AYT_REGISTER_COMPONENT(expression) \
    do { \
        ComponentRegistryResult result = (expression); \
        if (!result) return result; \
    } while (false)

    AYT_REGISTER_COMPONENT(registerSceneComponent<Transform>(
        registry, "Transform", "Transform", "Core"));
    AYT_REGISTER_COMPONENT(registerComponent<SimTransformComponent>(
        registry,
        "SimTransformComponent",
        "Simulation Transform",
        "Simulation"));
    {
        auto det = detail::makeComponentDescriptor<DetSimTransformComponent>(
            "DetSimTransformComponent", "Binary32 Simulation Transform", "Simulation");
        det.editorAddable = false; // Runtime state has no authoring/scene schema.
        AYT_REGISTER_COMPONENT(registry.registerComponent(std::move(det)));
    }
    {
        auto state = detail::makeComponentDescriptor<DetSimStateComponent>(
            "DetSimStateComponent", "Registered Simulation State", "Simulation");
        state.editorAddable = false;
        AYT_REGISTER_COMPONENT(registry.registerComponent(std::move(state)));
    }
    AYT_REGISTER_COMPONENT(registerSceneComponent<HealthComponent>(
        registry, "HealthComponent", "Health", "Gameplay"));
    AYT_REGISTER_COMPONENT(registerSceneComponent<MeshComponent>(
        registry, "MeshComponent", "Mesh", "Rendering"));
    AYT_REGISTER_COMPONENT(registerSceneComponent<PerspectiveCameraComponent>(
        registry, "PerspectiveCameraComponent", "Perspective Camera", "3D",
        [](Entity& entity, IComponent&) {
            if (!entity.hasComponent<Transform>()) entity.addComponent<Transform>();
        }, ComponentMultiplicity::Multiple));
    {
        auto actor = detail::makeComponentDescriptor<ActorInstanceComponent>(
            "ActorInstanceComponent", "Actor Instance", "Core");
        actor.editorAddable = false; // Only the Actor class spawner creates it.
        actor.sceneSerializable = true;
        actor.serialize = [](ayt::serializer::ISerializer& serializer,
                             const IComponent& component) {
            auto& typed = const_cast<ActorInstanceComponent&>(
                static_cast<const ActorInstanceComponent&>(component));
            ayt::serializer::SerializerForReflect<ActorInstanceComponent>
                ::applyFields(serializer, typed);
        };
        actor.deserialize = [](ayt::serializer::ISerializer& serializer,
                               IComponent& component) {
            ayt::serializer::SerializerForReflect<ActorInstanceComponent>
                ::applyReadFields(serializer,
                    static_cast<ActorInstanceComponent&>(component));
        };
        AYT_REGISTER_COMPONENT(registry.registerComponent(std::move(actor)));
    }
#if AY_ENTITY_HAS_2D_SCHEMA
    AYT_REGISTER_COMPONENT(registerEntity2DComponents(registry));
#endif
#if AY_ENTITY_HAS_PARTICLE
    AYT_REGISTER_COMPONENT(registerEntityParticleComponents(registry));
#endif
#undef AYT_REGISTER_COMPONENT
    return ComponentRegistryResult::success();
}

} // namespace ayt::entity
