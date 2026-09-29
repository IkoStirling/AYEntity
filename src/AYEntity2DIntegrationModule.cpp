#include <AYEntity/Entity2DIntegrationModule.h>

#include <AYEntity/EntityRuntimeModule.h>
#include <AYEntity/OrthoCameraUpdateSystem.h>
#include <AYEntity/SpriteAnimationSystem.h>
#include <AYEntity/SpriteRenderSystem.h>
#include <AYEntity/TilemapAnimationTickSystem.h>
#include <AYEntity/TilemapRenderSystem.h>
#include <AYEntity/TilemapStreamingSystem.h>
#include <AYEntity/World.h>
#include <AYEntity/WorldLifecycle.h>
#include <AYRenderer/RendererRuntimeModule.h>
#include <AYRenderer/RendererSubSystem.h>

#include <cstring>
#include <string>

namespace ayt::entity
{

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

void registerEntity2DSystems()
{
    ensureWorldLifecycleBridge();
    World& world = World::instance();
    if (!hasSystemNamed(world, "OrthoCameraUpdateSystem")) {
        registerOrthoCameraUpdateSystem();
    }
    if (!hasSystemNamed(world, "TilemapStreamingSystem")) {
        registerTilemapStreamingSystem();
    }
    if (!hasSystemNamed(world, "TilemapAnimationTickSystem")) {
        registerTilemapAnimationTickSystem();
    }
    if (!hasSystemNamed(world, "SpriteAnimationSystem")) {
        registerSpriteAnimationSystem();
    }
    if (!hasSystemNamed(world, "TilemapRenderSystem")) {
        registerTilemapRenderSystem();
    }
    if (!hasSystemNamed(world, "SpriteRenderSystem")) {
        registerSpriteRenderSystem();
    }
}

Entity2DIntegrationModule::Entity2DIntegrationModule()
    : _descriptor{
          .id = std::string(kEntity2DIntegrationModuleId),
          .displayName = "AYEntity 2D Integration",
          .version = "0.1.0",
          .dependencies = {
              ayt::module::ModuleDependency::required(
                  std::string(kEntityRuntimeModuleId)),
              ayt::module::ModuleDependency::required(
                  std::string(ayt::render::kRendererRuntimeModuleId))}}
{
}

const ayt::module::ModuleDescriptor&
Entity2DIntegrationModule::descriptor() const noexcept
{
    return _descriptor;
}

ayt::module::ModuleResult Entity2DIntegrationModule::registerTypes(
    ayt::module::IModuleContext& context)
{
    auto* registry = context.findServiceAs<ComponentRegistry>(
        kComponentRegistryModuleService);
    if (registry == nullptr) {
        return ayt::module::ModuleResult::failure(
            ayt::module::ModuleErrorCode::TypeRegistrationFailed,
            "ComponentRegistry service is unavailable for "
            "AYEntity.2DIntegration");
    }
    const ComponentRegistryResult result = registerEntity2DComponents(*registry);
    if (!result) {
        return ayt::module::ModuleResult::failure(
            ayt::module::ModuleErrorCode::TypeRegistrationFailed,
            result.message());
    }
    return ayt::module::ModuleResult::success();
}

ayt::module::ModuleResult Entity2DIntegrationModule::install(
    ayt::module::IModuleContext&)
{
    registerEntity2DSystems();
    return ayt::module::ModuleResult::success();
}

void Entity2DIntegrationModule::shutdown(
    ayt::module::IModuleContext&) noexcept
{
}

} // namespace ayt::entity
