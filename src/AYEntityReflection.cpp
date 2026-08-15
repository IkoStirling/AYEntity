// AYEntityReflection.cpp - single-TU reflect + World component registration.
//
// AY_FINALIZE_REGISTRATION_METADATA must live here only (not in component
// headers). Headers are included from many TUs via AYEntity.h; duplicate
// static finalizers corrupt the CRT debug heap.

#include "AYEntity/EntityModule.h"
#include "AYEntity/World.h"

#include "AYEntity/components/AnimationComponent.h"
#include "AYEntity/components/AnimationStateMachineComponent.h"
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
AY_FINALIZE_REGISTRATION_METADATA(SpriteComponent)
AY_FINALIZE_REGISTRATION_METADATA(OrthoCameraComponent)

void registerEntityComponents()
{
    static bool registered = false;
    if (registered) {
        return;
    }
    registered = true;

    // Parentheses prevent Win32 GDI macro "Transform" from breaking the template arg.
    World::registerComponentType<Transform>("Transform");
    World::registerComponentType<HealthComponent>("HealthComponent");
    World::registerComponentType<MeshComponent>("MeshComponent");
    World::registerComponentType<SkeletonComponent>("SkeletonComponent");
    World::registerComponentType<AnimationComponent>("AnimationComponent");
    World::registerComponentType<ScriptComponent>("ScriptComponent");
    World::registerComponentType<NetworkComponent>("NetworkComponent");
    World::registerComponentType<RigidBodyComponent>("RigidBodyComponent");
    // P3.1 (2026-08-06) — register new component type for the World query.
    World::registerComponentType<AnimationStateMachineComponent>(
        "AnimationStateMachineComponent");
    // CM-3 (2026-08-11) — 2D lane components.
    World::registerComponentType<TilemapComponent>("TilemapComponent");
    World::registerComponentType<SpriteComponent>("SpriteComponent");
    World::registerComponentType<OrthoCameraComponent>("OrthoCameraComponent");
}

} // namespace ayt::entity
