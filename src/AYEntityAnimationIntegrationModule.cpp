#include <AYEntity/EntityAnimationIntegrationModule.h>

#include <AYEntity/AnimationSystem.h>
#include <AYEntity/BlendSpaceSystem.h>
#include <AYEntity/ComponentRegistration.h>
#include <AYEntity/EntityRuntimeModule.h>
#include <AYEntity/StateMachineSystem.h>
#include <AYEntity/World.h>
#include <AYEntity/components/AnimationComponent.h>
#include <AYEntity/components/AnimationStateMachineComponent.h>
#include <AYEntity/components/BlendSpaceComponent.h>
#include <AYEntity/components/SkeletonComponent.h>

#include <cstring>
#include <string>

namespace ayt::entity
{

AY_FINALIZE_REGISTRATION_METADATA(AdditiveLayerSpec)
AY_FINALIZE_REGISTRATION_METADATA(SkeletonComponent)
AY_FINALIZE_REGISTRATION_METADATA(AnimationComponent)
AY_FINALIZE_REGISTRATION_METADATA(AnimationStateMachineComponent)

namespace
{

bool hasSystemNamed(const World& world, const char* name)
{
    for (size_t i = 0; i < world.systemCount(); ++i) {
        const char* existing = world.getSystemNameAt(i);
        if (existing != nullptr && std::strcmp(existing, name) == 0) {
            return true;
        }
    }
    return false;
}

} // namespace

ComponentRegistryResult registerEntityAnimationComponents(
    ComponentRegistry& registry)
{
    if (auto result = registerSceneComponent<SkeletonComponent>(
            registry, "SkeletonComponent", "Skeleton", "Animation");
        !result) {
        return result;
    }
    if (auto result = registerSceneComponent<AnimationComponent>(
            registry, "AnimationComponent", "Animation", "Animation");
        !result) {
        return result;
    }
    if (auto result = registerComponent<BlendSpaceComponent>(
            registry, "BlendSpaceComponent", "Blend Space", "Animation");
        !result) {
        return result;
    }
    return registerComponent<AnimationStateMachineComponent>(
        registry,
        "AnimationStateMachineComponent",
        "Animation State Machine",
        "Animation");
}

void registerEntityAnimationSystems()
{
    World& world = World::instance();
    if (!hasSystemNamed(world, "AnimationSystem")) {
        registerAnimationSystem();
    }
    if (!hasSystemNamed(world, "BlendSpaceSystem")) {
        registerBlendSpaceSystem();
    }
    if (!hasSystemNamed(world, "StateMachineSystem")) {
        registerStateMachineSystem();
    }
}

EntityAnimationIntegrationModule::EntityAnimationIntegrationModule()
    : _descriptor{
          .id = std::string(kEntityAnimationIntegrationModuleId),
          .displayName = "AYEntity Animation Integration",
          .version = "0.1.0",
          .dependencies = {
              ayt::module::ModuleDependency::required(
                  std::string(kEntityRuntimeModuleId))}}
{
}

const ayt::module::ModuleDescriptor&
EntityAnimationIntegrationModule::descriptor() const noexcept
{
    return _descriptor;
}

ayt::module::ModuleResult
EntityAnimationIntegrationModule::registerTypes(
    ayt::module::IModuleContext& context)
{
    auto* registry = context.findServiceAs<ComponentRegistry>(
        kComponentRegistryModuleService);
    if (registry == nullptr) {
        return ayt::module::ModuleResult::failure(
            ayt::module::ModuleErrorCode::TypeRegistrationFailed,
            "ComponentRegistry service is unavailable for "
            "AYEntity.AnimationIntegration");
    }
    const ComponentRegistryResult result =
        registerEntityAnimationComponents(*registry);
    if (!result) {
        return ayt::module::ModuleResult::failure(
            ayt::module::ModuleErrorCode::TypeRegistrationFailed,
            result.message());
    }
    return ayt::module::ModuleResult::success();
}

ayt::module::ModuleResult EntityAnimationIntegrationModule::install(
    ayt::module::IModuleContext&)
{
    registerEntityAnimationSystems();
    return ayt::module::ModuleResult::success();
}

} // namespace ayt::entity
