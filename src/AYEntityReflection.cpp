// AYEntityReflection.cpp - single-TU reflect + World component registration.
//
// AY_FINALIZE_REGISTRATION_METADATA must live here only (not in component
// headers). Headers are included from many TUs via AYEntity.h; duplicate
// static finalizers corrupt the CRT debug heap.

#include "AYEntity/EntityModule.h"
#include "AYEntity/ComponentRegistration.h"

#include "AYEntity/components/AnimationComponent.h"
#include "AYEntity/components/AnimationStateMachineComponent.h"
#include "AYEntity/components/BlendSpaceComponent.h"
#include "AYEntity/components/ColliderComponent.h"
#include "AYEntity/components/HealthComponent.h"
#include "AYEntity/components/MeshComponent.h"
#include "AYEntity/components/NetworkComponent.h"
#include "AYEntity/components/OrthoCameraComponent.h"
#include "AYEntity/components/RigidBodyComponent.h"
#include "AYEntity/components/ScriptComponent.h"
#include "AYEntity/components/SkeletonComponent.h"
#include "AYEntity/components/SpriteComponent.h"
#include "AYEntity/components/TilemapComponent.h"
#include "AYEntity/components/TransformComponent.h"

namespace ayt::entity
{

// Reflect metadata (one static initializer per type, this TU only).
AY_FINALIZE_REGISTRATION_METADATA(AdditiveLayerSpec)
AY_FINALIZE_REGISTRATION_METADATA(Transform)
AY_FINALIZE_REGISTRATION_METADATA(HealthComponent)
AY_FINALIZE_REGISTRATION_METADATA(MeshComponent)
AY_FINALIZE_REGISTRATION_METADATA(SkeletonComponent)
AY_FINALIZE_REGISTRATION_METADATA(AnimationComponent)
// P3.1 (2026-08-06) — L1 state machine component.
AY_FINALIZE_REGISTRATION_METADATA(AnimationStateMachineComponent)
// CM-3 (2026-08-11) — 2D lane components. AY_FINALIZE_REGISTRATION_METADATA
// must live in this single TU only (see the header comment — duplicate
// static finalizers corrupt the CRT debug heap).
AY_FINALIZE_REGISTRATION_METADATA(TilemapComponent)
// Collider (2026-08-19) — the shape spec struct must be finalized before the
// component that embeds vector<ColliderShapeSpec> (design.md §TryRegisterVector
// order constraint). Both live here, in declaration order.
AY_FINALIZE_REGISTRATION_METADATA(ColliderShapeSpec)
AY_FINALIZE_REGISTRATION_METADATA(ColliderComponent)
AY_FINALIZE_REGISTRATION_METADATA(SpriteComponent)
AY_FINALIZE_REGISTRATION_METADATA(OrthoCameraComponent)

ComponentRegistryResult registerEntityComponents(ComponentRegistry& registry)
{
#define AYT_REGISTER_COMPONENT(expression) \
    do { \
        ComponentRegistryResult result = (expression); \
        if (!result) return result; \
    } while (false)

    AYT_REGISTER_COMPONENT(registerSceneComponent<Transform>(
        registry, "Transform", "Transform", "Core"));
    AYT_REGISTER_COMPONENT(registerSceneComponent<HealthComponent>(
        registry, "HealthComponent", "Health", "Gameplay"));
    AYT_REGISTER_COMPONENT(registerSceneComponent<MeshComponent>(
        registry, "MeshComponent", "Mesh", "Rendering"));
    AYT_REGISTER_COMPONENT(registerSceneComponent<SkeletonComponent>(
        registry, "SkeletonComponent", "Skeleton", "Animation"));
    AYT_REGISTER_COMPONENT(registerSceneComponent<AnimationComponent>(
        registry, "AnimationComponent", "Animation", "Animation"));
    AYT_REGISTER_COMPONENT(registerComponent<BlendSpaceComponent>(
        registry, "BlendSpaceComponent", "Blend Space", "Animation"));
    AYT_REGISTER_COMPONENT(registerComponent<AnimationStateMachineComponent>(
        registry,
        "AnimationStateMachineComponent",
        "Animation State Machine",
        "Animation"));
    AYT_REGISTER_COMPONENT(registerComponent<ScriptComponent>(
        registry, "ScriptComponent", "Script", "Scripting"));
    AYT_REGISTER_COMPONENT(registerComponent<NetworkComponent>(
        registry, "NetworkComponent", "Network", "Networking"));
    AYT_REGISTER_COMPONENT(registerComponent<RigidBodyComponent>(
        registry, "RigidBodyComponent", "Rigid Body", "Physics"));
    AYT_REGISTER_COMPONENT(registerSceneComponent<ColliderComponent>(
        registry, "ColliderComponent", "Collider", "Physics"));
    AYT_REGISTER_COMPONENT(registerSceneComponent<TilemapComponent>(
        registry, "TilemapComponent", "Tilemap", "2D"));
    AYT_REGISTER_COMPONENT(registerSceneComponent<SpriteComponent>(
        registry, "SpriteComponent", "Sprite", "2D"));
    AYT_REGISTER_COMPONENT(registerSceneComponent<OrthoCameraComponent>(
        registry, "OrthoCameraComponent", "Orthographic Camera", "2D"));

#undef AYT_REGISTER_COMPONENT
    return ComponentRegistryResult::success();
}

void registerEntityComponents()
{
    (void)registerEntityComponents(ComponentRegistry::instance());
}

} // namespace ayt::entity
