#pragma once

#include <AYEntity/ComponentRegistry.h>
#include <AYModule/IModule.h>

#include <string_view>

namespace ayt::entity
{

inline constexpr std::string_view kEntityNetworkIntegrationModuleId =
    "AYEntity.NetworkIntegration";

class EntityNetworkIntegrationModule final : public ayt::module::IModule
{
public:
    EntityNetworkIntegrationModule();

    [[nodiscard]] const ayt::module::ModuleDescriptor& descriptor()
        const noexcept override;
    [[nodiscard]] ayt::module::ModuleResult registerTypes(
        ayt::module::IModuleContext& context) override;

private:
    ayt::module::ModuleDescriptor _descriptor;
};

[[nodiscard]] ComponentRegistryResult registerEntityNetworkComponents(
    ComponentRegistry& registry);

} // namespace ayt::entity
