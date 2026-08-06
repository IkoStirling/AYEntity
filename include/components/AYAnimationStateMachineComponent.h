// AYAnimationStateMachineComponent.h — P3.1 (2026-08-06)
//
// ECS handle for an entity that owns a state-machine-driven animation
// graph. Mirrors AnimationComponent field shape; pairs with StateMachineSystem
// (priority 460) which drives the AnimationPlayer on transition.
//
// .ayasm loader deferred per §4.14 — see design.md §4.14.7. resourcePath
// is a placeholder; in L1 the StateMachine is built procedurally via
// StateMachineSystem::buildStateMachine or by direct injection (tests).

#pragma once

#include <IAYEntity.h>

#include <cstdint>
#include <string>
#include <vector>

namespace ayt::entity
{

#define AY_CURRENT_CLASS AnimationStateMachineComponent
struct AnimationStateMachineComponent : public IComponent {
    const char* getName() const override { return "AnimationStateMachineComponent"; }

    // State-machine definition (in-memory build by entity setup code).
    // Empty resourcePath ⇒ no resource loader wired (deferred).
    AY_PROPERTY(std::string, resourcePath, kAttrSerialize)

    // === Triggers queued by gameplay code (consumed each tick) ===
    AY_PROPERTY(std::vector<std::string>, pendingTriggers, kAttrSerialize)

    // === Parameters (L1 condition eval) ===
    AY_PROPERTY(float, speed,         kAttrSerialize)
    AY_PROPERTY(float, verticalSpeed, kAttrSerialize)
    AY_PROPERTY(bool,  isGrounded,    kAttrSerialize)
    AY_PROPERTY(bool,  isAttacking,   kAttrSerialize)

    // === Read-back (set by StateMachineSystem each tick) ===
    AY_PROPERTY(std::string, currentState,   kAttrSerialize)
    AY_PROPERTY(std::string, previousState,  kAttrSerialize)
    AY_PROPERTY(bool,        isTransitioning, kAttrSerialize)
    // P3.2 NEW — deepest leaf state name across the active sub-machine
    // hierarchy. Equals currentState when the parent's current state is
    // not a sub-machine entry. StateMachineSystem writes it each tick.
    AY_PROPERTY(std::string, activeSubState,  kAttrSerialize)

    AnimationStateMachineComponent() {
        resourcePath      = "";        // P3.1 — defer .ayasm loader
        speed             = 0.0f;
        verticalSpeed     = 0.0f;
        isGrounded        = true;
        isAttacking       = false;
        currentState      = "";
        previousState     = "";
        isTransitioning   = false;
        activeSubState    = "";        // P3.2 NEW
        // pendingTriggers defaults to empty.
    }

    // Convenience: gameplay code calls this instead of pushing into
    // pendingTriggers manually. StateMachineSystem drains pendingTriggers
    // each tick and forwards to ayt::anim::StateMachine::setTrigger.
    void setTrigger(const std::string& name) {
        pendingTriggers.push_back(name);
    }
};
#undef AY_CURRENT_CLASS

} // namespace ayt::entity