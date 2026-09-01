#pragma once

#include <AYEntity/ComponentRegistry.h>
#include <AYModule/IModule.h>

namespace ayt::entity
{

// First AYModule adoption slice: component metadata only. GameLoop subsystem
// and presentation-system ownership deliberately remain on the legacy
// bootstrap path until their teardown contract is migrated separately.
class EntityComponentModule final : public ayt::module::IModule
{
public:
    explicit EntityComponentModule(ComponentRegistry& registry);

    [[nodiscard]] const ayt::module::ModuleDescriptor& descriptor()
        const noexcept override;
    [[nodiscard]] ayt::module::ModuleResult registerTypes(
        ayt::module::IModuleContext& context) override;

private:
    ComponentRegistry& _registry;
    ayt::module::ModuleDescriptor _descriptor;
};

} // namespace ayt::entity
