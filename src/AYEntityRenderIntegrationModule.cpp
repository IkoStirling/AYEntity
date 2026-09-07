#include <AYEntity/EntityRenderIntegrationModule.h>

#include <AYEntity/ComponentRegistration.h>
#include <AYEntity/EntityAnimationIntegrationModule.h>
#include <AYEntity/EntityRuntimeModule.h>
#include <AYEntity/RenderSystem.h>
#include <AYEntity/SkinnedMeshRenderSystem.h>
#include <AYEntity/World.h>
#include <AYEntity/WorldLifecycle.h>
#include <AYEntity/components/MeshComponent.h>
#include <AYRenderer/RendererRuntimeModule.h>
#include <AYRenderer/RendererSubSystem.h>

#include <cstring>
#include <string>

namespace ayt::entity
{

AY_FINALIZE_REGISTRATION_METADATA(MeshComponent)

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

void clearWorldRenderCallbacks(World& world) noexcept
{
    if (auto* renderer = ayt::render::RendererSubSystem::findRegistered()) {
        renderer->clearSceneBuildersForOwner(&world);
    }
}

void ensureWorldLifecycleBridge()
{
    static int ownerToken = 0;
    (void)registerWorldBeforeShutdownCallback(
        &ownerToken, &clearWorldRenderCallbacks);
}

} // namespace

ComponentRegistryResult registerEntityRenderComponents(
    ComponentRegistry& registry)
{
    return registerSceneComponent<MeshComponent>(
        registry, "MeshComponent", "Mesh", "Rendering");
}

void registerEntityRenderSystems()
{
    ensureWorldLifecycleBridge();
    World& world = World::instance();
    if (!hasSystemNamed(world, "SkinnedMeshRenderSystem")) {
        registerSkinnedMeshRenderSystem();
    }
    if (!hasSystemNamed(world, "RenderSystem")) {
        registerRenderSystem();
    }
}

EntityRenderIntegrationModule::EntityRenderIntegrationModule()
    : _descriptor{
          .id = std::string(kEntityRenderIntegrationModuleId),
          .displayName = "AYEntity Render Integration",
          .version = "0.1.0",
          .dependencies = {
              ayt::module::ModuleDependency::required(
                  std::string(kEntityRuntimeModuleId)),
              ayt::module::ModuleDependency::required(
                  std::string(kEntityAnimationIntegrationModuleId)),
              ayt::module::ModuleDependency::required(
                  std::string(ayt::render::kRendererRuntimeModuleId))}}
{
}

const ayt::module::ModuleDescriptor&
EntityRenderIntegrationModule::descriptor() const noexcept
{
    return _descriptor;
}

ayt::module::ModuleResult EntityRenderIntegrationModule::registerTypes(
    ayt::module::IModuleContext& context)
{
    auto* registry = context.findServiceAs<ComponentRegistry>(
        kComponentRegistryModuleService);
    if (registry == nullptr) {
        return ayt::module::ModuleResult::failure(
            ayt::module::ModuleErrorCode::TypeRegistrationFailed,
            "ComponentRegistry service is unavailable for "
            "AYEntity.RenderIntegration");
    }
    const ComponentRegistryResult result =
        registerEntityRenderComponents(*registry);
    if (!result) {
        return ayt::module::ModuleResult::failure(
            ayt::module::ModuleErrorCode::TypeRegistrationFailed,
            result.message());
    }
    return ayt::module::ModuleResult::success();
}

ayt::module::ModuleResult EntityRenderIntegrationModule::install(
    ayt::module::IModuleContext&)
{
    registerEntityRenderSystems();
    return ayt::module::ModuleResult::success();
}

void EntityRenderIntegrationModule::shutdown(
    ayt::module::IModuleContext&) noexcept
{
    // The process-lifetime callback is code-only and resolves Renderer at
    // invocation time. It remains valid for legacy bootstrap users too.
}

} // namespace ayt::entity
