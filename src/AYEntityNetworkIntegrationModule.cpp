#include <AYEntity/EntityNetworkIntegrationModule.h>

#include <AYEntity/ComponentRegistration.h>
#include <AYEntity/EntityComponentModule.h>
#include <AYEntity/components/NetworkComponent.h>

#include <string>

namespace ayt::entity
{

ComponentRegistryResult registerEntityNetworkComponents(
    ComponentRegistry& registry)
{
    return registerComponent<NetworkComponent>(
        registry, "NetworkComponent", "Network", "Networking");
}

EntityNetworkIntegrationModule::EntityNetworkIntegrationModule()
    : _descriptor{
          .id = std::string(kEntityNetworkIntegrationModuleId),
          .displayName = "AYEntity Network Integration",
          .version = "0.1.0",
          .dependencies = {
              ayt::module::ModuleDependency::required(
                  std::string(kEntityComponentModuleId))}}
{
}

const ayt::module::ModuleDescriptor&
EntityNetworkIntegrationModule::descriptor() const noexcept
{
    return _descriptor;
}

ayt::module::ModuleResult EntityNetworkIntegrationModule::registerTypes(
    ayt::module::IModuleContext& context)
{
    auto* registry = context.findServiceAs<ComponentRegistry>(
        kComponentRegistryModuleService);
    if (registry == nullptr) {
        return ayt::module::ModuleResult::failure(
            ayt::module::ModuleErrorCode::TypeRegistrationFailed,
            "ComponentRegistry service is unavailable for "
            "AYEntity.NetworkIntegration");
    }
    const ComponentRegistryResult result =
        registerEntityNetworkComponents(*registry);
    if (!result) {
        return ayt::module::ModuleResult::failure(
            ayt::module::ModuleErrorCode::TypeRegistrationFailed,
            result.message());
    }
    return ayt::module::ModuleResult::success();
}

} // namespace ayt::entity
