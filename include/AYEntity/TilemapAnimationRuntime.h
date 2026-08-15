#pragma once
// AYEntity/TilemapAnimationRuntime.h — CM-5 (2026-08-12): engine-side mirror of
// the AY2D per-tile animation table tick (ayt::ay2d TileAnimationTable /
// tickTilemapAnimation / resolveAnimatedTileId).
//
// Dependency-direction lock: AYEntity must not depend on AY2D (same rule
// as AYEntity/2DUvMath.h / AYEntity/OrthoCameraUpdateSystem.h). This runtime mirrors the
// ayt::ay2d PODs and the integer-ms tick semantics 1:1; the unittest
// cross-asserts the mirror against the real AY2D functions on a shared
// deterministic case (AYTest_2DComponents precedent) so the two never
// drift.
//
// Data flow per frame (single-threaded — the lane runs with
// setRenderThreadEnabled(false), same trust as the 3D lane):
//   1. TilemapAnimationTickSystem (priority 460) queries
//      <Transform, TilemapComponent>, ensure()s one runtime entry per
//      tilemap path, and ticks it with ayt::performanceNowUs() (QPC —
//      design.md §7.2's gameNow is not advanced anywhere in the tree, so
//      the call site holds the clock, exactly like ayt::ay2d's contract).
//   2. The tick refreshes `resolved[]` — the render path (510, runs after
//      460 in the same frame) indexes it once per tile with no hash.
//   3. A tilemap path without a runtime entry (tick system not registered,
//      load failed) renders fully static — the AY2D "no table == static"
//      contract, fail-soft.

#include <AYEntity/IEntity.h>

#include <AYResource/assetsDefs/ITilemap.h>

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace ayt::entity
{

// One frame of one animation entry (mirror of ayt::ay2d::TileFrame).
struct TileAnimFrame {
    uint32_t frameTileId = 0;  // tile id to display for this frame
    uint32_t durationMs  = 0;  // hold time in integer ms (0 = break/no-op)
};

// Sparse table (mirror of ayt::ay2d::TileAnimationTable): indexed by
// sourceTileId, empty inner vector == static tile. Sized to
// maxSourceTileId + 1 (the hot-path index bound).
using TileAnimTable = std::vector<std::vector<TileAnimFrame>>;

// Mirror runtime state for one tilemap path. One entry per path, shared
// across entities that reference the same tilemap — tick() is idempotent
// for zero deltas, so two entities on one path ticked in the same frame
// only advance once.
struct TilemapAnimationRuntimeEntry {
    TileAnimTable table;                     // sparse by sourceTileId
    std::vector<uint32_t> currentFrameIdx;   // index into the inner vec
    std::vector<uint32_t> elapsedMs;         // ms into the current frame
    // Hot-path resolve buffer: resolved[i] == the frame tile id currently
    // displayed for source tile id i (== i when static/absent). Refreshed
    // by every tick() call (including the first baseline call), so the
    // render path reads it with no hash and no table traversal.
    std::vector<uint32_t> resolved;
    int64_t lastTickUs    = 0;
    bool    hasBeenTicked = false;
};

// Per-path cache + tick/resolve API. Singleton mirrors World's lifetime
// contract; tests call clear() to reset between cases.
class TilemapAnimationRuntime {
public:
    static TilemapAnimationRuntime& instance();

    // Returns the entry for `path`, building it from the resource's
    // animation table if it does not exist yet. The table is copied once
    // at ensure time; a reloaded resource with a changed table for the
    // same path is not re-read (call clear() to drop the cache).
    TilemapAnimationRuntimeEntry* ensure(
        const std::string& path, const ayt::resource::ITilemap& res);

    // Cache lookup without a resource: nullptr when the path was never
    // ensured (render path uses this — absent entry means static).
    TilemapAnimationRuntimeEntry* find(const std::string& path);

    // Advances `e` to `nowUs`, mirroring ayt::ay2d::tickTilemapAnimation:
    //   - first call stashes the baseline, no advance (no huge initial
    //     jump on a far-future first timestamp);
    //   - reversed clock (nowUs < lastTickUs) clamps delta to 0;
    //   - deltaUs == 0 (e.g. a second entity sharing the path in the same
    //     frame) is a no-op — idempotent by construction;
    //   - integer-ms accumulation; while-loop modulo advance; a
    //     zero-duration frame breaks (never advances, never loops);
    //   - refreshes resolved[] for every table index.
    void tick(TilemapAnimationRuntimeEntry& e, int64_t nowUs);

    // Animation source tile id -> currently displayed frame tile id.
    // Out-of-bounds or static (empty entry) -> sourceTileId unchanged
    // (mirror of ayt::ay2d::resolveAnimatedTileId).
    static uint32_t resolve(const TilemapAnimationRuntimeEntry& e,
                            uint32_t sourceTileId) noexcept;

    // Test hook: drops the per-path cache.
    void clear();

private:
    TilemapAnimationRuntime() = default;

    std::unordered_map<std::string, TilemapAnimationRuntimeEntry> _byPath;
};

} // namespace ayt::entity
