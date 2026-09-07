#pragma once

#include <AYEntity/ComponentRegistry.h>
#include <AYModule/IModule.h>

#include <string_view>

namespace ayt::entity
{

inline constexpr std::string_view kEntityScriptIntegrationModuleId =
    "AYEntity.ScriptIntegration";

class EntityScriptIntegrationModule final : public ayt::module::IModule
{
public:
    EntityScriptIntegrationModule();

    [[nodiscard]] const ayt::module::ModuleDescriptor& descriptor()
        const noexcept override;
    [[nodiscard]] ayt::module::ModuleResult registerTypes(
        ayt::module::IModuleContext& context) override;

private:
    ayt::module::ModuleDescriptor _descriptor;
};

[[nodiscard]] ComponentRegistryResult registerEntityScriptComponents(
    ComponentRegistry& registry);

} // namespace ayt::entity
