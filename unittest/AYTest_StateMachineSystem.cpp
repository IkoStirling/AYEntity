// AYTest_StateMachineSystem.cpp — P3.1 (2026-08-06) state machine
// ECS integration tests.
//
// 8+ cases pinning the bridge contract:
//   * AnimationStateMachineComponent → StateMachineSystem → AnimationPlayer
//   * System priority 460 (after AnimationSystem 450)
//   * AnimStateChangedEvent dispatched on transition
//   * State machine built procedurally via direct injection (no .ayasm loader)
//
// Cleanup contract: every test ends with destroyEntity + shutdown
// (mirrors AYTest_SkeletonMaskBridge.cpp).

#include <AYEntity.h>
#include <AYEntityModule.h>
#include <AYWorld.h>

#include <AYAnimationSystem.h>
#include <AYStateMachineSystem.h>
#include <components/AYAnimationComponent.h>
#include <components/AYAnimationStateMachineComponent.h>
#include <components/AYSkeletonComponent.h>
#include <components/AYTransformComponent.h>

#include <ayanimation/AnimationPlayer.h>
#include <ayanimation/StateMachine.h>
#include <ayanimation/AnimStateChangedEvent.h>

#include <assetsImpl/AYSkeleton.h>
#include <aymath/MathTypes.h>

#include <AYTest.h>
#include <AYResourceManager.h>
#include <ayevent/EventBus.h>

#include <cstdio>
#include <memory>
#include <string>

using namespace ayt::entity;

namespace
{

std::shared_ptr<ayt::resource::Skeleton> makeOneBoneSkeletonShared()
{
    using namespace ayt::math;
    auto s = std::make_shared<ayt::resource::Skeleton>();
    s->setBoneCount(1);
    ayt::resource::Bone root;
    root.name              = "Bone0";
    root.parentIndex       = -1;
    root.localPosition     = FVector3(0, 0, 0);
    root.localRotation     = FQuaternion::identity();
    root.localScale        = FVector3(1, 1, 1);
    root.inverseBindMatrix = Float4x4::identity();
    s->setBone(0, root);
    return s;
}

// Build a 1-bone skeleton + AnimationPlayer, but NO AnimationComponent
// (state machine drives clip selection). Used by every test below.
Entity* makeStateMachineEntity(World& world)
{
    world.shutdown();
    world.initialize();
    Entity* e = world.createEntity();
    if (e == nullptr) return nullptr;
    e->addComponent<Transform>();
    auto* skel = e->addComponent<SkeletonComponent>();
    auto* smc  = e->addComponent<AnimationStateMachineComponent>();

    skel->skeleton = makeOneBoneSkeletonShared();
    skel->jointCount = static_cast<uint32_t>(skel->skeleton->getBoneCount());
    skel->skinMatrices = new ayt::math::Float4x4[skel->jointCount];
    {
        const ayt::math::Float4x4 id = ayt::math::Float4x4::identity();
        for (uint32_t i = 0; i < skel->jointCount; ++i) {
            std::memcpy(&skel->skinMatrices[i], &id, sizeof(id));
        }
    }
    skel->loaded = true;
    skel->player = ayt::anim::AnimationPlayer::create();
    skel->player->setSkeleton(skel->skeleton);

    smc->speed         = 0.0f;
    smc->verticalSpeed = 0.0f;
    smc->isGrounded    = true;
    smc->isAttacking   = false;
    smc->currentState  = "";
    smc->previousState = "";
    return e;
}

// Build a simple Idle → Run state machine (idle.ayanm and run.ayanm paths
// only — no ResourceManager load needed because the test fixtures don't
// actually drive player.play).
void populateIdleRunGraph(ayt::anim::StateMachine& sm)
{
    using ayt::anim::State;
    using ayt::anim::Transition;

    State idle;
    idle.name     = "Idle";
    idle.clipPath = "idle.ayanm";
    idle.loop     = true;
    sm.addState(idle);

    State run;
    run.name     = "Run";
    run.clipPath = "run.ayanm";
    run.loop     = true;
    sm.addState(run);

    Transition idleToRun;
    idleToRun.fromState = "Idle";
    idleToRun.toState   = "Run";
    idleToRun.trigger   = "Run";
    sm.addTransition(idleToRun);

    Transition runToIdle;
    runToIdle.fromState = "Run";
    runToIdle.toState   = "Idle";
    runToIdle.trigger   = "Idle";
    sm.addTransition(runToIdle);

    sm.setInitialState("Idle");
}

void teardown(World& world, Entity* e)
{
    if (e != nullptr) {
        world.destroyEntity(e);
    }
}

} // namespace

TEST_SUITE(StateMachineSystemTests)

    // ─── #1 — system priority 460 (after AnimationSystem 450). ────────
    TEST_CASE(sm_system_priority_460_after_animation_system_450) {
        World& world = World::instance();
        world.shutdown();
        world.initialize();

        CHECK(AnimationSystem::kPriority    == 450);
        CHECK(StateMachineSystem::kPriority == 460);
        CHECK(StateMachineSystem::kPriority > AnimationSystem::kPriority);

        // Sanity: confirm via world introspection that the registered
        // systems report the expected priorities.
        ayt::entity::bootstrapModule();
        CHECK(world.systemCount() >= 2);
        bool sawAnim = false, sawSm = false;
        for (size_t i = 0; i < world.systemCount(); ++i) {
            const char* n = world.getSystemNameAt(i);
            const int32_t p = world.getSystemPriorityAt(i);
            if (n != nullptr && std::strcmp(n, "AnimationSystem") == 0) {
                CHECK(p == 450);
                sawAnim = true;
            } else if (n != nullptr && std::strcmp(n, "StateMachineSystem") == 0) {
                CHECK(p == 460);
                sawSm = true;
            }
        }
        CHECK(sawAnim);
        CHECK(sawSm);

        world.shutdown();
    }

    // ─── #2 — initial state populated after first tick. ──────────────
    TEST_CASE(sm_system_init_runs_first_update_with_initial_state) {
        World& world = World::instance();
        Entity* e = makeStateMachineEntity(world);
        CHECK(e != nullptr);

        StateMachineSystem sm_system;
        auto* smc = e->getComponent<AnimationStateMachineComponent>();
        // Inject state graph (no .ayasm loader yet).
        auto* smPtr = sm_system.getOrCreateMachine(e);
        populateIdleRunGraph(*smPtr);

        sm_system.onUpdate(0.016f);
        // After first tick + lazy-init, currentState is "Idle".
        CHECK(smc->currentState == "Idle");
        CHECK(smc->previousState == "Idle");

        teardown(world, e);
    }

    // ─── #3 — setTrigger("Run") fires Idle → Run transition. ─────────
    TEST_CASE(sm_system_trigger_pushes_to_state_machine) {
        World& world = World::instance();
        Entity* e = makeStateMachineEntity(world);
        CHECK(e != nullptr);

        StateMachineSystem sm_system;
        auto* smc = e->getComponent<AnimationStateMachineComponent>();
        auto* smPtr = sm_system.getOrCreateMachine(e);
        populateIdleRunGraph(*smPtr);

        smc->setTrigger("Run");
        sm_system.onUpdate(0.016f);

        CHECK(smc->currentState == "Run");
        CHECK(smc->previousState == "Idle");

        teardown(world, e);
    }

    // ─── #4 — speed parameter doesn't auto-fire without transition. ──
    TEST_CASE(sm_system_no_transition_when_no_trigger_set) {
        World& world = World::instance();
        Entity* e = makeStateMachineEntity(world);
        CHECK(e != nullptr);

        StateMachineSystem sm_system;
        auto* smc = e->getComponent<AnimationStateMachineComponent>();
        auto* smPtr = sm_system.getOrCreateMachine(e);
        populateIdleRunGraph(*smPtr);

        // No trigger set; stay in Idle across multiple ticks.
        smc->speed = 7.0f;
        sm_system.onUpdate(0.016f);
        CHECK(smc->currentState == "Idle");
        sm_system.onUpdate(0.016f);
        CHECK(smc->currentState == "Idle");

        teardown(world, e);
    }

    // ─── #5 — pendingTriggers cleared each tick. ─────────────────────
    TEST_CASE(sm_system_pending_triggers_cleared_after_tick) {
        World& world = World::instance();
        Entity* e = makeStateMachineEntity(world);
        CHECK(e != nullptr);

        StateMachineSystem sm_system;
        auto* smc = e->getComponent<AnimationStateMachineComponent>();
        auto* smPtr = sm_system.getOrCreateMachine(e);
        populateIdleRunGraph(*smPtr);

        smc->setTrigger("Run");
        CHECK(smc->pendingTriggers.size() == 1u);
        sm_system.onUpdate(0.016f);
        CHECK(smc->pendingTriggers.empty());
        // Subsequent ticks don't fire the same trigger again (UE rule).
        sm_system.onUpdate(0.016f);
        CHECK(smc->currentState == "Run");

        teardown(world, e);
    }

    // ─── #6 — AnimStateChangedEvent emitted on transition. ────────────
    TEST_CASE(sm_system_emit_state_changed_event) {
        World& world = World::instance();
        Entity* e = makeStateMachineEntity(world);
        CHECK(e != nullptr);

        StateMachineSystem sm_system;
        auto* smc = e->getComponent<AnimationStateMachineComponent>();
        auto* smPtr = sm_system.getOrCreateMachine(e);
        populateIdleRunGraph(*smPtr);

        ayt::anim::AnimStateChangedEvent captured{e, "", ""};
        bool fired = false;
        auto subId = ayt::event::EventBus::instance().subscribe<ayt::anim::AnimStateChangedEvent>(
            [&](const ayt::anim::AnimStateChangedEvent& ev) {
                captured = ev;
                fired = true;
            });

        smc->setTrigger("Run");
        sm_system.onUpdate(0.016f);

        CHECK(fired == true);
        CHECK(captured.previousState == "Idle");
        CHECK(captured.currentState  == "Run");
        CHECK(captured.entity == e);

        ayt::event::EventBus::instance().unsubscribe(subId);
        teardown(world, e);
    }

    // ─── #7 — entity without AnimationPlayer: no crash. ──────────────
    TEST_CASE(sm_system_entity_without_player_no_crash) {
        World& world = World::instance();
        world.shutdown();
        world.initialize();
        Entity* e = world.createEntity();
        e->addComponent<Transform>();
        auto* skel = e->addComponent<SkeletonComponent>();
        // Note: no AnimationPlayer, no skeleton asset bound.
        skel->loaded = false;
        skel->player = nullptr;
        e->addComponent<AnimationStateMachineComponent>();

        StateMachineSystem sm_system;
        // Should not crash even though the entity has no player.
        sm_system.onUpdate(0.016f);

        teardown(world, e);
    }

    // ─── #8 — multiple entities with independent state machines. ─────
    // NB: don't call makeStateMachineEntity twice — each call does
    // world.shutdown(); world.initialize();, which invalidates the
    // first entity. Build both in a single shutdown+initialize pass.
    TEST_CASE(sm_system_multiple_entities_independent) {
        World& world = World::instance();
        world.shutdown();
        world.initialize();
        Entity* a = world.createEntity();
        Entity* b = world.createEntity();
        CHECK(a != nullptr);
        CHECK(b != nullptr);

        for (Entity* e : {a, b}) {
            e->addComponent<Transform>();
            auto* skel = e->addComponent<SkeletonComponent>();
            e->addComponent<AnimationStateMachineComponent>();
            skel->skeleton = makeOneBoneSkeletonShared();
            skel->jointCount = static_cast<uint32_t>(skel->skeleton->getBoneCount());
            skel->skinMatrices = new ayt::math::Float4x4[skel->jointCount];
            const ayt::math::Float4x4 id = ayt::math::Float4x4::identity();
            for (uint32_t i = 0; i < skel->jointCount; ++i) {
                std::memcpy(&skel->skinMatrices[i], &id, sizeof(id));
            }
            skel->loaded = true;
            skel->player = ayt::anim::AnimationPlayer::create();
            skel->player->setSkeleton(skel->skeleton);
        }

        StateMachineSystem sm_system;
        auto* smc_a = a->getComponent<AnimationStateMachineComponent>();
        auto* smc_b = b->getComponent<AnimationStateMachineComponent>();

        populateIdleRunGraph(*sm_system.getOrCreateMachine(a));
        populateIdleRunGraph(*sm_system.getOrCreateMachine(b));

        // Fire transition on A only.
        smc_a->setTrigger("Run");
        sm_system.onUpdate(0.016f);

        CHECK(smc_a->currentState == "Run");
        CHECK(smc_b->currentState == "Idle");

        // Fire on B too — independent.
        smc_b->setTrigger("Run");
        sm_system.onUpdate(0.016f);
        CHECK(smc_a->currentState == "Run");
        CHECK(smc_b->currentState == "Run");

        teardown(world, a);
        teardown(world, b);
    }

TEST_SUITE_END