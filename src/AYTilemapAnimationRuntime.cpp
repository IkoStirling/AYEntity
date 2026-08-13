// AYTilemapAnimationRuntime.cpp — CM-5 (2026-08-12): mirror of the AY2D
// per-tile animation tick (see the header for the full contract).

#include "AYTilemapAnimationRuntime.h"

#include <algorithm>

namespace ayt::entity
{

namespace
{

// Mirror of ayt::ay2d::resolveAnimatedTileId evaluated for the current
// frame index (the AY2D original resolves at call time; the mirror
// resolves at tick time into `resolved[]` — identical results for any
// read after a tick).
uint32_t resolvedFrameId(const TileAnimTable& table,
                         const std::vector<uint32_t>& currentFrameIdx,
                         uint32_t sourceTileId) noexcept
{
    if (sourceTileId >= table.size()) {
        return sourceTileId;
    }
    const auto& frames = table[sourceTileId];
    if (frames.empty()) {
        return sourceTileId;
    }
    const size_t idx = currentFrameIdx[sourceTileId] % frames.size();
    return frames[idx].frameTileId;
}

} // namespace

TilemapAnimationRuntime& TilemapAnimationRuntime::instance()
{
    static TilemapAnimationRuntime s_runtime;
    return s_runtime;
}

TilemapAnimationRuntimeEntry* TilemapAnimationRuntime::ensure(
    const std::string& path, const ayt::resource::ITilemap& res)
{
    auto it = _byPath.find(path);
    if (it != _byPath.end()) {
        return &it->second;
    }

    TilemapAnimationRuntimeEntry e;
    const UInt32 count = res.getAnimationCount();
    if (count > 0) {
        const ayt::resource::TileAnimationEntry* entries =
            res.getAnimationEntries();
        if (entries == nullptr) {
            return nullptr;  // corrupt resource state — treat as no table
        }
        // Size to maxSourceTileId + 1 (mirror of the AY2D convention:
        // the outer vector's extent IS the hot-path index bound).
        UInt32 maxId = 0;
        for (UInt32 i = 0; i < count; ++i) {
            maxId = std::max(maxId, entries[i].sourceTileId);
        }
        e.table.resize(static_cast<size_t>(maxId) + 1);
        for (UInt32 i = 0; i < count; ++i) {
            const ayt::resource::TileAnimationEntry& src = entries[i];
            if (src.frameCount == 0 || src.frames == nullptr) {
                continue;  // defensive: empty entries never occur on disk
            }
            std::vector<TileAnimFrame>& dst = e.table[src.sourceTileId];
            dst.reserve(src.frameCount);
            for (UInt32 f = 0; f < src.frameCount; ++f) {
                const ayt::resource::TileAnimationFrame& fr = src.frames[f];
                dst.push_back(TileAnimFrame{fr.frameTileId, fr.durationMs});
            }
        }
        e.currentFrameIdx.assign(e.table.size(), 0u);
        e.elapsedMs.assign(e.table.size(), 0u);
        e.resolved.resize(e.table.size());
        // Frame-0 snapshot so even a render before the first tick shows
        // the first frame (belt and suspenders — the tick refreshes it).
        for (size_t i = 0; i < e.table.size(); ++i) {
            e.resolved[i] = resolvedFrameId(e.table, e.currentFrameIdx,
                                            static_cast<uint32_t>(i));
        }
    }

    const auto emplaced = _byPath.emplace(path, std::move(e));
    return &emplaced.first->second;
}

TilemapAnimationRuntimeEntry* TilemapAnimationRuntime::find(
    const std::string& path)
{
    auto it = _byPath.find(path);
    return it == _byPath.end() ? nullptr : &it->second;
}

void TilemapAnimationRuntime::tick(TilemapAnimationRuntimeEntry& e,
                                   int64_t nowUs)
{
    // First tick: stash the baseline, refresh the resolve buffer, no
    // advance (mirror of the AY2D first-call contract).
    if (!e.hasBeenTicked) {
        e.lastTickUs    = nowUs;
        e.hasBeenTicked = true;
        for (size_t i = 0; i < e.table.size(); ++i) {
            e.resolved[i] = resolvedFrameId(e.table, e.currentFrameIdx,
                                            static_cast<uint32_t>(i));
        }
        return;
    }

    int64_t deltaUs = nowUs - e.lastTickUs;
    if (deltaUs < 0) {
        deltaUs = 0;  // never run backwards (AY2D R-7 spirit)
    }
    e.lastTickUs = nowUs;
    if (deltaUs == 0) {
        return;  // second entity on the same path this frame — no-op
    }

    // Integer-ms accumulation — no float drift (design.md §7.2 lock).
    const uint32_t deltaMs = static_cast<uint32_t>(deltaUs / 1000);
    if (deltaMs == 0) {
        return;
    }

    const size_t n = e.table.size();
    if (n == 0) {
        return;  // no animations registered
    }

    for (size_t i = 0; i < n; ++i) {
        const auto& frames = e.table[i];
        if (frames.empty()) {
            continue;  // static — resolved[i] stays == i
        }

        uint32_t& elapsed  = e.elapsedMs[i];
        uint32_t& frameIdx = e.currentFrameIdx[i];
        elapsed += deltaMs;

        // Walk frames while elapsed exceeds the frame duration; loops by
        // mod; a zero-duration frame breaks (no infinite loop).
        while (true) {
            const TileAnimFrame& f = frames[frameIdx % frames.size()];
            if (f.durationMs == 0 || elapsed < f.durationMs) {
                break;
            }
            elapsed -= f.durationMs;
            frameIdx = (frameIdx + 1u)
                     % static_cast<uint32_t>(frames.size());
        }

        e.resolved[i] = resolvedFrameId(e.table, e.currentFrameIdx,
                                        static_cast<uint32_t>(i));
    }
}

uint32_t TilemapAnimationRuntime::resolve(
    const TilemapAnimationRuntimeEntry& e, uint32_t sourceTileId) noexcept
{
    if (sourceTileId < e.resolved.size()) {
        return e.resolved[sourceTileId];
    }
    return sourceTileId;
}

void TilemapAnimationRuntime::clear()
{
    _byPath.clear();
}

} // namespace ayt::entity
