#include <AYEntity/EntityParticleIntegrationModule.h>
#include <AYEntity/EntityRuntimeModule.h>
#include <AYEntity/ParticleSystem.h>
#include <AYEntity/World.h>
#include <AYParticle/EffectResource.h>
#include <AYEntity/WorldLifecycle.h>
#include <AYRenderer/RendererRuntimeModule.h>
#include <AYRenderer/RendererSubSystem.h>
namespace ayt::entity {
void registerEntityParticleSystems() {
    particle::registerEffectResourceLoader();
    static int ownerToken=0;
    (void)registerWorldBeforeShutdownCallback(&ownerToken,[](World& world) noexcept {
        if (auto* renderer=render::RendererSubSystem::findRegistered())
            renderer->clearSceneBuildersForOwner(&world);
    });
    auto& world=World::instance();
    if (!world.findSystemByName("ParticleSimulationSystem"))
        world.registerSystem<ParticleSimulationSystem>(ParticleSimulationSystem::kPriority);
    if (!world.findSystemByName("ParticleRenderSystem"))
        world.registerSystem<ParticleRenderSystem>(ParticleRenderSystem::kPriority);
}
EntityParticleIntegrationModule::EntityParticleIntegrationModule()
    : _descriptor{.id=std::string(kEntityParticleIntegrationModuleId),
        .displayName="AYEntity Particle Integration", .version="0.1.0",
        .dependencies={module::ModuleDependency::required(std::string(kEntityRuntimeModuleId)),
            module::ModuleDependency::required(std::string(render::kRendererRuntimeModuleId))}} {}
module::ModuleResult EntityParticleIntegrationModule::registerTypes(module::IModuleContext& context) {
    auto* registry=context.findServiceAs<ComponentRegistry>(kComponentRegistryModuleService);
    if (!registry) return module::ModuleResult::failure(module::ModuleErrorCode::TypeRegistrationFailed,"Particle ComponentRegistry unavailable");
    const auto result=registerEntityParticleComponents(*registry);
    return result ? module::ModuleResult::success() :
        module::ModuleResult::failure(module::ModuleErrorCode::TypeRegistrationFailed,result.message());
}
module::ModuleResult EntityParticleIntegrationModule::install(module::IModuleContext&) {
    registerEntityParticleSystems(); return module::ModuleResult::success();
}
}