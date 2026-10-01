#include <AYEntity/EntityScriptIntegrationModule.h>

#include <AYEntity/ComponentRegistration.h>
#include <AYEntity/EntityComponentModule.h>
#include <AYEntity/components/ScriptComponent.h>
#include <AYEntity/components/ActorInstanceComponent.h>

#include <string>

namespace ayt::entity
{

ComponentRegistryResult registerEntityScriptComponents(
    ComponentRegistry& registry)
{
    auto result = registerComponent<ScriptComponent>(
        registry, "ScriptComponent", "Script", "Scripting");
    if (!result) return result;
    auto actorScript = detail::makeComponentDescriptor<ActorScriptComponent>(
        "ActorScriptComponent", "Actor Script", "Scripting");
    actorScript.editorAddable = false;
    return registry.registerComponent(std::move(actorScript));
}

EntityScriptIntegrationModule::EntityScriptIntegrationModule()
    : _descriptor{
          .id = std::string(kEntityScriptIntegrationModuleId),
          .displayName = "AYEntity Script Integration",
          .version = "0.1.0",
          .dependencies = {
              ayt::module::ModuleDependency::required(
                  std::string(kEntityComponentModuleId))}}
{
}

const ayt::module::ModuleDescriptor&
EntityScriptIntegrationModule::descriptor() const noexcept
{
    return _descriptor;
}

ayt::module::ModuleResult EntityScriptIntegrationModule::registerTypes(
    ayt::module::IModuleContext& context)
{
    auto* registry = context.findServiceAs<ComponentRegistry>(
        kComponentRegistryModuleService);
    if (registry == nullptr) {
        return ayt::module::ModuleResult::failure(
            ayt::module::ModuleErrorCode::TypeRegistrationFailed,
            "ComponentRegistry service is unavailable for "
            "AYEntity.ScriptIntegration");
    }
    const ComponentRegistryResult result =
        registerEntityScriptComponents(*registry);
    if (!result) {
        return ayt::module::ModuleResult::failure(
            ayt::module::ModuleErrorCode::TypeRegistrationFailed,
            result.message());
    }
    return ayt::module::ModuleResult::success();
}

} // namespace ayt::entity
