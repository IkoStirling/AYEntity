#pragma once

#include <AYEntity/ComponentRegistry.h>
#include <AYModule/IModule.h>

#include <string_view>

namespace ayt::entity
{

inline constexpr std::string_view kEntityAnimationIntegrationModuleId =
    "AYEntity.AnimationIntegration";

class EntityAnimationIntegrationModule final : public ayt::module::IModule
{
public:
    EntityAnimationIntegrationModule();

    [[nodiscard]] const ayt::module::ModuleDescriptor& descriptor()
        const noexcept override;
    [[nodiscard]] ayt::module::ModuleResult registerTypes(
        ayt::module::IModuleContext& context) override;
    [[nodiscard]] ayt::module::ModuleResult install(
        ayt::module::IModuleContext& context) override;

private:
    ayt::module::ModuleDescriptor _descriptor;
};

[[nodiscard]] ComponentRegistryResult registerEntityAnimationComponents(
    ComponentRegistry& registry);
void registerEntityAnimationSystems();

} // namespace ayt::entity
