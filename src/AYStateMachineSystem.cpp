// AYStateMachineSystem.cpp — P3.1 (2026-08-06) + P3.2 (2026-08-06) L3
// 子状态机 ECS bridge.
//
// P3.2 changes:
//   * dt plumbing — `sm.update(0.0f)` (P3.1 stub) → `sm.update(dt)` so
//     cross-fade clocks advance properly. P3.1 §4.14.5 deferred this
//     to P3.2.
//   * Sub-machine entry semantics — when a transition fires into a
//     state with `isSubMachine=true`, the ECS bridge MUST NOT call
//     `player.play()`; the child sub-machine drives its own clip via
//     its own transitions (INV-27).
//   * activeSubState read-back — written each frame from
//     `sm.getActiveLeafStateName()` for inspector / debug.
//
// P3.x刀 N+1.BC (2026-08-07) — Time-in-State Query + Per-State AnimNotify Routing:
//   * Per-state notify routing — bridge pushes the current SM state name
//     to the AnimationPlayer via `setCurrentStateName()` whenever a
//     transition fires (or the state changes). The player caches this
//     in `_currentStateNameForNotify` and writes it into every
//     AnimNotifyRecord::fromStateName when the next tick fires a
//     notify. INV-40..42 contracts honored.

#include "AYEntity/StateMachineSystem.h"
#include "AYEntity/components/AnimationStateMachineComponent.h"
#include "AYEntity/components/SkeletonComponent.h"
#include "AYEntity/components/AnimationComponent.h"
#include "AYEntity/World.h"

#include <AYAnimation/AnimationPlayer.h>
#include <AYAnimation/AnimStateChangedEvent.h>

#include <AYResource/assetsDefs/IAnimation.h>
#include <AYResource/ResourceManager.h>

#include <AYEventSystem/EventBus.h>

#include <AYEntity.h>

namespace ayt::entity
{

using ayt::anim::StateMachine;
using ayt::anim::State;
using ayt::anim::Transition;

void StateMachineSystem::onStart() {
    _machines.clear();
}

StateMachine* StateMachineSystem::getOrCreateMachine(Entity* e) {
    auto it = _machines.find(e);
    if (it != _machines.end()) {
        return it->second.get();
    }
    auto machine = std::make_unique<StateMachine>();
    auto* raw = machine.get();
    _machines.emplace(e, std::move(machine));
    return raw;
}

void StateMachineSystem::buildStateMachine(
    const AnimationStateMachineComponent& /*c*/,
    StateMachine& out)
{
    // L1 ships without a .ayasm loader (resourcePath is a placeholder).
    // Tests use getOrCreateMachine + direct addState/addTransition API.
    // This helper is a no-op stub that satisfies the API contract; the
    // entity-setup code path that builds real graphs ships in P4.x
    // alongside the .ayasm loader.
    out.clear();
}

void StateMachineSystem::onUpdate(float dt) {
    auto& world = World::instance();

    // Walk every entity with AnimationStateMachineComponent + SkeletonComponent.
    // Pattern mirrors AYAnimationSystem / AYBlendSpaceSystem (use world.query).
    for (Entity* e : world.query<AnimationStateMachineComponent, SkeletonComponent>()) {
        auto* c = e->getComponent<AnimationStateMachineComponent>();
        if (c == nullptr) continue;
        auto* skel = e->getComponent<SkeletonComponent>();
        if (skel == nullptr || skel->player == nullptr) continue;

        // (1) Lazily create the per-entity StateMachine. Tests bypass
        //     buildStateMachine by injecting via getOrCreateMachine.
        StateMachine* smPtr = getOrCreateMachine(e);
        StateMachine& sm = *smPtr;
        if (sm.getStateCount() == 0) {
            buildStateMachine(*c, sm);
        }
        if (sm.getStateCount() == 0) {
            // No graph wired — entity has no animation pipeline.
            continue;
        }

        // (2) Sync params + triggers from component.
        sm.setParam("Speed",         c->speed);
        sm.setParam("VerticalSpeed", c->verticalSpeed);
        sm.setParam("IsGrounded",    c->isGrounded    ? 1.0f : 0.0f);
        sm.setParam("IsAttacking",   c->isAttacking   ? 1.0f : 0.0f);
        for (const auto& trig : c->pendingTriggers) {
            sm.setTrigger(trig);
        }
        c->pendingTriggers.clear();

        // (3) Tick the state machine — P3.2: pass real dt (P3.1 stubbed 0.0f).
        const std::string prevState = sm.getCurrentStateName();
        sm.update(dt);

        // (4) Push current state to AnimationPlayer if state changed AND
        //     the new state is NOT a sub-machine entry (INV-27: sub-
        //     machine entries are owned by the child SM; the ECS bridge
        //     MUST NOT call player.play() for them — child SM drives).
        // P3.x刀 N+1.C NEW — Per-state AnimNotify routing. Push the
        // active state name to the player EVERY tick (cheap std::string
        // move) so the player's cache is observable after wire-up even
        // before the first transition. The player writes this into every
        // AnimNotifyRecord::fromStateName on the next tick that fires a
        // notify. The bridge push keeps SM and Player decoupled (P3.x刀
        // N+1 lesson: composition pattern, no direct SM→Player dep).
        skel->player->setCurrentStateName(sm.getCurrentStateName());

        if (sm.didTransitionThisFrame() || prevState != sm.getCurrentStateName()) {
            const auto& states = sm.getStates();
            const std::string& newStateName = sm.getCurrentStateName();
            auto it = std::find_if(states.begin(), states.end(),
                [&](const State& s) { return s.name == newStateName; });
            if (it != states.end()) {
                if (it->isSubMachine) {
                    // P3.2 NEW — child SM owns clip selection; skip player.play.
                } else if (!it->clipPath.empty()) {
                    auto clip = ayt::resource::ResourceManager::instance()
                                    .load<ayt::resource::IAnimation>(it->clipPath);
                    if (clip) {
                        skel->player->play(clip.get());
                        skel->player->setLoop(it->loop);
                        skel->player->setPlayRate(it->playRate);
                    }
                }
            }
            ayt::event::EventBus::instance().emit<ayt::anim::AnimStateChangedEvent>(
                ayt::anim::AnimStateChangedEvent{e, prevState, newStateName});
        }

        // (5) Update read-back fields — P3.2 NEW: include activeSubState.
        c->currentState    = sm.getCurrentStateName();
        c->previousState   = sm.getPreviousStateName();
        c->isTransitioning = sm.isTransitioning();
        c->activeSubState  = sm.getActiveLeafStateName();
    }
}

void registerStateMachineSystem()
{
    // INV-25 — priority 460 (after AnimationSystem 450).
    World::instance().registerSystem<StateMachineSystem>(460);
}

} // namespace ayt::entity