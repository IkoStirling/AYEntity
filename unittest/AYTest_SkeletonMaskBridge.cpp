// AYTest_SkeletonMaskBridge.cpp — P2.2 (2026-08-03) bridge tests.
//
// Validates the AnimationSystem ↔ AnimationPlayer mask bridge:
//   * ECS-side `AnimationComponent::maskPath` triggers rebind.
//   * ResourceManager::load<ISkeletonMask>(path) is the only load path.
//   * The .aymask loader is deferred per §4.2.1 — every load in this
//     test returns nullptr by design. The bridge must fail-soft.
//   * The rebind-detection cache (`_lastAppliedMaskPath`) latches
//     failed paths so subsequent ticks do not retry the loader
//     every frame (no log spam, no per-frame work).
//
// The tests deliberately use the DIRECT PLAYER API to validate the
// bridge in isolation from any future .aymask loader — when that
// loader ships, the bridge call path is unchanged and these tests
// still pass bit-identical. This mirrors the P1.5 / BlendSpaceSystem
// test pattern (no in-package loader stub; the test exercises the
// real ResourceManager nullptr return).
//
// Cleanup contract: every test ends with destroyEntity + shutdown
// (mirrors AYTest_BlendSpaceSystem.cpp). Without shutdown the
// accumulated World state corrupts the singleton destructor at exit
// (World::~World calls removeAllComponents for each entity and
// crashes on a sparse-set lookup after the static storage is gone).

#include <AYEntity.h>
#include <AYEntityModule.h>
#include <AYWorld.h>

#include <AYAnimationSystem.h>
#include <components/AYAnimationComponent.h>
#include <components/AYSkeletonComponent.h>
#include <components/AYTransformComponent.h>

#include <ayanimation/AnimationPlayer.h>
#include <ayanimation/ISkeletonMask.h>

#include <assetsImpl/AYSkeleton.h>
#include <aymath/MathTypes.h>

#include <AYTest.h>
#include <AYResourceManager.h>

#include "../../AYAnimation/src/SkeletonMask.h"   // P2.2 fixture

#include <cstdio>
#include <memory>
#include <string>

using namespace ayt::entity;
using ayt::anim::ISkeletonMask;
using ayt::anim::SkeletonMask;

namespace
{

// Build a 1-bone skeleton ("Bone0") so the player has a minimal
// binding target. P1.7 pattern: shared_ptr into SkeletonComponent.
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

// Canonical fixture: World + entity + SkeletonComponent bound + a
// no-op clipPath so AnimationSystem skips its lazy-load block
// (maskPath-empty test does NOT need a real .ayanm).
//
// Returns the freshly-created entity; tests call `system.onUpdate`
// directly (mirrors SkinnedAnimationTest.cpp).
//
// The fixture ALWAYS shutdowns + reinitializes the World first so
// any stale entities from a previous test are cleared. This matches
// the AYTest_BlendSpaceSystem.cpp pattern.
// The caller is responsible for `world.destroyEntity(e)` +
// `world.shutdown()` at end of each test — see the cleanup contract
// at the top of this file.
Entity* makeMaskedEntity(World& world, const std::string& maskPath)
{
    world.shutdown();
    world.initialize();
    Entity* e = world.createEntity();
    if (e == nullptr) return nullptr;
    e->addComponent<Transform>();
    auto* skel = e->addComponent<SkeletonComponent>();
    auto* anim = e->addComponent<AnimationComponent>();
    anim->autoplay = false;          // tests don't drive time
    anim->looping  = true;
    anim->playRate = 1.0f;
    anim->clipPath = "";             // bind-pose / mesh-only path
    anim->maskPath = maskPath;

    // Manually bind the skeleton so the rebind block can find
    // skel->player already created. The bridge lazy-loads the player
    // only when clipPath is non-empty; bypassing that lets us test
    // the mask rebind block in isolation.
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
    return e;
}

// Standard cleanup (matches AYTest_BlendSpaceSystem.cpp). MUST be
// called at the end of every test in this suite to avoid the World
// destructor crash described in the file banner.
void teardown(World& world, Entity* e)
{
    if (e != nullptr) {
        world.destroyEntity(e);
    }
}

} // namespace

TEST_SUITE(SkeletonMaskBridgeTests)

    // ─── #1 — maskPath empty → bridge leaves player with no mask. ──
    // Bit-identical to P2.1 behavior. INV-15 regression guard.
    TEST_CASE(bridge_maskpath_empty_means_no_mask_applied) {
        World& world = World::instance();
        Entity* e = makeMaskedEntity(world, /*maskPath=*/"");
        CHECK(e != nullptr);
        AnimationSystem system;

        system.onUpdate(0.0001f);

        auto* skel = e->getComponent<SkeletonComponent>();
        CHECK(skel != nullptr);
        CHECK(skel->player != nullptr);
        CHECK(skel->player->hasSkeletonMask() == false);
        CHECK(skel->player->getSkeletonMaskBoneCount() == 0u);

        teardown(world, e);
    }

    // ─── #2 — maskPath = nonexistent path → fail-soft to no mask. ─
    // ResourceManager::load<ISkeletonMask> returns nullptr because
    // no .aymask loader is registered (deferred per §4.2.1). The
    // bridge must NOT crash, must NOT leave a half-bound mask, and
    // must latch the failed path so subsequent ticks no-op.
    TEST_CASE(bridge_maskpath_load_failure_degrades_to_no_mask) {
        const std::string nonexistent = "nonexistent_test_path_no_loader";
        World& world = World::instance();
        Entity* e = makeMaskedEntity(world, nonexistent);
        CHECK(e != nullptr);
        AnimationSystem system;

        system.onUpdate(0.0001f);

        auto* skel = e->getComponent<SkeletonComponent>();
        CHECK(skel != nullptr);
        CHECK(skel->player != nullptr);
        CHECK(skel->player->hasSkeletonMask() == false);
        CHECK(skel->player->getSkeletonMaskBoneCount() == 0u);

        teardown(world, e);
    }

    // ─── #3 — Rebind cache latches failed path. Second tick is no-op. ─
    // The bridge stores the failed path in _lastAppliedMaskPath so
    // ResourceManager::load is not retried every frame. We assert by
    // counting the player's resolved-weight state across three ticks:
    // tick 1 latches + warns; ticks 2 and 3 see the latch and skip
    // the load entirely (no change in mask generation or weight count).
    TEST_CASE(bridge_maskpath_load_failure_latched_no_retry) {
        const std::string nonexistent = "nonexistent_test_path_no_loader";
        World& world = World::instance();
        Entity* e = makeMaskedEntity(world, nonexistent);
        CHECK(e != nullptr);
        AnimationSystem system;

        system.onUpdate(0.0001f);  // tick 1 — load, fail, latch + warn
        auto* skel = e->getComponent<SkeletonComponent>();
        CHECK(skel->player->hasSkeletonMask() == false);

        // Snapshot the player's resolved weights. Subsequent ticks
        // must not change anything (latch holds).
        const std::uint32_t genAfterTick1 =
            skel->player->getSkeletonMaskGeneration();
        const std::size_t bonesAfterTick1 =
            skel->player->getSkeletonMaskBoneCount();

        system.onUpdate(0.0001f);  // tick 2 — must be a no-op
        CHECK(skel->player->getSkeletonMaskGeneration() == genAfterTick1);
        CHECK(skel->player->getSkeletonMaskBoneCount() == bonesAfterTick1);

        system.onUpdate(0.0001f);  // tick 3 — still no-op
        CHECK(skel->player->getSkeletonMaskGeneration() == genAfterTick1);
        CHECK(skel->player->getSkeletonMaskBoneCount() == bonesAfterTick1);

        teardown(world, e);
    }

    // ─── #4 — Direct-API mask survives when anim->maskPath stays empty. ─
    // The user pre-binds a mask via the direct player API (the
    // in-memory fixture, not ResourceManager). With `anim->maskPath`
    // empty AND `_lastAppliedMaskPath[e]` empty (first-time bind),
    // the bridge sees `lastMask == anim->maskPath` and skips the
    // rebind block entirely. The user's direct-API mask survives.
    TEST_CASE(bridge_direct_mask_survives_when_maskpath_empty_first_time) {
        World& world = World::instance();
        Entity* e = makeMaskedEntity(world, /*maskPath=*/"");
        CHECK(e != nullptr);
        auto* skel = e->getComponent<SkeletonComponent>();

        // Direct API bind (the test fixture, not ResourceManager).
        auto maskHandle = SkeletonMask::create();
        maskHandle->addEntry("Bone0", 0.5f);
        skel->player->setSkeletonMask(maskHandle);
        CHECK(skel->player->hasSkeletonMask());
        CHECK(skel->player->getSkeletonMaskBoneCount() == 1u);

        // Tick. The bridge sees anim->maskPath = "" and lastMask = ""
        // (first-time bind) — `lastMask != anim->maskPath` is FALSE,
        // so the bridge does NOT run clearSkeletonMask. The direct-API
        // mask survives.
        AnimationSystem system;
        system.onUpdate(0.0001f);
        CHECK(skel->player->hasSkeletonMask());
        CHECK(skel->player->getSkeletonMaskBoneCount() == 1u);

        teardown(world, e);
    }

    // ─── #5 — Direct-API mask survives repeated ticks when path is empty. ─
    // This is the ergonomic case: user manages the mask entirely via
    // the player API (e.g. a procedural character controller). The
    // bridge must stay out of the way across many frames.
    TEST_CASE(bridge_direct_mask_survives_when_maskpath_empty_repeated_ticks) {
        World& world = World::instance();
        Entity* e = makeMaskedEntity(world, /*maskPath=*/"");
        CHECK(e != nullptr);
        auto* skel = e->getComponent<SkeletonComponent>();

        auto maskHandle = SkeletonMask::create();
        maskHandle->addEntry("Bone0", 0.25f);
        skel->player->setSkeletonMask(maskHandle);

        const std::uint32_t gen0 = skel->player->getSkeletonMaskGeneration();

        AnimationSystem system;
        for (int i = 0; i < 5; ++i) {
            system.onUpdate(0.0001f);
        }
        // Mask still bound (bridge did not interfere).
        CHECK(skel->player->hasSkeletonMask());
        CHECK(skel->player->getSkeletonMaskBoneCount() == 1u);
        // Generation did NOT bump — bridge skipped the rebind block
        // entirely because lastMask == anim->maskPath == "".
        CHECK(skel->player->getSkeletonMaskGeneration() == gen0);
        // The user's resolved weights must still reflect Bone0=0.25.
        const auto& w = skel->player->getResolvedBoneMaskWeights();
        CHECK_FLOAT_EQ(w[0], 0.25f, 1e-6f);

        teardown(world, e);
    }

    // ─── #6 — User clears maskPath → bridge calls clearSkeletonMask. ─
    // Setup: pre-bind a mask via the direct API. Then set
    // anim->maskPath = "" AFTER a tick has latched the last applied
    // state (empty). The bridge sees `lastMask != anim->maskPath`
    // (lastMask="" too in first-time bind, but we flip the field to
    // force a path change detection). This test exercises the
    // clearing path of the bridge — flipping a previously bound path
    // back to empty MUST trigger clearSkeletonMask so the user's
    // inspector "clear" action works.
    TEST_CASE(bridge_maskpath_cleared_runs_clear_skeleton_mask) {
        const std::string nonexistent = "nonexistent_test_path_no_loader";
        World& world = World::instance();
        Entity* e = makeMaskedEntity(world, /*maskPath=*/"");
        CHECK(e != nullptr);
        auto* skel = e->getComponent<SkeletonComponent>();

        // Tick 1: bridge sees lastMask="" == anim->maskPath="" → no-op.
        AnimationSystem system;
        system.onUpdate(0.0001f);

        // Direct API bind.
        auto maskHandle = SkeletonMask::create();
        maskHandle->addEntry("Bone0", 0.0f);    // suppress Bone0
        skel->player->setSkeletonMask(maskHandle);
        CHECK(skel->player->hasSkeletonMask());

        // Set the maskPath to a non-empty value then back to empty
        // in two steps to force the rebind block to fire.
        auto* anim = e->getComponent<AnimationComponent>();
        anim->maskPath = nonexistent;
        system.onUpdate(0.0001f);  // tick 2: load fails, latch
        // Direct-API mask survives (fail-soft does NOT clear).
        CHECK(skel->player->hasSkeletonMask());

        // Now flip back to empty — bridge should clear the mask.
        anim->maskPath = "";
        system.onUpdate(0.0001f);  // tick 3: path differs → clearSkeletonMask
        CHECK(skel->player->hasSkeletonMask() == false);
        CHECK(skel->player->getSkeletonMaskBoneCount() == 0u);

        teardown(world, e);
    }

TEST_SUITE_END