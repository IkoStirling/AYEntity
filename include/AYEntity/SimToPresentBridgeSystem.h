#pragma once

#include <AYEntity/IEntity.h>

namespace ayt::entity
{

/// Convert Fixed or DetFloat translation into presentation Transform position.
/// onUpdate receives alpha, not dt; conflicting authorities/non-finite Det state
/// are skipped. Native presentation interpolation does not write Sim state.
class SimToPresentBridgeSystem final : public ISystem {
public:
    const char* getName() const override { return "SimToPresentBridgeSystem"; }
    void onUpdate(float interpolationAlpha) override;

    static constexpr int kPriority = 0;
};

/// Install the core translation Bridge in the active World (SystemLane::Bridge).
void registerSimToPresentBridgeSystem();

} // namespace ayt::entity
