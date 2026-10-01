#pragma once
#include <AYEntity/ComponentRegistry.h>
#include <AYModule/IModule.h>
#include <string_view>
namespace ayt::entity {
inline constexpr std::string_view kEntityParticleIntegrationModuleId="AYEntity.ParticleIntegration";
ComponentRegistryResult registerEntityParticleComponents(ComponentRegistry& registry);
/// Install once in each active World after the renderer is registered.
void registerEntityParticleSystems();
class EntityParticleIntegrationModule final : public ayt::module::IModule {
public:
    EntityParticleIntegrationModule();
    const ayt::module::ModuleDescriptor& descriptor() const noexcept override { return _descriptor; }
    ayt::module::ModuleResult registerTypes(ayt::module::IModuleContext& context) override;
    ayt::module::ModuleResult install(ayt::module::IModuleContext&) override;
private:
    ayt::module::ModuleDescriptor _descriptor;
};
}