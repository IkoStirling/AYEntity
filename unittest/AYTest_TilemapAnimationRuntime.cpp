// AYTest_TilemapAnimationRuntime.cpp — CM-5 (2026-08-12) acceptance.
//
// Covers the engine-side tile animation table runtime:
//   1. cross-assert vs the real ayt::ay2d::tickTilemapAnimation /
//      resolveAnimatedTileId on a shared deterministic case (frameIdx /
//      elapsedMs / resolved must match step for step) — the mirror
//      drift lock;
//   2. resolve semantics: static / out-of-bounds ids pass through;
//   3. idempotent double-tick (two entities on one path in a frame) +
//      reversed clock clamping;
//   4. empty table (no animations) is fully static;
//   5. tick system wiring: entities share one per-path entry;
//   6. sticky-Noop render closed loop: an animated tile's payload UV
//      flips to the next frame's tile id after a tick (the Present-lane
//      consumer).
//
// Systems are driven via constructed instances (the AYTest_2DRenderSystems
// inline-test pattern). The AY2D lib link here is test-only (CM-5 comment
// in unittest/CMakeLists.txt); the AYEntity lib itself never links AY2D.

#include <AYEntity.h>
#include <AYEntityImpl.h>
#include <AYEntityModule.h>
#include <AYWorld.h>

#include <AY2DUvMath.h>
#include <AYRendererSubSystem.h>
#include <AYSubSystemRegistry.h>
#include <AYTilemapAnimationRuntime.h>
#include <AYTilemapAnimationTickSystem.h>
#include <AYTilemapRenderSystem.h>

#include <components/AYTilemapComponent.h>
#include <components/AYTransformComponent.h>

#include <assetsImpl/AYTexture.h>
#include <assetsImpl/AYTilemapAsset.h>

#include <AYResourceManager.h>
#include <AYTest.h>

#include <ayio/File.h>

// Real AY2D — test-only cross-assert target (never linked by the lib).
#include <AYTileAnimation.h>
#include <AYTilemap.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <sys/stat.h>
#include <vector>

#ifndef AY_SHADER_SHADERC_HINT
#  define AY_SHADER_SHADERC_HINT ""
#endif

#ifdef _WIN32
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#endif

using ayt::entity::Entity;
using ayt::entity::TilemapAnimationRuntime;
using ayt::entity::TilemapAnimationRuntimeEntry;
using ayt::entity::TilemapComponent;
using ayt::entity::Transform;
using ayt::entity::World;

namespace
{

int g_sentinelWindowHandle = 0;

std::string tempPath(const char* name)
{
    std::string path = std::string("test_cm5_2d_") + name;
#ifdef _WIN32
    char tempDir[MAX_PATH] = {};
    const DWORD len = GetTempPathA(MAX_PATH, tempDir);
    if (len > 0 && len < MAX_PATH) {
        path = std::string(tempDir) + name;
    }
#endif
    return path;
}

bool writeBinaryFile(const std::string& path,
                     const std::vector<ayt::resource::UInt8>& data)
{
    ayt::io::File file(path, ayt::io::File::Mode::BinaryWrite);
    if (!file.isOpen()) {
        return false;
    }
    return file.write(data.data(), data.size()) == data.size();
}

bool fileExists(const std::string& path)
{
    struct stat st;
    return !path.empty() && ::stat(path.c_str(), &st) == 0;
}

bool shadercAvailable()
{
    if (AY_SHADER_SHADERC_HINT[0] == '\0') {
        return false;
    }
    struct stat st;
    return ::stat(AY_SHADER_SHADERC_HINT, &st) == 0;
}

// The render systems resolve the renderer through the process-global
// SubSystemRegistry (the production seam — demos/editor register a
// RendererSubSystem there; a stack instance is invisible to them and
// the systems early-return with zero items). The closed-loop case
// must therefore register one for real: the registry owns it, and
// unregistering deletes it. initialize() failure unregisters so the
// registry never holds a half-built subsystem.
ayt::render::RendererSubSystem* registerTestRenderer()
{
    ayt::render::RendererSubSystem::setWindowProvider({});
    ayt::render::RendererSubSystem::setBootstrapBackend(
        ayt::render::Backend::Noop);
    ayt::render::RendererSubSystem::setBootstrapWindow(
        &g_sentinelWindowHandle, 800, 600);
    auto* rss = new ayt::render::RendererSubSystem();
    ayt::game::SubSystemRegistry::instance().registerSubSystem(rss);
    if (!rss->initialize()) {
        ayt::game::SubSystemRegistry::instance().unregisterSubSystem("Renderer");
        return nullptr;
    }
    return rss;
}

void unregisterTestRenderer()
{
    ayt::game::SubSystemRegistry::instance().unregisterSubSystem("Renderer");
}

// Bake a real .aytex atlas (8x8 checkerboard) + a real .aytilemap
// (2 cols x 3 rows, all tiles = defaultTileId 2, animated via entry
// 2 -> [(10, 60ms), (11, 60ms)]).
bool bakeAnimatedAssets(std::string& outTexPath, std::string& outMapPath)
{
    ayt::resource::Texture texture;
    texture.createCheckerboard(8, 8, 4);
    std::vector<ayt::resource::UInt8> texBinary;
    if (!texture.saveToBinary(texBinary)) {
        return false;
    }
    outTexPath = tempPath("atlas.aytex");
    if (!writeBinaryFile(outTexPath, texBinary)) {
        return false;
    }

    ayt::resource::TilemapAsset map;
    map.create(2u, 3u, 32u, 32u,
               ayt::resource::TilemapPackMode::Narrow16,
               /*defaultTileId=*/2u, nullptr, 0u);
    const ayt::resource::TileAnimationFrame frames[] = {
        {10u, 60u}, {11u, 60u},
    };
    if (!map.setAnimationEntry(2u, frames, 2u)) {
        return false;
    }
    std::vector<ayt::resource::UInt8> mapBinary;
    if (!map.saveToBinary(mapBinary)) {
        return false;
    }
    outMapPath = tempPath("map_anim.aytilemap");
    return writeBinaryFile(outMapPath, mapBinary);
}

// Assert a payload's UV equals tileUvQuad(tileId) in an 8x4 dense grid
// of 32x32 tiles.
void checkPayloadUvIsTileN(uint32_t tileId,
                           const ayt::render::DrawPayload2D& payload)
{
    ayt::entity::AtlasGridDesc grid;
    grid.tilesPerRow      = 8;
    grid.tilesPerColumn   = 4;
    grid.tileWidthTexels  = 32;
    grid.tileHeightTexels = 32;
    const ayt::entity::TileUvQuad uv = ayt::entity::tileUvQuad(tileId, grid);
    CHECK_FLOAT_EQ(payload.sourceRectMin.x, uv.uMin, 1e-6f);
    CHECK_FLOAT_EQ(payload.sourceRectMin.y, uv.vMin, 1e-6f);
    CHECK_FLOAT_EQ(payload.sourceRectMax.x, uv.uMax, 1e-6f);
    CHECK_FLOAT_EQ(payload.sourceRectMax.y, uv.vMax, 1e-6f);
}

// Mirror-side fixture: a TilemapAsset carrying the deterministic table
// (entry 2: three 100/150/100ms frames; entry 5: a zero-duration frame —
// tick break; entry 9: single frame).
ayt::resource::TilemapAsset makeDeterministicAsset()
{
    ayt::resource::TilemapAsset asset;
    asset.create(4u, 4u, 32u, 32u, ayt::resource::TilemapPackMode::Narrow16,
                 0u, nullptr, 0u);
    const ayt::resource::TileAnimationFrame water[] = {
        {10u, 100u}, {11u, 150u}, {12u, 100u},
    };
    asset.setAnimationEntry(2u, water, 3u);
    const ayt::resource::TileAnimationFrame zeroDur[] = {
        {20u, 0u}, {21u, 250u},
    };
    asset.setAnimationEntry(5u, zeroDur, 2u);
    const ayt::resource::TileAnimationFrame lava[] = {{90u, 300u}};
    asset.setAnimationEntry(9u, lava, 1u);
    return asset;
}

// AY2D-side fixture with the same table.
void fillRealTable(ayt::ay2d::Tilemap& real)
{
    real.animationTable.resize(10u);
    real.animationTable[2].push_back({10u, 100u});
    real.animationTable[2].push_back({11u, 150u});
    real.animationTable[2].push_back({12u, 100u});
    real.animationTable[5].push_back({20u, 0u});
    real.animationTable[5].push_back({21u, 250u});
    real.animationTable[9].push_back({90u, 300u});
}

} // namespace

TEST_SUITE(AYEntityTilemapAnimationRuntime)

// ─── #1 — mirror vs real AY2D: deterministic step-for-step match. ──
TEST_CASE(cm5_runtime_cross_asserts_ay2d_tick)
{
    ayt::resource::TilemapAsset asset = makeDeterministicAsset();

    TilemapAnimationRuntime& rt = TilemapAnimationRuntime::instance();
    rt.clear();
    TilemapAnimationRuntimeEntry* mirror = rt.ensure("x", asset);
    CHECK_NOT_NULL(mirror);
    CHECK_INT_EQ(static_cast<int>(mirror->table.size()), 10);
    CHECK_INT_EQ(static_cast<int>(mirror->resolved.size()), 10);

    ayt::ay2d::Tilemap real;
    fillRealTable(real);

    // Shared deterministic walk (microseconds). Step 1 baselines both
    // sides; every later step must produce identical state.
    const int64_t nowUs[] = {0, 50000, 100000, 150000, 300000, 1000000};
    for (int64_t now : nowUs) {
        rt.tick(*mirror, now);
        ayt::ay2d::tickTilemapAnimation(real, now);

        for (uint32_t i = 0u; i <= 9u; ++i) {
            CHECK_INT_EQ(static_cast<int>(mirror->currentFrameIdx[i]),
                         static_cast<int>(real.animationState.currentFrameIdx[i]));
            CHECK_INT_EQ(static_cast<int>(mirror->elapsedMs[i]),
                         static_cast<int>(real.animationState.elapsedMs[i]));
            const uint32_t expect =
                ayt::ay2d::resolveAnimatedTileId(real, i);
            CHECK_INT_EQ(static_cast<int>(TilemapAnimationRuntime::resolve(
                             *mirror, i)),
                         static_cast<int>(expect));
        }
        // Out-of-range id passes through on both sides.
        CHECK_INT_EQ(static_cast<int>(TilemapAnimationRuntime::resolve(
                         *mirror, 99u)),
                     99);
        CHECK_INT_EQ(static_cast<int>(ayt::ay2d::resolveAnimatedTileId(real, 99u)),
                     99);
    }

    // Sanity on the deterministic endpoint: entry 2 sat on frame 2
    // (50 + 50 + 50 + 150 + 700 = 1000ms walked), the zero-duration
    // entry accumulated 1000ms but never left frame 0, entry 9 (300ms
    // frames) sat on frame 0 with 100ms remainder.
    CHECK_INT_EQ(static_cast<int>(mirror->currentFrameIdx[2]), 2);
    CHECK_INT_EQ(static_cast<int>(mirror->elapsedMs[2]), 50);
    CHECK_INT_EQ(static_cast<int>(mirror->currentFrameIdx[5]), 0);
    CHECK_INT_EQ(static_cast<int>(mirror->elapsedMs[5]), 1000);
    CHECK_INT_EQ(static_cast<int>(mirror->currentFrameIdx[9]), 0);
    CHECK_INT_EQ(static_cast<int>(mirror->resolved[2]), 12);
    CHECK_INT_EQ(static_cast<int>(mirror->resolved[5]), 20);
    CHECK_INT_EQ(static_cast<int>(mirror->resolved[9]), 90);

    rt.clear();
}

// ─── #2 — resolve semantics: static ids and out-of-bounds. ─────────
TEST_CASE(cm5_runtime_resolve_static_and_out_of_bounds)
{
    ayt::resource::TilemapAsset asset;
    asset.create(2u, 2u, 16u, 16u, ayt::resource::TilemapPackMode::Narrow16,
                 0u, nullptr, 0u);
    const ayt::resource::TileAnimationFrame frames[] = {{10u, 100u}};
    asset.setAnimationEntry(2u, frames, 1u);

    TilemapAnimationRuntime& rt = TilemapAnimationRuntime::instance();
    rt.clear();
    TilemapAnimationRuntimeEntry* e = rt.ensure("x", asset);
    CHECK_NOT_NULL(e);
    CHECK_INT_EQ(static_cast<int>(e->table.size()), 3);  // maxId 2 + 1

    // Static ids stay themselves; out-of-bounds passes through.
    CHECK_INT_EQ(static_cast<int>(TilemapAnimationRuntime::resolve(*e, 0u)), 0);
    CHECK_INT_EQ(static_cast<int>(TilemapAnimationRuntime::resolve(*e, 1u)), 1);
    CHECK_INT_EQ(static_cast<int>(TilemapAnimationRuntime::resolve(*e, 99u)), 99);

    // Same asset via a different path => a different entry (per-path cache).
    TilemapAnimationRuntimeEntry* e2 = rt.ensure("y", asset);
    CHECK_NOT_NULL(e2);
    CHECK(e2 != e);

    rt.clear();
}

// ─── #3 — idempotent double-tick + reversed clock. ─────────────────
TEST_CASE(cm5_runtime_double_tick_idempotent_and_reversed_clock)
{
    ayt::resource::TilemapAsset asset = makeDeterministicAsset();

    TilemapAnimationRuntime& rt = TilemapAnimationRuntime::instance();
    rt.clear();
    TilemapAnimationRuntimeEntry* e = rt.ensure("x", asset);
    CHECK_NOT_NULL(e);

    rt.tick(*e, 100000);   // baseline (first call, no advance)
    CHECK(e->hasBeenTicked);
    CHECK_INT_EQ(static_cast<int>(e->currentFrameIdx[2]), 0);
    CHECK_INT_EQ(static_cast<int>(e->elapsedMs[2]), 0);

    // Second entity on the same path in the same frame: deltaUs == 0.
    rt.tick(*e, 100000);
    CHECK_INT_EQ(static_cast<int>(e->currentFrameIdx[2]), 0);
    CHECK_INT_EQ(static_cast<int>(e->elapsedMs[2]), 0);

    // +100ms -> exactly one frame (100ms) consumed.
    rt.tick(*e, 200000);
    CHECK_INT_EQ(static_cast<int>(e->currentFrameIdx[2]), 1);
    CHECK_INT_EQ(static_cast<int>(e->elapsedMs[2]), 0);

    // Reversed clock: delta clamped to 0 (no back-run), lastTickUs still
    // advances to the supplied value (AY2D mirror).
    rt.tick(*e, 150000);
    CHECK_INT_EQ(static_cast<int>(e->currentFrameIdx[2]), 1);
    CHECK_INT_EQ(static_cast<int>(e->elapsedMs[2]), 0);

    // +100ms from the clamped 150k baseline: frame 1 holds 150ms, so the
    // elapsed remainder accumulates (100 < 150) — no advance yet.
    rt.tick(*e, 250000);
    CHECK_INT_EQ(static_cast<int>(e->currentFrameIdx[2]), 1);
    CHECK_INT_EQ(static_cast<int>(e->elapsedMs[2]), 100);

    // +200ms more (350k): 100 + 200 = 300 >= 150 consumes frame 1, then
    // 150 >= 100 consumes frame 2, then 50 < 100 stops on frame 2.
    rt.tick(*e, 350000);
    CHECK_INT_EQ(static_cast<int>(e->currentFrameIdx[2]), 2);
    CHECK_INT_EQ(static_cast<int>(e->elapsedMs[2]), 50);

    rt.clear();
}

// ─── #4 — no animations => empty table, fully static. ──────────────
TEST_CASE(cm5_runtime_no_animations_static)
{
    ayt::resource::TilemapAsset asset;
    asset.create(2u, 2u, 16u, 16u, ayt::resource::TilemapPackMode::Narrow16,
                 0u, nullptr, 0u);

    TilemapAnimationRuntime& rt = TilemapAnimationRuntime::instance();
    rt.clear();
    TilemapAnimationRuntimeEntry* e = rt.ensure("x", asset);
    CHECK_NOT_NULL(e);
    CHECK(e->table.empty());
    CHECK(e->resolved.empty());

    rt.tick(*e, 100000);
    rt.tick(*e, 500000);
    CHECK_INT_EQ(static_cast<int>(TilemapAnimationRuntime::resolve(*e, 7u)), 7);
    CHECK_INT_EQ(static_cast<int>(TilemapAnimationRuntime::resolve(*e, 0u)), 0);

    rt.clear();
}

// ─── #5 — tick system wiring: entities share one per-path entry. ───
TEST_CASE(cm5_tick_system_shares_entry_per_path)
{
    std::string texPath, mapPath;
    CHECK(bakeAnimatedAssets(texPath, mapPath));

    World& world = World::instance();
    world.shutdown();
    world.initialize();

    Entity* a = world.createEntity();
    a->setName("waterA");
    CHECK_NOT_NULL(a);
    a->addComponent<Transform>();
    TilemapComponent* tmA = a->addComponent<TilemapComponent>();
    CHECK_NOT_NULL(tmA);
    tmA->tilemapPath = mapPath;
    tmA->atlasTexturePath = texPath;

    // Second entity on the SAME path — must share the runtime entry.
    Entity* b = world.createEntity();
    b->setName("waterB");
    CHECK_NOT_NULL(b);
    b->addComponent<Transform>();
    TilemapComponent* tmB = b->addComponent<TilemapComponent>();
    CHECK_NOT_NULL(tmB);
    tmB->tilemapPath = mapPath;
    tmB->atlasTexturePath = texPath;

    TilemapAnimationRuntime& rt = TilemapAnimationRuntime::instance();
    rt.clear();

    ayt::entity::TilemapAnimationTickSystem system;
    system.onStart();
    system.onUpdate(0.0f);   // first pass: ensure + baseline
    system.onUpdate(0.0f);   // second pass: idempotent no-op ticks

    TilemapAnimationRuntimeEntry* entry = rt.find(mapPath);
    CHECK_NOT_NULL(entry);
    CHECK(entry->hasBeenTicked);
    CHECK_INT_EQ(static_cast<int>(entry->table.size()), 3);  // maxId 2 + 1
    // Frame-0 snapshot is visible even though no frame advanced yet
    // (sub-ms deltas between the two onUpdate calls).
    CHECK_INT_EQ(static_cast<int>(entry->resolved[2]), 10);

    // Entity without a tilemap path is skipped without crashing.
    Entity* c = world.createEntity();
    c->setName("noPath");
    CHECK_NOT_NULL(c);
    c->addComponent<Transform>();
    system.onUpdate(0.0f);

    world.shutdown();
    rt.clear();
    std::remove(texPath.c_str());
    std::remove(mapPath.c_str());
}

// ─── #6 — sticky-Noop render closed loop: payload flips frames. ────
TEST_CASE(cm5_tilemap_render_animation_closed_loop)
{
    if (!shadercAvailable()) {
        std::cerr << "[AYEntity test] SKIP: shaderc not available.\n";
        return;
    }

    std::string texPath, mapPath;
    CHECK(bakeAnimatedAssets(texPath, mapPath));

    auto* rss = registerTestRenderer();
    CHECK_NOT_NULL(rss);
    if (rss == nullptr) {
        return;
    }

    World& world = World::instance();
    world.shutdown();
    world.initialize();
    Entity* entity = world.createEntity();
    entity->setName("animatedGround");
    CHECK_NOT_NULL(entity);
    entity->addComponent<Transform>();
    TilemapComponent* tm = entity->addComponent<TilemapComponent>();
    CHECK_NOT_NULL(tm);
    tm->tilemapPath         = mapPath;
    tm->atlasTexturePath    = texPath;
    tm->atlasTilesPerRow    = 8;
    tm->atlasTilesPerColumn = 4;

    // Register the animation runtime entry (the tick system would do this
    // at 460; driven manually here so the frame timestamps are exact).
    TilemapAnimationRuntime& rt = TilemapAnimationRuntime::instance();
    rt.clear();
    auto tilemap = ayt::resource::ResourceManager::instance()
                       .load<ayt::resource::ITilemap>(mapPath);
    CHECK_NOT_NULL(tilemap.get());
    TilemapAnimationRuntimeEntry* entry = rt.ensure(mapPath, *tilemap);
    CHECK_NOT_NULL(entry);

    ayt::entity::TilemapRenderSystem system;
    system.onStart();

    // t0: baseline tick — frame A (tile id 10) must render.
    rt.tick(*entry, 100000);
    {
        ayt::render::RenderScene scene;
        system.buildRenderScene(scene);
        CHECK_INT_EQ(static_cast<int>(scene.items().size()), 6);
        for (const ayt::render::DrawItem& item : scene.items()) {
            CHECK_NOT_NULL(item.payload);
            checkPayloadUvIsTileN(10u, *item.payload);
        }
    }

    // t0 + 70ms: frame B (tile id 11) must render.
    rt.tick(*entry, 170000);
    {
        ayt::render::RenderScene scene;
        system.buildRenderScene(scene);
        CHECK_INT_EQ(static_cast<int>(scene.items().size()), 6);
        for (const ayt::render::DrawItem& item : scene.items()) {
            CHECK_NOT_NULL(item.payload);
            checkPayloadUvIsTileN(11u, *item.payload);
        }
    }

    // t0 + 70ms + 60ms: wraps back to frame A (mod loop).
    rt.tick(*entry, 230000);
    {
        ayt::render::RenderScene scene;
        system.buildRenderScene(scene);
        CHECK_INT_EQ(static_cast<int>(scene.items().size()), 6);
        for (const ayt::render::DrawItem& item : scene.items()) {
            CHECK_NOT_NULL(item.payload);
            checkPayloadUvIsTileN(10u, *item.payload);
        }
    }

    rss->shutdown();
    unregisterTestRenderer();
    world.shutdown();
    rt.clear();
    std::remove(texPath.c_str());
    std::remove(mapPath.c_str());
}

TEST_SUITE_END
