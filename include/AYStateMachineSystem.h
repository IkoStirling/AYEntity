// AYStateMachineSystem.h — P3.1 (2026-08-06)
//
// Bridges AnimationStateMachineComponent → ayt::anim::StateMachine →
// AnimationPlayer (via SkeletonComponent). Priority 460 runs AFTER
// AnimationSystem (450), so a transition fires this frame and the new
// clip plays next frame (1-frame latency, L1 simple).
//
// INV-25 — registered at priority 460 via AY_SYSTEM(StateMachineSystem, 460)
// in AYEntityModule.cpp.

#pragma once

#include <IAYEntity.h>

#include <ayanimation/StateMachine.h>

#include <memory>
#include <unordered_map>

namespace ayt::entity
{

class Entity;
class AnimationStateMachineComponent;

class StateMachineSystem : public ISystem {
public:
    const char* getName() const override { return "StateMachineSystem"; }

    void onStart() override;
    void onUpdate(float dt) override;

    // Exposed for tests / debug only. Mirrors AnimationSystem::kPriority.
    static constexpr int kPriority = 460;

    // === Static authoring helper ===
    // Reads resourcePath / params / triggers from the component and
    // fills `out` with a freshly constructed state machine. For L1
    // resourcePath is a placeholder; tests bypass this and construct
    // StateMachine directly via addState / addTransition.
    static void buildStateMachine(const AnimationStateMachineComponent& c,
                                  ayt::anim::StateMachine& out);

    // Test/inspector entry: per-entity StateMachine cache (P3.1 ships
    // without editor integration; tests use this to inject a
    // pre-built StateMachine so they don't need buildStateMachine).
    ayt::anim::StateMachine* getOrCreateMachine(Entity* e);

private:
    std::unordered_map<Entity*, std::unique_ptr<ayt::anim::StateMachine>> _machines;
};

} // namespace ayt::entity