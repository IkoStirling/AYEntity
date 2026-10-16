#pragma once

#include <AYEntity/ComponentRegistry.h>
#include <AYGameLoop/SubSystemModule.h>

#include <string_view>

namespace ayt::physics { class PhysicsManager; }
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
class World;
/// Install the default camera sphere-sweep adapter for this World. Custom queries take precedence.
/// nullptr manager uses the registered PhysicsSubSystem on each probe (safe across teardown).
void installCameraPhysicsCollision(World& world, ayt::physics::PhysicsManager* manager = nullptr);

} // namespace ayt::entity
