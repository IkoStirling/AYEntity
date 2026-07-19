// AYEntityReflection.cpp - single-TU reflect + World component registration.
//
// AY_FINALIZE_REGISTRATION_METADATA must live here only (not in component
// headers). Headers are included from many TUs via AYEntity.h; duplicate
// static finalizers corrupt the CRT debug heap.

#include "AYEntityModule.h"
#include "AYWorld.h"

#include "components/AYAnimationComponent.h"
#include "components/AYHealthComponent.h"
#include "components/AYMeshComponent.h"
#include "components/AYNetworkComponent.h"
#include "components/AYRigidBodyComponent.h"
#include "components/AYScriptComponent.h"
#include "components/AYSkeletonComponent.h"
#include "components/AYTransformComponent.h"

namespace ayt::entity
{

// Reflect metadata (one static initializer per type, this TU only).
AY_FINALIZE_REGISTRATION_METADATA(Transform)
AY_FINALIZE_REGISTRATION_METADATA(HealthComponent)
AY_FINALIZE_REGISTRATION_METADATA(MeshComponent)
AY_FINALIZE_REGISTRATION_METADATA(SkeletonComponent)
AY_FINALIZE_REGISTRATION_METADATA(AnimationComponent)

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
}

} // namespace ayt::entity
