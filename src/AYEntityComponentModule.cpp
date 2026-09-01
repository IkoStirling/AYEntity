#include <AYEntity/EntityComponentModule.h>
#include <AYEntity/EntityModule.h>

namespace ayt::entity
{

EntityComponentModule::EntityComponentModule(ComponentRegistry& registry)
    : _registry(registry),
      _descriptor{
          .id = "AYEntity.Components",
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
    (void)context;
    ComponentRegistryResult result = registerEntityComponents(_registry);
    if (!result) {
        return ayt::module::ModuleResult::failure(
            ayt::module::ModuleErrorCode::TypeRegistrationFailed,
            result.message());
    }
    return ayt::module::ModuleResult::success();
}

} // namespace ayt::entity
