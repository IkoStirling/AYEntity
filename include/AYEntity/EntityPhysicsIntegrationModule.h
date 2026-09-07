#pragma once

#include <AYEntity/ComponentRegistry.h>
#include <AYGameLoop/SubSystemModule.h>

#include <string_view>

namespace ayt::entity
{

inline constexpr std::string_view kEntityPhysicsIntegrationModuleId =
    "AYEntity.PhysicsIntegration";

class EntityPhysicsIntegrationModule final
    : public ayt::game::SubSystemModule
{
public:
    EntityPhysicsIntegrationModule();

    [[nodiscard]] ayt::module::ModuleResult registerTypes(
        ayt::module::IModuleContext& context) override;
};

[[nodiscard]] ComponentRegistryResult registerEntityPhysicsComponents(
    ComponentRegistry& registry);
void registerEntityPhysicsIntegrationSubSystem();

} // namespace ayt::entity
