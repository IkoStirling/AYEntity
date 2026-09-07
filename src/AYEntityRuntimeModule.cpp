#include <AYEntity/EntityRuntimeModule.h>

#include <AYEntity/EntityComponentModule.h>
#include <AYEntity/EntityModule.h>

#include <string>

namespace ayt::entity
{

EntityRuntimeModule::EntityRuntimeModule()
    : SubSystemModule(
          ayt::module::ModuleDescriptor{
              .id = std::string(kEntityRuntimeModuleId),
              .displayName = "AYEntity Runtime",
              .version = "0.1.0",
              .dependencies = {
                  ayt::module::ModuleDependency::required(
                      std::string(kEntityComponentModuleId))}},
          "Entity",
          []() {
              return createEntitySubSystem();
          },
          [](
              ayt::module::IModuleContext&,
              ayt::game::ISubSystem&) {
              registerEntityCoreSystems();
              return ayt::module::ModuleResult::success();
          })
{
}

} // namespace ayt::entity
