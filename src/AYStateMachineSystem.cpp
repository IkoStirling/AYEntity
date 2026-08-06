// AYStateMachineSystem.cpp — P3.1 (2026-08-06)

#include "AYStateMachineSystem.h"
#include "components/AYAnimationStateMachineComponent.h"
#include "components/AYSkeletonComponent.h"
#include "components/AYAnimationComponent.h"
#include "AYWorld.h"

#include <ayanimation/AnimationPlayer.h>
#include <ayanimation/AnimStateChangedEvent.h>

#include <assetsDefs/IAYAnimation.h>
#include <AYResourceManager.h>

#include <ayevent/EventBus.h>

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

void StateMachineSystem::onUpdate(float /*dt*/) {
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

        // (3) Tick the state machine.
        const std::string prevState = sm.getCurrentStateName();
        sm.update(0.0f);   // P3.1: state machine is event-driven; dt
                           // plumbing happens at the host level. The
                           // bridge still calls update() so cross-fade
                           // transitions can advance their clock when
                           // the host supplies dt — but L1 ships without
                           // that hook (see §5.5 design note).

        // (4) Push current state to AnimationPlayer if state changed.
        if (sm.didTransitionThisFrame() || prevState != sm.getCurrentStateName()) {
            const auto& states = sm.getStates();
            const std::string& newStateName = sm.getCurrentStateName();
            auto it = std::find_if(states.begin(), states.end(),
                [&](const State& s) { return s.name == newStateName; });
            if (it != states.end() && !it->clipPath.empty()) {
                auto clip = ayt::resource::ResourceManager::instance()
                                .load<ayt::resource::IAnimation>(it->clipPath);
                if (clip) {
                    skel->player->play(clip.get());
                    skel->player->setLoop(it->loop);
                    skel->player->setPlayRate(it->playRate);
                }
            }
            ayt::event::EventBus::instance().emit<ayt::anim::AnimStateChangedEvent>(
                ayt::anim::AnimStateChangedEvent{e, prevState, newStateName});
        }

        // (5) Update read-back fields.
        c->currentState    = sm.getCurrentStateName();
        c->previousState   = sm.getPreviousStateName();
        c->isTransitioning = sm.isTransitioning();
    }
}

void registerStateMachineSystem()
{
    // INV-25 — priority 460 (after AnimationSystem 450).
    World::instance().registerSystem<StateMachineSystem>(460);
}

} // namespace ayt::entity