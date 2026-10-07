#pragma once

#include <AYEntity/IEntity.h>

namespace ayt::entity
{

/// Publish Fixed/DetFloat translation and opt-in DetQuaternion rotation to Transform.
/// onUpdate receives alpha, not dt; conflicting authorities/non-finite Det state
/// are skipped before pose writes. Translation uses native presentation arithmetic;
/// Det rotation uses software nlerp. Neither path writes Sim state.
class SimToPresentBridgeSystem final : public ISystem {
public:
    const char* getName() const override { return "SimToPresentBridgeSystem"; }
    void onUpdate(float interpolationAlpha) override;

    static constexpr int kPriority = 0;
};

/// Install the one-way translation/rotation Bridge in the active World (SystemLane::Bridge).
void registerSimToPresentBridgeSystem();

} // namespace ayt::entity
