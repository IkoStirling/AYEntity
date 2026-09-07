#pragma once

#include <AYEntity/IEntity.h>

namespace ayt::entity
{

// Converts deterministic fixed translation into the float presentation
// Transform. onUpdate's argument is interpolation alpha, not delta time.
class SimToPresentBridgeSystem final : public ISystem {
public:
    const char* getName() const override { return "SimToPresentBridgeSystem"; }
    void onUpdate(float interpolationAlpha) override;

    static constexpr int kPriority = 0;
};

void registerSimToPresentBridgeSystem();

} // namespace ayt::entity
