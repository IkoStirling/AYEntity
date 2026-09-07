// AYEntityReflection.cpp - AYEntityCore reflection + component registration.
//
// AY_FINALIZE_REGISTRATION_METADATA must live here only (not in component
// headers). Headers are included from many TUs via AYEntity.h; duplicate
// static finalizers corrupt the CRT debug heap.

#include "AYEntity/EntityModule.h"
#include "AYEntity/ComponentRegistration.h"

#include "AYEntity/components/HealthComponent.h"
#include "AYEntity/components/SimTransformComponent.h"
#include "AYEntity/components/TransformComponent.h"

namespace ayt::entity
{

// Reflect metadata (one static initializer per type, this TU only).
AY_FINALIZE_REGISTRATION_METADATA(Transform)
AY_FINALIZE_REGISTRATION_METADATA(HealthComponent)

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
    AYT_REGISTER_COMPONENT(registerSceneComponent<HealthComponent>(
        registry, "HealthComponent", "Health", "Gameplay"));
#undef AYT_REGISTER_COMPONENT
    return ComponentRegistryResult::success();
}

} // namespace ayt::entity
