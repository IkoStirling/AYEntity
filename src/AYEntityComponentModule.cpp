#include <AYEntity/EntityComponentModule.h>
#include <AYEntity/EntityModule.h>

namespace ayt::entity
{

EntityComponentModule::EntityComponentModule()
    : _descriptor{
          .id = std::string(kEntityComponentModuleId),
          .displayName = "AYEntity Components",
          .version = "0.1.0",
          .dependencies = {}}
{
}

const ayt::module::ModuleDescriptor& EntityComponentModule::descriptor()
    const noexcept
{
    return _descriptor;
}

ayt::module::ModuleResult EntityComponentModule::registerTypes(
    ayt::module::IModuleContext& context)
{
    auto* registry = context.findServiceAs<ComponentRegistry>(
        kComponentRegistryModuleService);
    if (registry == nullptr) {
        return ayt::module::ModuleResult::failure(
            ayt::module::ModuleErrorCode::TypeRegistrationFailed,
            "ComponentRegistry service is unavailable for AYEntity.Components");
    }

    ComponentRegistryResult result = registerEntityCoreComponents(*registry);
    if (!result) {
        return ayt::module::ModuleResult::failure(
            ayt::module::ModuleErrorCode::TypeRegistrationFailed,
            result.message());
    }
    return ayt::module::ModuleResult::success();
}

} // namespace ayt::entity
