// AYTest_2DRenderSystems.cpp — CM-3 (2026-08-11) acceptance cases.
//
// Covers the five 2D lane systems:
//   - priority wiring: 405/430/460/510/510 + the 510 tie-break order
//     (TilemapRenderSystem registered before SpriteRenderSystem —
//     registration order IS the scene-builder chain order).
//   - tilemap closed loop: sticky-Noop RendererSubSystem + real
//     .aytilemap/.aytex assets → buildRenderScene → one item per chunk,
//     tile UVs baked in the chunk mesh,
//     mesh/material valid, 3D-only entities excluded, full
//     beginFrame/render/endFrame → drawCalls == item count.
//   - sprite sorted submission + camera AABB cull (screen-out sprite
//     dropped, layer ordering ascending by packedSortKey).
//   - lazy-load failure paths: bad tilemap path and bad texture path
//     both produce zero items and no crash.
//
// Systems are driven directly via constructed instances (not
// bootstrapModule re-registration) — the AYTest_BlendSpaceSystem
// inline-test pattern, avoiding static-init duplicates across cases.

#include <AYEntity.h>
#include <AYEntity/EntityImpl.h>
#include <AYEntity/EntityModule.h>
#include <AYEntity/World.h>

#include <AYEntity/2DUvMath.h>
#include <AYEntity/OrthoCameraUpdateSystem.h>
#include <AYRenderer/RendererSubSystem.h>
#include <AYEntity/SpriteRenderSystem.h>
#include <AYGameLoop/SubSystemRegistry.h>
#include <AYEntity/TilemapAnimationTickSystem.h>
#include <AYEntity/TilemapRenderSystem.h>
#include <AYEntity/TilemapStreamingSystem.h>

#include <AYEntity/components/OrthoCameraComponent.h>
#include <AYEntity/components/SpriteComponent.h>
#include <AYEntity/components/TilemapComponent.h>
#include <AYEntity/components/TransformComponent.h>

#include <AYResource/assetsImpl/Texture.h>
#include <AYResource/assetsImpl/TilemapAsset.h>

#include <AYIO/File.h>
#include <AYTest.h>

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
using ayt::entity::OrthoCameraComponent;
using ayt::entity::SpriteComponent;
using ayt::entity::TilemapComponent;
using ayt::entity::Transform;
using ayt::entity::World;

namespace
{

int g_sentinelWindowHandle = 0;

std::string tempPath(const char* name)
{
    std::string path = std::string("test_cm3_2d_") + name;
#ifdef _WIN32
    char tempDir[MAX_PATH] = {};
    const DWORD len = GetTempPathA(MAX_PATH, tempDir);
    if (len > 0 && len < MAX_PATH) {
        path = std::string(tempDir) + name;
    }
#endif
    return path;
}

bool writeBinaryFile(const std::string& path, const std::vector<ayt::resource::UInt8>& data)
{
    ayt::io::File file(path, ayt::io::File::Mode::BinaryWrite);
    if (!file.isOpen()) {
        return false;
    }
    return file.write(data.data(), data.size()) == data.size();
}

void removeFile(const std::string& path)
{
    std::remove(path.c_str());
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
// the systems early-return with zero items). The closed-loop cases
// must therefore register one for real: the registry owns it, and
// unregistering deletes it. initialize() failure unregisters so the
// registry never holds a half-built subsystem.
ayt::render::RendererSubSystem* registerTestRenderer()
{
    ayt::render::RendererSubSystem::setWindowProvider({});
    ayt::render::RendererSubSystem::setBootstrapBackend(ayt::render::Backend::Noop);
    ayt::render::RendererSubSystem::setBootstrapWindow(&g_sentinelWindowHandle,
                                                       800, 600);
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

// Write a real .aytex atlas (8x8 checkerboard) + a real .aytilemap
// (2 cols x 3 rows, all tiles = defaultTileId 5) to temp files.
// Returns false if any write failed (test aborts).
bool bakeAssets(std::string& outTexPath, std::string& outMapPath)
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
               /*defaultTileId=*/5u, nullptr, 0u);
    std::vector<ayt::resource::UInt8> mapBinary;
    if (!map.saveToBinary(mapBinary)) {
        return false;
    }
    outMapPath = tempPath("map.aytilemap");
    return writeBinaryFile(outMapPath, mapBinary);
}

// Tile 5's UV in a 8x4 grid of 32x32 tiles (dense, gutter 0) — the
// value every submitted tilemap payload must carry.
void checkPayloadUvIsTile5(const ayt::render::DrawPayload2D& payload)
{
    ayt::entity::AtlasGridDesc grid;
    grid.tilesPerRow      = 8;
    grid.tilesPerColumn   = 4;
    grid.tileWidthTexels  = 32;
    grid.tileHeightTexels = 32;
    const ayt::entity::TileUvQuad uv = ayt::entity::tileUvQuad(5u, grid);
    CHECK_FLOAT_EQ(payload.sourceRectMin.x, uv.uMin, 1e-6f);
    CHECK_FLOAT_EQ(payload.sourceRectMin.y, uv.vMin, 1e-6f);
    CHECK_FLOAT_EQ(payload.sourceRectMax.x, uv.uMax, 1e-6f);
    CHECK_FLOAT_EQ(payload.sourceRectMax.y, uv.vMax, 1e-6f);
}

} // namespace

TEST_SUITE(AYEntity2DRenderSystems)

// ─── #1 — priority wiring (mirrors sm_system_priority test). ──────
TEST_CASE(cm3_2d_systems_priority_wiring)
{
    World& world = World::instance();
    world.shutdown();
    world.initialize();

    CHECK(ayt::entity::OrthoCameraUpdateSystem::kPriority == 405);
    CHECK(ayt::entity::TilemapStreamingSystem::kPriority == 430);
    CHECK(ayt::entity::TilemapAnimationTickSystem::kPriority == 460);
    CHECK(ayt::entity::TilemapRenderSystem::kPriority == 510);
    CHECK(ayt::entity::SpriteRenderSystem::kPriority == 510);
    CHECK(ayt::entity::OrthoCameraUpdateSystem::kPriority
          < ayt::entity::TilemapStreamingSystem::kPriority);
    CHECK(ayt::entity::TilemapStreamingSystem::kPriority
          < ayt::entity::TilemapAnimationTickSystem::kPriority);
    CHECK(ayt::entity::TilemapAnimationTickSystem::kPriority
          < ayt::entity::TilemapRenderSystem::kPriority);

    // Sanity: confirm via world introspection that the registered
    // systems report the expected priorities AND that the two 510
    // systems keep registration order (chain order = registration
    // order; TilemapRenderSystem before SpriteRenderSystem).
    ayt::entity::bootstrapModule();
    int idxTilemap = -1, idxSprite = -1;
    bool sawCamera = false, sawStreaming = false, sawTick = false;
    for (size_t i = 0; i < world.systemCount(); ++i) {
        const char* name = world.getSystemNameAt(i);
        const int32_t p  = world.getSystemPriorityAt(i);
        if (name == nullptr) {
            continue;
        }
        if (std::strcmp(name, "OrthoCameraUpdateSystem") == 0) {
            CHECK(p == 405);
            sawCamera = true;
        } else if (std::strcmp(name, "TilemapStreamingSystem") == 0) {
            CHECK(p == 430);
            sawStreaming = true;
        } else if (std::strcmp(name, "TilemapAnimationTickSystem") == 0) {
            CHECK(p == 460);
            sawTick = true;
        } else if (std::strcmp(name, "TilemapRenderSystem") == 0) {
            CHECK(p == 510);
            idxTilemap = static_cast<int>(i);
        } else if (std::strcmp(name, "SpriteRenderSystem") == 0) {
            CHECK(p == 510);
            idxSprite = static_cast<int>(i);
        }
    }
    CHECK(sawCamera);
    CHECK(sawStreaming);
    CHECK(sawTick);
    CHECK(idxTilemap >= 0);
    CHECK(idxSprite >= 0);
    CHECK(idxTilemap < idxSprite);

    world.shutdown();
}

TEST_CASE(ortho_camera_is_recorded_as_an_independent_scene_overlay)
{
    World& world = World::instance();
    world.shutdown();
    world.initialize();

    Entity* entity = world.createEntity();
    CHECK_NOT_NULL(entity);
    Transform* transform = entity->addComponent<Transform>();
    CHECK_NOT_NULL(transform);
    transform->position = {17.0f, -9.0f, 0.0f};
    OrthoCameraComponent* camera = entity->addComponent<OrthoCameraComponent>();
    CHECK_NOT_NULL(camera);
    camera->viewSize = 24.0f;
    camera->viewportAspect = 4.0f / 3.0f;
    camera->layerMask = 0x0000002Du;
    camera->isPrimary = true;

    const auto expectedView = camera->viewMatrix(*transform);
    const auto expectedProjection = camera->projectionMatrix();
    ayt::render::RenderScene scene;
    ayt::entity::OrthoCameraUpdateSystem system;
    system.buildCamera(scene);

    CHECK(scene.hasOverlayCamera2D());
    CHECK(scene.overlayCamera2D().layerMask == 0x0000002Du);
    CHECK_FLOAT_EQ(scene.overlayCamera2D().view.row[0].w,
                   expectedView.row[0].w, 1e-6f);
    CHECK_FLOAT_EQ(scene.overlayCamera2D().view.row[1].w,
                   expectedView.row[1].w, 1e-6f);
    CHECK_FLOAT_EQ(scene.overlayCamera2D().projection.row[0].x,
                   expectedProjection.row[0].x, 1e-6f);
    CHECK_FLOAT_EQ(scene.overlayCamera2D().projection.row[1].y,
                   expectedProjection.row[1].y, 1e-6f);

    world.shutdown();
}

TEST_CASE(ortho_camera_selection_uses_active_priority_and_viewport_aspect)
{
    auto* rss = registerTestRenderer();
    CHECK_NOT_NULL(rss);
    if (rss == nullptr) return;
    rss->setViewportRect(0, 0, 400, 400);

    World& world = World::instance();
    world.shutdown();
    world.initialize();

    Entity* low = world.createEntity();
    Transform* lowTransform = low->addComponent<Transform>();
    lowTransform->position = {2.0f, 0.0f, 0.0f};
    OrthoCameraComponent* lowCamera =
        low->addComponent<OrthoCameraComponent>();
    lowCamera->priority = 1;

    Entity* high = world.createEntity();
    Transform* highTransform = high->addComponent<Transform>();
    highTransform->position = {8.0f, 0.0f, 0.0f};
    OrthoCameraComponent* highCamera =
        high->addComponent<OrthoCameraComponent>();
    highCamera->priority = 9;
    highCamera->viewSize = 600.0f;
    highCamera->designWidth = 960.0f;
    highCamera->designHeight = 600.0f;
    highCamera->aspectPolicy = 1;

    Entity* inactive = world.createEntity();
    inactive->addComponent<Transform>();
    OrthoCameraComponent* inactiveCamera =
        inactive->addComponent<OrthoCameraComponent>();
    inactiveCamera->priority = 100;
    inactiveCamera->active = false;

    ayt::render::RenderScene scene;
    ayt::entity::OrthoCameraUpdateSystem system;
    system.buildCamera(scene);

    CHECK(scene.hasOverlayCamera2D());
    const auto expectedView = highCamera->viewMatrix(*highTransform);
    const auto expectedProjection = highCamera->projectionMatrix(1.0f);
    CHECK_FLOAT_EQ(scene.overlayCamera2D().view.row[0].w,
                   expectedView.row[0].w, 1e-6f);
    CHECK_FLOAT_EQ(scene.overlayCamera2D().projection.row[0].x,
                   expectedProjection.row[0].x, 1e-6f);
    CHECK_FLOAT_EQ(scene.overlayCamera2D().projection.row[1].y,
                   expectedProjection.row[1].y, 1e-6f);

    rss->shutdown();
    unregisterTestRenderer();
    world.shutdown();
}

// ─── #2 — tilemap render closed loop (sticky-Noop). ───────────────
TEST_CASE(cm3_tilemap_render_closed_loop)
{
    if (!shadercAvailable()) {
        std::cerr << "[AYEntity test] SKIP: shaderc not available.\n";
        return;
    }

    std::string texPath, mapPath;
    CHECK(bakeAssets(texPath, mapPath));

    auto* rss = registerTestRenderer();
    CHECK_NOT_NULL(rss);
    if (rss == nullptr) {
        return;
    }
    World::instance().initialize();
    Entity* entity = World::instance().createEntity();
    entity->setName("ground");
    CHECK_NOT_NULL(entity);
    Transform* transform = entity->addComponent<Transform>();
    CHECK_NOT_NULL(transform);
    TilemapComponent* tm = entity->addComponent<TilemapComponent>();
    CHECK_NOT_NULL(tm);
    tm->tilemapPath        = mapPath;
    tm->atlasTexturePath   = texPath;
    tm->atlasTilesPerRow   = 8;
    tm->atlasTilesPerColumn = 4;
    tm->layer              = 2;
    tm->sortingKey         = 10;

    // 3D-only decoy must NOT leak into the 2D lane.
    Entity* decoy = World::instance().createEntity();
    decoy->setName("decoy");
    CHECK_NOT_NULL(decoy);
    decoy->addComponent<Transform>();

    ayt::entity::TilemapRenderSystem system;
    system.onStart();
    ayt::render::RenderScene scene;
    system.buildRenderScene(scene);

    // 2 cols x 3 rows fit in the default 16x16 chunk: one item; decoy excluded.
    CHECK_INT_EQ(static_cast<int>(scene.items().size()), 1);

    for (const ayt::render::DrawItem& item : scene.items()) {
        CHECK_NOT_NULL(item.payload);
        CHECK_TRUE(item.mesh.isValid());
        CHECK_TRUE(item.material.isValid());
        CHECK(item.shadowFlags == ayt::render::ShadowFlags::None);
        if (item.payload == nullptr) {
            continue;
        }
        CHECK(item.payload->packedSortKey == ayt::entity::drawSortKey(2, 10));
        CHECK_FLOAT_EQ(item.payload->sourceRectMin.x, 0.0f, 1e-6f);
        CHECK_FLOAT_EQ(item.payload->sourceRectMin.y, 0.0f, 1e-6f);
        CHECK_FLOAT_EQ(item.payload->sourceRectMax.x, 1.0f, 1e-6f);
        CHECK_FLOAT_EQ(item.payload->sourceRectMax.y, 1.0f, 1e-6f);
        CHECK_FLOAT_EQ(item.payload->atlasTexelSize.x, 1.0f / 256.0f, 1e-6f);
        CHECK_FLOAT_EQ(item.payload->atlasTexelSize.y, 1.0f / 128.0f, 1e-6f);
    }

    // Full pipeline: beginFrame/render/endFrame must submit exactly
    // the one chunk item (no double-submit — Opaque blend lane only).
    rss->renderer().beginFrame({});
    rss->renderer().render(scene);
    rss->renderer().endFrame();
    CHECK(rss->renderer().getFrameStats().drawCalls == 1u);
    CHECK_INT_EQ(system.lastFrameStats().cellsVisited, 6u);
    CHECK_INT_EQ(system.lastFrameStats().visibleChunks, 1u);
    CHECK_INT_EQ(system.lastFrameStats().drawItemsEmitted, 1u);
    CHECK_INT_EQ(system.lastFrameStats().chunkMeshesBuilt, 1u);
    CHECK_INT_EQ(system.lastFrameStats().residentChunkMeshes, 1u);

    rss->shutdown();
    unregisterTestRenderer();
    World::instance().shutdown();
    removeFile(texPath);
    removeFile(mapPath);
}

TEST_CASE(world_lit_tilemap_uses_baked_uv_and_deferred_route)
{
    if (!shadercAvailable()) {
        std::cerr << "[AYEntity test] SKIP: shaderc not available.\n";
        return;
    }

    std::string texPath, mapPath;
    CHECK(bakeAssets(texPath, mapPath));

    auto* rss = registerTestRenderer();
    CHECK_NOT_NULL(rss);
    if (rss == nullptr) {
        return;
    }

    World::instance().initialize();
    Entity* entity = World::instance().createEntity();
    CHECK_NOT_NULL(entity);
    Transform* transform = entity->addComponent<Transform>();
    CHECK_NOT_NULL(transform);
    TilemapComponent* tm = entity->addComponent<TilemapComponent>();
    CHECK_NOT_NULL(tm);
    tm->tilemapPath = mapPath;
    tm->atlasTexturePath = texPath;
    tm->normalTexturePath = texPath;
    tm->roughnessTexturePath = texPath;
    tm->emissiveTexturePath = texPath;
    tm->atlasTilesPerRow = 8;
    tm->atlasTilesPerColumn = 4;
    tm->samplingQuality = 3;
    tm->renderDomain = 1;
    tm->roughness = 0.6f;
    tm->alphaCutoff = 0.35f;

    ayt::entity::TilemapRenderSystem system;
    system.onStart();
    ayt::render::RenderScene scene;
    system.buildRenderScene(scene);

    CHECK_INT_EQ(static_cast<int>(scene.items().size()), 1);
    CHECK_NOT_NULL(scene.items()[0].payload);
    if (scene.items()[0].payload != nullptr) {
        CHECK(scene.items()[0].payload->renderDomain
              == ayt::render::RenderDomain2D::WorldLit);
        CHECK(scene.items()[0].payload->uvMapping
              == ayt::render::UvMapping2D::BakedAtlas);
        CHECK(scene.items()[0].payload->samplingQuality
              == ayt::render::TilemapSamplingQuality::Tap9);
    }
    CHECK(ayt::render::castsShadow(scene.items()[0].shadowFlags));
    CHECK(ayt::render::receivesShadow(scene.items()[0].shadowFlags));

    tm->castShadow = false;
    scene.clear();
    system.buildRenderScene(scene);
    CHECK_INT_EQ(static_cast<int>(scene.items().size()), 1);
    CHECK_FALSE(ayt::render::castsShadow(scene.items()[0].shadowFlags));
    CHECK(ayt::render::receivesShadow(scene.items()[0].shadowFlags));
    tm->castShadow = true;

    // WorldLit is intentionally absent from the forward overlay lane.
    rss->renderer().beginFrame({});
    rss->renderer().render(scene);
    rss->renderer().endFrame();
    CHECK(rss->renderer().getFrameStats().drawCalls == 0u);

    // The same scene item is routed to the GBuffer when Deferred is mounted;
    // the Noop backend intentionally does not issue GBuffer GPU draws, so the
    // pipeline contract is the observable assertion here.
    rss->renderer().configurePipeline(
        ayt::render::RenderPipelineDesc::makeDeferred());
    CHECK(rss->renderer().pipelineDesc().isDeferred());
    CHECK(rss->renderer().pipelineDesc().contains(
        ayt::render::RenderPassSlot::GBuffer));

    // Perspective culling is evaluated in the tilemap's complete world
    // transform, independently from the overlay camera rectangle.
    transform->position.x = 10000.0f;
    scene.clear();
    system.buildRenderScene(scene);
    CHECK_TRUE(scene.items().empty());
    CHECK_INT_EQ(system.lastFrameStats().visibleChunks, 0u);

    rss->shutdown();
    unregisterTestRenderer();
    World::instance().shutdown();
    removeFile(texPath);
    removeFile(mapPath);
}

// ─── #3 — sprite sorted submission + camera AABB cull. ────────────
TEST_CASE(cm3_sprite_render_sorted_and_culled)
{
    if (!shadercAvailable()) {
        std::cerr << "[AYEntity test] SKIP: shaderc not available.\n";
        return;
    }

    std::string texPath, mapPath;
    CHECK(bakeAssets(texPath, mapPath));

    auto* rss = registerTestRenderer();
    CHECK_NOT_NULL(rss);
    if (rss == nullptr) {
        return;
    }
    rss->setViewportRect(0, 0, 600, 600);

    World::instance().initialize();

    // Primary camera at origin, viewSize 10, square viewport =>
    // world rect ±5 x ±5.
    Entity* camEntity = World::instance().createEntity();
    camEntity->setName("cam");
    CHECK_NOT_NULL(camEntity);
    CHECK_NOT_NULL(camEntity->addComponent<Transform>());
    OrthoCameraComponent* cam = camEntity->addComponent<OrthoCameraComponent>();
    CHECK_NOT_NULL(cam);
    cam->viewSize       = 10.0f;
    cam->viewportAspect = 1.0f;
    cam->isPrimary      = true;

    // A: on-screen (origin), layer 0 / key 5.
    Entity* a = World::instance().createEntity();
    a->setName("spriteA");
    CHECK_NOT_NULL(a->addComponent<Transform>());
    SpriteComponent* sa = a->addComponent<SpriteComponent>();
    sa->texturePath = texPath;
    sa->layer = 0;
    sa->sortingKey = 5;
    // C: on-screen, layer 1 / key 3 — must sort AFTER A.
    Entity* c = World::instance().createEntity();
    c->setName("spriteC");
    Transform* tc = c->addComponent<Transform>();
    CHECK_NOT_NULL(tc);
    tc->position = {1.0f, 1.0f, 0.0f};
    SpriteComponent* sc = c->addComponent<SpriteComponent>();
    sc->texturePath = texPath;
    sc->layer       = 1;
    sc->sortingKey  = 3;
    // B: far off-screen — must be culled.
    Entity* b = World::instance().createEntity();
    b->setName("spriteB");
    Transform* tb = b->addComponent<Transform>();
    CHECK_NOT_NULL(tb);
    tb->position = {100.0f, 0.0f, 0.0f};
    SpriteComponent* sb = b->addComponent<SpriteComponent>();
    sb->texturePath = texPath;

    ayt::entity::SpriteRenderSystem system;
    system.onStart();
    ayt::render::RenderScene scene;
    system.buildRenderScene(scene);

    CHECK_INT_EQ(static_cast<int>(scene.items().size()), 2);

    // Ascending packedSortKey: A (layer 0) before C (layer 1).
    const uint32_t first  = scene.items()[0].payload->packedSortKey;
    const uint32_t second = scene.items()[1].payload->packedSortKey;
    CHECK(first == ayt::entity::drawSortKey(0, 5));
    CHECK(second == ayt::entity::drawSortKey(1, 3));
    CHECK(first < second);

    // At zoom 2 the square renderer viewport exposes ±2.5 in X.
    // Moving C to x=4 proves culling uses the reduced world extent.
    tc->position.x = 4.0f;
    cam->zoom = 2.0f;
    scene.clear();
    system.buildRenderScene(scene);
    CHECK_INT_EQ(static_cast<int>(scene.items().size()), 1);
    CHECK(scene.items()[0].payload->packedSortKey
          == ayt::entity::drawSortKey(0, 5));

    // Culled sprite must have left a slot in the payload buffer only
    // for the two submitted items (no dangling pointers).
    for (const ayt::render::DrawItem& item : scene.items()) {
        CHECK_NOT_NULL(item.payload);
        CHECK(item.shadowFlags == ayt::render::ShadowFlags::None);
    }

    rss->shutdown();
    unregisterTestRenderer();
    World::instance().shutdown();
    removeFile(texPath);
    removeFile(mapPath);
}

TEST_CASE(world_lit_sprite_authors_shadow_cast_control)
{
    if (!shadercAvailable()) {
        std::cerr << "[AYEntity test] SKIP: shaderc not available.\n";
        return;
    }

    std::string texPath, mapPath;
    CHECK(bakeAssets(texPath, mapPath));
    auto* rss = registerTestRenderer();
    CHECK_NOT_NULL(rss);
    if (rss == nullptr) {
        return;
    }

    World::instance().initialize();
    Entity* entity = World::instance().createEntity();
    CHECK_NOT_NULL(entity);
    CHECK_NOT_NULL(entity->addComponent<Transform>());
    SpriteComponent* sprite = entity->addComponent<SpriteComponent>();
    CHECK_NOT_NULL(sprite);
    sprite->texturePath = texPath;
    sprite->renderDomain = 1;

    ayt::entity::SpriteRenderSystem system;
    system.onStart();
    ayt::render::RenderScene scene;
    system.buildRenderScene(scene);
    CHECK_INT_EQ(static_cast<int>(scene.items().size()), 1);
    CHECK(ayt::render::castsShadow(scene.items()[0].shadowFlags));
    CHECK(ayt::render::receivesShadow(scene.items()[0].shadowFlags));

    sprite->castShadow = false;
    scene.clear();
    system.buildRenderScene(scene);
    CHECK_INT_EQ(static_cast<int>(scene.items().size()), 1);
    CHECK_FALSE(ayt::render::castsShadow(scene.items()[0].shadowFlags));
    CHECK(ayt::render::receivesShadow(scene.items()[0].shadowFlags));

    rss->shutdown();
    unregisterTestRenderer();
    World::instance().shutdown();
    removeFile(texPath);
    removeFile(mapPath);
}

// ─── #4 — lazy-load failure: bad tilemap path => zero items. ──────
TEST_CASE(cm3_tilemap_lazy_load_failure_skips)
{
    std::string texPath, mapPath;
    CHECK(bakeAssets(texPath, mapPath));

    auto* rss = registerTestRenderer();
    CHECK_NOT_NULL(rss);
    if (rss == nullptr) {
        return;
    }

    World::instance().initialize();
    Entity* entity = World::instance().createEntity();
    entity->setName("broken");
    CHECK_NOT_NULL(entity);
    entity->addComponent<Transform>();
    TilemapComponent* tm = entity->addComponent<TilemapComponent>();
    CHECK_NOT_NULL(tm);
    tm->tilemapPath      = "tilemaps/does_not_exist.aytilemap";
    tm->atlasTexturePath = texPath;

    ayt::entity::TilemapRenderSystem system;
    system.onStart();
    ayt::render::RenderScene scene;
    system.buildRenderScene(scene);

    // Load fails => marker skip => no items, no crash.
    CHECK_INT_EQ(static_cast<int>(scene.items().size()), 0);

    rss->shutdown();
    unregisterTestRenderer();
    World::instance().shutdown();
    removeFile(texPath);
    removeFile(mapPath);
}

// ─── #5 — lazy-load failure: bad texture path => zero items. ──────
TEST_CASE(cm3_sprite_texture_load_failure_skips)
{
    std::string texPath, mapPath;
    CHECK(bakeAssets(texPath, mapPath));

    auto* rss = registerTestRenderer();
    CHECK_NOT_NULL(rss);
    if (rss == nullptr) {
        return;
    }

    World::instance().initialize();
    Entity* entity = World::instance().createEntity();
    entity->setName("brokenSprite");
    CHECK_NOT_NULL(entity);
    CHECK_NOT_NULL(entity->addComponent<Transform>());
    SpriteComponent* sp = entity->addComponent<SpriteComponent>();
    CHECK_NOT_NULL(sp);
    sp->texturePath = "textures/does_not_exist.aytex";

    ayt::entity::SpriteRenderSystem system;
    system.onStart();
    ayt::render::RenderScene scene;
    system.buildRenderScene(scene);

    CHECK_INT_EQ(static_cast<int>(scene.items().size()), 0);

    rss->shutdown();
    unregisterTestRenderer();
    World::instance().shutdown();
    removeFile(texPath);
    removeFile(mapPath);
}

TEST_SUITE_END
