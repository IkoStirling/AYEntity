#pragma once

#include <AYEntity/ComponentRegistry.h>
#include <AYModule/IModule.h>

#include <string_view>

namespace ayt::entity
{

inline constexpr std::string_view kEntity2DIntegrationModuleId =
    "AYEntity.2DIntegration";

class Entity2DIntegrationModule final : public ayt::module::IModule
{
public:
    Entity2DIntegrationModule();

    [[nodiscard]] const ayt::module::ModuleDescriptor& descriptor()
        const noexcept override;
    [[nodiscard]] ayt::module::ModuleResult registerTypes(
        ayt::module::IModuleContext& context) override;
    [[nodiscard]] ayt::module::ModuleResult install(
        ayt::module::IModuleContext& context) override;
    void shutdown(ayt::module::IModuleContext& context) noexcept override;

private:
    ayt::module::ModuleDescriptor _descriptor;
};

[[nodiscard]] ComponentRegistryResult registerEntity2DComponents(
    ComponentRegistry& registry);
void registerEntity2DSystems();

} // namespace ayt::entity
