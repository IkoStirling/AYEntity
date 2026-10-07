#pragma once
#include <AYEntity/IEntity.h>
#include <AYMath/DetFloat.h>

namespace ayt::entity {
/** @brief Sim system receiving software binary32 dt without a native intermediary.
 * @note Register explicitly in SystemLane::Sim. World::fixedUpdate(DetFloat32)
 * calls onDeterministicUpdate directly. The legacy float World tick calls the
 * final onUpdate adapter; its dt is an explicit native-input boundary, so hosts
 * needing exact tick ratios must use the typed tick or construct their own dt.
 */
class IDeterministicSystem : public ISystem {
public:
    /// Implement authoritative logic using DetFloat32/DetVec values only.
    virtual void onDeterministicUpdate(math::DetFloat32 fixedDt) = 0;
    /// Compatibility adapter for existing native fixed-step hosts.
    void onUpdate(float fixedDt) final {
        onDeterministicUpdate(math::DetFloat32::fromFloat(fixedDt));
    }
};
} // namespace ayt::entity
