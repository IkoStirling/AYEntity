#pragma once
#include <AYGameLoop/IGameLoop.h>
#include <memory>
#include <optional>

namespace ayt::entity {
class World;
/** @brief Optional standard Entity subsystem simulation policy, owned by its binding.
 * @note nullopt explicitly selects ordinary World Sim; false stops the fixed phase.
 * Core has no dependency on a concrete session, Scene, Host or replay implementation.
 * Installation/removal is owner-thread, at an external frame boundary only.
 */
class IEntitySimulationDriver {
public:
    virtual ~IEntitySimulationDriver() = default;
    virtual std::optional<bool> fixedTick(World&, const game::FrameContext&) = 0;
    virtual bool presentationBoundary(World&) { return true; }
    /// A waiting network owner may hold the latest committed pose instead of rewinding history.
    virtual float presentationAlpha(float alpha) const { return alpha; }
    virtual void hostShutdown() noexcept = 0;
    virtual void worldShutdown(World&) noexcept = 0;
};

/// Attach once to the standard Entity subsystem; reject unknown/occupied adapters.
bool attachEntitySimulationDriver(game::ISubSystem&, std::shared_ptr<IEntitySimulationDriver>);
/// Remove only the exact attached driver; stale owners cannot disconnect a replacement.
bool detachEntitySimulationDriver(game::ISubSystem&, const IEntitySimulationDriver*) noexcept;
} // namespace ayt::entity
