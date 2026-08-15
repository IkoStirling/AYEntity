// AYBlendSpaceSystem.cpp — P2.1 (2026-07-27). Per-frame driver for
// BlendSpaceComponent → SkeletonComponent::skinMatricesBlendSpace.
//
// Phase 0 + Phase 1 (composite base pose):
//   1. query<SkeletonComponent, BlendSpaceComponent>
//   2. lazy-load each entry's clip path → IAnimation via
//      ResourceManager (cached in _clipCache by path)
//   3. dispatch to BlendSpace1D / BlendSpace2D based on is2D
//   4. setSkeleton + tick + setParameter + evaluate
//
// Phase 2 + 3 (world + skin):
//   The BlendSpace already wrote per-bone PARENT-LOCAL TRS into
//   _scratchPos/Rot/Scl. We now compose to world and multiply by
//   inverse-bind to produce the same skin matrix format
//   AnimationPlayer::getBoneSkinMatrices() returns. This 30-line
//   block duplicates AnimationPlayer.cpp:1198-1227 verbatim —
//   DUPLICATION-OK by design (P2.x cleanup: lift into a free helper
//   in ayanimation/AYAnimation.h).

#include <AYBlendSpaceSystem.h>

#include <AYEntity.h>      // full definition of Query<T...> (forward-declared in AYWorld.h)
#include <AYWorld.h>

#include <components/AYBlendSpaceComponent.h>
#include <components/AYSkeletonComponent.h>

#include <ayanimation/BlendSpace.h>

#include <AYMath/MathTypes.h>

#include <assetsDefs/IAYAnimation.h>
#include <assetsDefs/IAYSkeleton.h>
#include <assetsImpl/AYSkeleton.h>   // P1.7 — for static_pointer_cast<Skeleton>
#include <AYResourceManager.h>

#include <cassert>
#include <cstdio>
#include <cstring>

namespace ayt::entity
{

namespace
{

// Helper: copy a per-clip IAnimation into the BlendSpace's sample
// table. Returns true if the BlendSpace changed (caller sets
// lastPaths / lastPositions for rebind detection).
bool ensureBlendSpaceBinds(
    ayt::anim::BlendSpace1D&                     bs,
    const BlendSpaceComponent&                   bsComp,
    const std::vector<std::shared_ptr<ayt::resource::IAnimation>>& clips,
    std::vector<std::string>&                    lastPaths,
    std::vector<ayt::math::FVector2>&            lastPositions)
{
    bool changed = false;
    if (lastPaths.size() != clips.size()) {
        changed = true;
    } else {
        for (size_t i = 0; i < clips.size(); ++i) {
            if (lastPaths[i] != bsComp.entries[i].clipPath) {
                changed = true;
                break;
            }
            if (lastPositions[i].x != bsComp.entries[i].samplePosition.x
                || lastPositions[i].y != bsComp.entries[i].samplePosition.y) {
                changed = true;
                break;
            }
        }
    }
    if (!changed) return false;

    bs.removeAllSamplePoints();
    for (size_t i = 0; i < clips.size(); ++i) {
        if (clips[i]) {
            bs.addSamplePoint(bsComp.entries[i].samplePosition.x, clips[i]);
        }
    }
    lastPaths.clear();
    lastPositions.clear();
    lastPaths.reserve(clips.size());
    lastPositions.reserve(clips.size());
    for (size_t i = 0; i < clips.size(); ++i) {
        lastPaths.push_back(bsComp.entries[i].clipPath);
        lastPositions.push_back(bsComp.entries[i].samplePosition);
    }
    return true;
}

bool ensureBlendSpaceBinds(
    ayt::anim::BlendSpace2D&                     bs,
    const BlendSpaceComponent&                   bsComp,
    const std::vector<std::shared_ptr<ayt::resource::IAnimation>>& clips,
    std::vector<std::string>&                    lastPaths,
    std::vector<ayt::math::FVector2>&            lastPositions)
{
    bool changed = false;
    if (lastPaths.size() != clips.size()) {
        changed = true;
    } else {
        for (size_t i = 0; i < clips.size(); ++i) {
            if (lastPaths[i] != bsComp.entries[i].clipPath) {
                changed = true;
                break;
            }
            if (lastPositions[i].x != bsComp.entries[i].samplePosition.x
                || lastPositions[i].y != bsComp.entries[i].samplePosition.y) {
                changed = true;
                break;
            }
        }
    }
    if (!changed) return false;

    bs.removeAllSamplePoints();
    for (size_t i = 0; i < clips.size(); ++i) {
        if (clips[i]) {
            bs.addSamplePoint(bsComp.entries[i].samplePosition, clips[i]);
        }
    }
    lastPaths.clear();
    lastPositions.clear();
    lastPaths.reserve(clips.size());
    lastPositions.reserve(clips.size());
    for (size_t i = 0; i < clips.size(); ++i) {
        lastPaths.push_back(bsComp.entries[i].clipPath);
        lastPositions.push_back(bsComp.entries[i].samplePosition);
    }
    return true;
}

// Promote per-bone parent-local TRS → world → skin matrix. Writes
// into skel->skinMatricesBlendSpace (preallocated by SkeletonComponent
// ctor / lazy-resize path). DUPLICATION-OK — mirrors
// AnimationPlayer.cpp:1198-1227 (Phase 2/3 body).
void composeParentLocalToSkin(
    const std::vector<float>& localPos,    // n * 3
    const std::vector<float>& localRot,    // n * 4
    const std::vector<float>& localScl,    // n * 3
    const ayt::resource::ISkeleton* skel,
    std::vector<ayt::math::Float4x4>& outSkin)
{
    using namespace ayt::math;
    if (skel == nullptr) return;
    const size_t n = skel->getBoneCount();
    if (n == 0) return;
    const ayt::resource::Bone* bones = skel->getBones();
    const Float4x4* ibm = skel->getInverseBindMatrices();
    outSkin.assign(n, Float4x4::identity());
    for (size_t i = 0; i < n; ++i) {
        const FVector3    lp(localPos[i * 3 + 0], localPos[i * 3 + 1], localPos[i * 3 + 2]);
        const FQuaternion lr(localRot[i * 4 + 0], localRot[i * 4 + 1],
                             localRot[i * 4 + 2], localRot[i * 4 + 3]);
        const FVector3    ls(localScl[i * 3 + 0], localScl[i * 3 + 1], localScl[i * 3 + 2]);
        const Float4x4 local = Float4x4::fromTRS(lp, lr, ls);
        const int p = bones[i].parentIndex;
        Float4x4 world;
        if (p < 0) {
            world = local;
        } else {
            world = outSkin[static_cast<size_t>(p)] * local;
        }
        outSkin[i] = (ibm != nullptr) ? (world * ibm[i]) : world;
    }
}

} // namespace

void BlendSpaceSystem::onUpdate(float dt)
{
    World& world = World::instance();
    for (Entity* e : world.query<SkeletonComponent, BlendSpaceComponent>()) {
        if (e == nullptr) continue;
        SkeletonComponent*     skel = e->getComponent<SkeletonComponent>();
        BlendSpaceComponent*   bs   = e->getComponent<BlendSpaceComponent>();
        if (skel == nullptr || bs == nullptr) continue;
        if (!bs->isValid()) continue;

        // Defer if the skeleton hasn't been lazy-loaded yet by
        // AnimationSystem (priority 450 runs AFTER us). We can't
        // produce skin matrices without a skeleton; skip this
        // entity's BlendSpace this frame. Once AnimationSystem
        // populates `skel->skeleton` + `skel->jointCount` the next
        // frame's BlendSpaceSystem tick will pick it up.
        if (!skel->loaded || skel->jointCount == 0 || !skel->skeleton) {
            continue;
        }

        // Lazy-load each entry's clip (path-keyed cache so multiple
        // entities sharing a clip share the parsed IAnimation).
        std::vector<std::shared_ptr<ayt::resource::IAnimation>> clips;
        clips.reserve(bs->entries.size());
        bool loadFailed = false;
        for (const auto& entry : bs->entries) {
            if (entry.clipPath.empty()) {
                clips.emplace_back(nullptr);
                continue;
            }
            auto it = _clipCache.find(entry.clipPath);
            if (it == _clipCache.end()) {
                auto res = ayt::resource::ResourceManager::instance()
                              .load<ayt::resource::IAnimation>(entry.clipPath);
                if (!res) {
                    std::fprintf(stderr,
                                 "[BlendSpaceSystem] loadAnimation('%s') failed\n",
                                 entry.clipPath.c_str());
                    loadFailed = true;
                    clips.emplace_back(nullptr);
                    continue;
                }
                _clipCache.emplace(entry.clipPath, res);
                clips.emplace_back(res);
            } else {
                clips.emplace_back(it->second);
            }
        }
        if (loadFailed) {
            // Any single-entry failure leaves the BlendSpace with a
            // nullptr sample point — keep going (that vertex gets
            // weight 0, others contribute fully).
        }

        EntityState& state = _entityStates[e];
        // Re-bind the BlendSpace if entries[] changed since last
        // frame or if is2D toggled.
        if (state.is2D != bs->is2D) {
            state.is2D = bs->is2D;
            state.bs1D.removeAllSamplePoints();
            state.bs2D.removeAllSamplePoints();
            state.lastPaths.clear();
            state.lastPositions.clear();
        }

        // Bind skeleton on first sight or after a skeleton swap.
        // The BlendSpace owns its own AnimationPlayers; the
        // shared_ptr<Skeleton> in SkeletonComponent is the source of
        // truth for the asset lifetime.
        // (We can't directly pass shared_ptr<ISkeleton> to BlendSpace
        // because SkeletonComponent holds shared_ptr<Skeleton> by
        // design — the implicit conversion handles the rest.)

        if (bs->is2D) {
            // 2D path.
            ensureBlendSpaceBinds(state.bs2D, *bs, clips,
                                  state.lastPaths, state.lastPositions);
            // Skeleton binding: only rebind if the underlying raw
            // ISkeleton pointer changed.
            // NOTE: BlendSpace2D::setSkeleton takes
            // shared_ptr<const ISkeleton>; SkeletonComponent owns
            // shared_ptr<Skeleton>. Implicit conversion via
            // shared_ptr's converting constructor works because
            // Skeleton derives public from ISkeleton.
            // For simplicity we cast: shared_ptr<Skeleton> →
            // shared_ptr<const ISkeleton>.
            std::shared_ptr<const ayt::resource::ISkeleton> skelSP =
                std::static_pointer_cast<const ayt::resource::ISkeleton>(
                    skel->skeleton);
            // Cheap binding: only call setSkeleton if the previous
            // binding's raw pointer differs.
            // (BlendSpace stores shared_ptr; comparing .get() before
            // assignment would be ideal but we don't track it here.
            // Re-binding is harmless when skeleton is unchanged — it
            // just refreshes the internal _skeleton field.)
            state.bs2D.setSkeleton(skelSP);
            state.bs2D.setParameter(bs->sampleInput);
            state.bs2D.setPlayRate(bs->playRate);
            state.bs2D.setLoop(bs->looping);
            state.bs2D.tick(dt);

            // Resize scratch once per entity.
            if (state.scratchPos.size() != skel->jointCount * 3) {
                state.scratchPos.assign(skel->jointCount * 3, 0.0f);
                state.scratchRot.assign(skel->jointCount * 4, 0.0f);
                state.scratchScl.assign(skel->jointCount * 3, 1.0f);
                state.bs2D.resizeTRS(skel->jointCount);
            }
            state.bs2D.evaluate(state.scratchPos, state.scratchRot, state.scratchScl);
        } else {
            // 1D path.
            ensureBlendSpaceBinds(state.bs1D, *bs, clips,
                                  state.lastPaths, state.lastPositions);
            std::shared_ptr<const ayt::resource::ISkeleton> skelSP =
                std::static_pointer_cast<const ayt::resource::ISkeleton>(
                    skel->skeleton);
            state.bs1D.setSkeleton(skelSP);
            state.bs1D.setParameter(bs->sampleInput.x);
            state.bs1D.setPlayRate(bs->playRate);
            state.bs1D.setLoop(bs->looping);
            state.bs1D.tick(dt);

            if (state.scratchPos.size() != skel->jointCount * 3) {
                state.scratchPos.assign(skel->jointCount * 3, 0.0f);
                state.scratchRot.assign(skel->jointCount * 4, 0.0f);
                state.scratchScl.assign(skel->jointCount * 3, 1.0f);
                state.bs1D.resizeTRS(skel->jointCount);
            }
            state.bs1D.evaluate(state.scratchPos, state.scratchRot, state.scratchScl);
        }

        // Allocate / reallocate skinMatricesBlendSpace on the
        // SkeletonComponent. Reuses the same jointCount + delete[]
        // pattern as the existing skinMatrices field.
        if (skel->skinMatricesBlendSpace == nullptr
            || skel->jointCount == 0) {
            delete[] skel->skinMatricesBlendSpace;
            skel->skinMatricesBlendSpace = nullptr;
            if (skel->jointCount > 0) {
                skel->skinMatricesBlendSpace =
                    new ayt::math::Float4x4[skel->jointCount];
                for (uint32_t i = 0; i < skel->jointCount; ++i) {
                    skel->skinMatricesBlendSpace[i] =
                        ayt::math::Float4x4::identity();
                }
            }
        }

        // Phase 2/3 — promote parent-local TRS to skin matrix.
        std::vector<ayt::math::Float4x4> worldSkin;
        worldSkin.reserve(skel->jointCount);
        composeParentLocalToSkin(state.scratchPos, state.scratchRot,
                                 state.scratchScl,
                                 skel->skeleton.get(), worldSkin);
        if (!worldSkin.empty()) {
            std::memcpy(skel->skinMatricesBlendSpace, worldSkin.data(),
                        skel->jointCount * sizeof(ayt::math::Float4x4));
        }
    }
}

void registerBlendSpaceSystem()
{
    World::instance().registerSystem<BlendSpaceSystem>(BlendSpaceSystem::kPriority);
}

} // namespace ayt::entity