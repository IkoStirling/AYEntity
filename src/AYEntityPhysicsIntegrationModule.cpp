#include <AYEntity/EntityPhysicsIntegrationModule.h>

#include <AYEntity/ComponentRegistration.h>
#include <AYEntity/EntityPhysicsBridge.h>
#include <AYEntity/EntityRuntimeModule.h>
#include <AYEntity/components/ColliderComponent.h>
#include <AYEntity/components/RigidBodyComponent.h>
#include <AYGameLoop.h>
#include <AYGameLoop/SubSystemRegistry.h>
#include <AYPhysics/PhysicsRuntimeModule.h>

#include <memory>
#include <string>

namespace ayt::entity
{

AY_FINALIZE_REGISTRATION_METADATA(ColliderShapeSpec)
AY_FINALIZE_REGISTRATION_METADATA(ColliderComponent)

namespace
{

inline constexpr const char* kPhysicsBridgeSubSystemName =
    "EntityPhysicsBridge";

class EntityPhysicsIntegrationSubSystem final
    : public ayt::game::ISubSystem
{
public:
    const char* getName() const override
    {
        return kPhysicsBridgeSubSystemName;
    }

    const ayt::game::SubSystemDescriptor& getDescriptor() const override
    {
        static const ayt::game::SubSystemDescriptor descriptor{
            .name = kPhysicsBridgeSubSystemName,
            .dependencies = {},
            .basePriority = 0,
            .timeType = ayt::game::SubSystemDescriptor::TimeType::Scaled,
            .phases =
                ayt::game::phaseBit(ayt::game::FramePhase::FixedPrePhysics)
                | ayt::game::phaseBit(
                    ayt::game::FramePhase::FixedPostPhysics),
            .clock = ayt::game::ClockDomain::Game,
            .initializeAfter = {"Entity", "Physics"},
            .runsAfter = {"Entity"},
            .phasePriority = 0,
            .reads = {"Simulation.World", "Physics.Snapshot"},
            .writes = {"Simulation.World", "Physics.Commands"}};
        return descriptor;
    }

    bool initialize() override { return true; }
    void update(float) override {}
    void fixedUpdate(float) override {}

    void tick(
        ayt::game::FramePhase phase,
        const ayt::game::FrameContext&) override
    {
        if (phase == ayt::game::FramePhase::FixedPrePhysics) {
            _bridge.syncEntityToPhysics();
        } else if (phase == ayt::game::FramePhase::FixedPostPhysics) {
            _bridge.syncPhysicsToEntity();
        }
    }

    void shutdown() override { _bridge.shutdown(); }

private:
    EntityPhysicsBridge _bridge;
};

std::unique_ptr<ayt::game::ISubSystem> createPhysicsBridgeSubSystem()
{
    return std::make_unique<EntityPhysicsIntegrationSubSystem>();
}

} // namespace

ComponentRegistryResult registerEntityPhysicsComponents(
    ComponentRegistry& registry)
{
    if (auto result = registerComponent<RigidBodyComponent>(
            registry, "RigidBodyComponent", "Rigid Body", "Physics");
        !result) {
        return result;
    }
    return registerSceneComponent<ColliderComponent>(
        registry, "ColliderComponent", "Collider", "Physics");
}

EntityPhysicsIntegrationModule::EntityPhysicsIntegrationModule()
    : SubSystemModule(
          ayt::module::ModuleDescriptor{
              .id = std::string(kEntityPhysicsIntegrationModuleId),
              .displayName = "AYEntity Physics Integration",
              .version = "0.1.0",
              .dependencies = {
                  ayt::module::ModuleDependency::required(
                      std::string(kEntityRuntimeModuleId)),
                  ayt::module::ModuleDependency::required(
                      std::string(ayt::physics::kPhysicsRuntimeModuleId))}},
          kPhysicsBridgeSubSystemName,
          []() { return createPhysicsBridgeSubSystem(); })
{
}

ayt::module::ModuleResult EntityPhysicsIntegrationModule::registerTypes(
    ayt::module::IModuleContext& context)
{
    auto* registry = context.findServiceAs<ComponentRegistry>(
        kComponentRegistryModuleService);
    if (registry == nullptr) {
        return ayt::module::ModuleResult::failure(
            ayt::module::ModuleErrorCode::TypeRegistrationFailed,
            "ComponentRegistry service is unavailable for "
            "AYEntity.PhysicsIntegration");
    }
    const ComponentRegistryResult result =
        registerEntityPhysicsComponents(*registry);
    if (!result) {
        return ayt::module::ModuleResult::failure(
            ayt::module::ModuleErrorCode::TypeRegistrationFailed,
            result.message());
    }
    return ayt::module::ModuleResult::success();
}

void registerEntityPhysicsIntegrationSubSystem()
{
    if (ayt::game::SubSystemRegistry::instance().findSubSystem(
            kPhysicsBridgeSubSystemName) != nullptr) {
        return;
    }
    auto subsystem = createPhysicsBridgeSubSystem();
    ayt::game::IGameLoop::instance().registerSubSystem(subsystem.release());
}

} // namespace ayt::entity
