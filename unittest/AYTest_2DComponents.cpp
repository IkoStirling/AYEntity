// AYTest_2DComponents.cpp — CM-3 (2026-08-11) acceptance cases.
//
// Covers the three 2D lane components:
//   - ctor defaults for every field (AY_PROPERTY emits bare
//     declarations — uninitialized members would read garbage, so the
//     ctor assignment contract is pinned here).
//   - isValid() semantics (path non-empty).
//   - .ayscene save/load round-trip through the ComponentFactory
//     wire table (a missing kEntries[] row would silently drop
//     components on load — this case catches it).
//   - math cross-asserts: AY2DUvMath.h (tileUvQuad / cellCenterWorld)
//     and OrthoCameraComponent::viewMatrix()/projectionMatrix() are
//     mirrored from AY2D — every value is compared against the REAL
//     header-only AY2D helpers (zero link dependency, drift breaks
//     this build).

#include <AYEntity.h>
#include <AYEntityImpl.h>
#include <AYEntityModule.h>
#include <AYWorld.h>
#include <AYComponentFactory.h>
#include <AYSceneSerializer.h>

#include <AY2DUvMath.h>
#include <components/AYOrthoCameraComponent.h>
#include <components/AYSpriteComponent.h>
#include <components/AYTilemapComponent.h>

// AY2D header-only math for cross-asserts (test-only include dir).
#include <AYAtlasDesc.h>
#include <AYOrthographicCamera.h>
#include <AYTileMath.h>
#include <AYTileSamplerUV.h>

#include <AYMath/MathTypes.h>
#include <AYTest.h>

#include <cstdio>
#include <cstring>

using ayt::entity::Entity;
using ayt::entity::OrthoCameraComponent;
using ayt::entity::SpriteComponent;
using ayt::entity::TilemapComponent;
using ayt::entity::World;

namespace
{

void checkMatrixEq(const ayt::math::Float4x4& a, const ayt::math::Float4x4& b)
{
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) {
            CHECK_FLOAT_EQ(a.row[r][c], b.row[r][c], 1e-5f);
        }
    }
}

} // namespace

TEST_SUITE(AYEntity2DComponents)

// ─── #1 — TilemapComponent ctor defaults. ─────────────────────────
TEST_CASE(cm3_tilemap_component_defaults)
{
    TilemapComponent c;
    CHECK_TRUE(c.tilemapPath.empty());
    CHECK_TRUE(c.atlasTexturePath.empty());
    CHECK_INT_EQ(c.atlasTilesPerRow, 1);
    CHECK_INT_EQ(c.atlasTilesPerColumn, 1);
    CHECK_INT_EQ(c.layer, 0);
    CHECK_INT_EQ(c.sortingKey, 0);
    CHECK_TRUE(c.visible);
    CHECK_TRUE(std::strcmp(c.getName(), "TilemapComponent") == 0);
}

// ─── #2 — SpriteComponent ctor defaults. ──────────────────────────
TEST_CASE(cm3_sprite_component_defaults)
{
    SpriteComponent c;
    CHECK_TRUE(c.texturePath.empty());
    CHECK_FLOAT_EQ(c.position.x, 0.0f, 0.0f);
    CHECK_FLOAT_EQ(c.position.y, 0.0f, 0.0f);
    CHECK_FLOAT_EQ(c.position.z, 0.0f, 0.0f);
    CHECK_FLOAT_EQ(c.rotationZ, 0.0f, 0.0f);
    CHECK_FLOAT_EQ(c.scaleX, 1.0f, 0.0f);
    CHECK_FLOAT_EQ(c.scaleY, 1.0f, 0.0f);
    CHECK_FLOAT_EQ(c.sourceRectMin.x, 0.0f, 0.0f);
    CHECK_FLOAT_EQ(c.sourceRectMin.y, 0.0f, 0.0f);
    CHECK_FLOAT_EQ(c.sourceRectMax.x, 1.0f, 0.0f);
    CHECK_FLOAT_EQ(c.sourceRectMax.y, 1.0f, 0.0f);
    CHECK_FLOAT_EQ(c.colorRGBA.x, 1.0f, 0.0f);
    CHECK_FLOAT_EQ(c.colorRGBA.w, 1.0f, 0.0f);
    CHECK_INT_EQ(c.flip, 0);
    CHECK_INT_EQ(c.layer, 0);
    CHECK_INT_EQ(c.sortingKey, 0);
    CHECK_TRUE(c.visible);
}

// ─── #3 — OrthoCameraComponent ctor defaults. ─────────────────────
TEST_CASE(cm3_orthocamera_component_defaults)
{
    OrthoCameraComponent c;
    CHECK_FLOAT_EQ(c.positionX, 0.0f, 0.0f);
    CHECK_FLOAT_EQ(c.positionY, 0.0f, 0.0f);
    CHECK_FLOAT_EQ(c.zoom, 1.0f, 0.0f);
    CHECK_FLOAT_EQ(c.rotationRadians, 0.0f, 0.0f);
    CHECK_FLOAT_EQ(c.viewSize, 1.0f, 0.0f);
    CHECK_FLOAT_EQ(c.viewportAspect, 16.0f / 9.0f, 1e-6f);
    CHECK_FLOAT_EQ(c.nearZ, -1.0f, 0.0f);
    CHECK_FLOAT_EQ(c.farZ, 1.0f, 0.0f);
    CHECK(c.layerMask == 0xFFFFFFFFu);
    CHECK_TRUE(c.isPrimary);
}

// ─── #4 — isValid() semantics. ────────────────────────────────────
TEST_CASE(cm3_component_isvalid_semantics)
{
    TilemapComponent tm;
    CHECK_FALSE(tm.isValid());
    tm.setTilemap("tilemaps/g.aytilemap");
    CHECK_TRUE(tm.isValid());

    SpriteComponent sp;
    CHECK_FALSE(sp.isValid());
    sp.setTexture("textures/s.aytex");
    CHECK_TRUE(sp.isValid());
}

// ─── #5 — tileUvQuad vs real AY2D tileUV (dense atlas, gutter 0). ─
TEST_CASE(cm3_tile_uv_matches_ay2d)
{
    using ayt::entity::AtlasGridDesc;
    using ayt::entity::TileUvQuad;

    // AYEntity-side dense-atlas descriptor (atlas extent implied).
    AtlasGridDesc grid;
    grid.tilesPerRow      = 8;
    grid.tilesPerColumn   = 4;
    grid.tileWidthTexels  = 16;
    grid.tileHeightTexels = 16;

    // AY2D-side reference: 128x64 atlas, same grid, gutter 0.
    ayt::ay2d::AtlasDesc desc;
    desc.atlasWidthTexels  = 128;
    desc.atlasHeightTexels = 64;
    desc.tileWidthTexels   = 16;
    desc.tileHeightTexels  = 16;
    desc.tilesPerRow       = 8;
    desc.tilesPerColumn    = 4;
    desc.gutter            = 0;

    // Samples covering every row + the last column (tile-id 31).
    const uint32_t samples[] = {0u, 1u, 5u, 7u, 8u, 15u, 16u, 30u, 31u};
    for (uint32_t tileId : samples) {
        const TileUvQuad uv  = ayt::entity::tileUvQuad(tileId, grid);
        const ayt::ay2d::TileUV ref = ayt::ay2d::tileUV(tileId, desc);
        CHECK_FLOAT_EQ(uv.uMin, ref.uMin, 1e-6f);
        CHECK_FLOAT_EQ(uv.uMax, ref.uMax, 1e-6f);
        CHECK_FLOAT_EQ(uv.vMin, ref.vMin, 1e-6f);
        CHECK_FLOAT_EQ(uv.vMax, ref.vMax, 1e-6f);
    }
}

// ─── #6 — degenerate tileUvQuad inputs yield all-zeros. ───────────
TEST_CASE(cm3_tile_uv_degenerate_inputs)
{
    using ayt::entity::AtlasGridDesc;
    const AtlasGridDesc bad;  // all zeros
    const ayt::entity::TileUvQuad zero = ayt::entity::tileUvQuad(3u, bad);
    CHECK_FLOAT_EQ(zero.uMin, 0.0f, 0.0f);
    CHECK_FLOAT_EQ(zero.uMax, 0.0f, 0.0f);
    CHECK_FLOAT_EQ(zero.vMin, 0.0f, 0.0f);
    CHECK_FLOAT_EQ(zero.vMax, 0.0f, 0.0f);
}

// ─── #7 — cellCenterWorld vs real AY2D cellToWorld. ───────────────
TEST_CASE(cm3_cell_center_matches_ay2d)
{
    const float w = 32.0f;
    const float h = 16.0f;
    const uint32_t cells[][2] = {
        {0u, 0u}, {1u, 0u}, {0u, 2u}, {7u, 3u}, {4u, 1u},
    };
    for (const auto& c : cells) {
        const ayt::math::FVector2 mine =
            ayt::entity::cellCenterWorld(c[0], c[1], w, h);
        const ayt::math::FVector2 ref = ayt::ay2d::cellToWorld(
            ayt::ay2d::TileCoord{static_cast<int32_t>(c[0]),
                                 static_cast<int32_t>(c[1])},
            ayt::math::FVector2{0.0f, 0.0f}, w, h);
        CHECK_FLOAT_EQ(mine.x, ref.x, 1e-6f);
        CHECK_FLOAT_EQ(mine.y, ref.y, 1e-6f);
    }
}

// ─── #8 — drawSortKey packing (design.md §7.4). ───────────────────
TEST_CASE(cm3_draw_sort_key_packing)
{
    CHECK(ayt::entity::drawSortKey(0, 0) == 0u);
    CHECK(ayt::entity::drawSortKey(0, 1) == 1u);
    CHECK(ayt::entity::drawSortKey(1, 0) == 0x01000000u);
    // layer wins over sortingKey (high byte).
    CHECK(ayt::entity::drawSortKey(1, 0) > ayt::entity::drawSortKey(0, 0xFFFFFF));
    // Negative sortingKey wraps into the low 24 bits.
    CHECK(ayt::entity::drawSortKey(0, -1) == 0xFFFFFFu);
    // Negative layer wraps into the byte.
    CHECK(ayt::entity::drawSortKey(-1, 0) == 0xFF000000u);
}

// ─── #9 — ortho camera matrices vs real AY2D camera. ──────────────
TEST_CASE(cm3_orthocamera_matrix_matches_ay2d)
{
    OrthoCameraComponent mine;
    mine.positionX       = 10.0f;
    mine.positionY       = -4.0f;
    mine.zoom            = 2.0f;
    mine.rotationRadians = 0.25f;
    mine.viewSize        = 6.0f;
    mine.viewportAspect  = 16.0f / 9.0f;
    mine.nearZ           = -2.0f;
    mine.farZ            = 2.0f;

    ayt::ay2d::OrthographicCamera ref;
    ref.viewport        = ayt::ay2d::ViewportRect{0, 0, 1600, 900};
    ref.positionX       = mine.positionX;
    ref.positionY       = mine.positionY;
    ref.zoom            = mine.zoom;
    ref.rotationRadians = mine.rotationRadians;
    ref.viewSize        = mine.viewSize;
    ref.nearZ           = mine.nearZ;
    ref.farZ            = mine.farZ;

    checkMatrixEq(mine.viewMatrix(), ref.viewMatrix());
    checkMatrixEq(mine.projectionMatrix(), ref.projectionMatrix());
}

// ─── #10 — .ayscene round-trip through the wire table. ────────────
TEST_CASE(cm3_2d_components_ayscene_roundtrip)
{
    World::instance().initialize();
    ayt::entity::registerEntityComponents();

    Entity* original = World::instance().createEntity();
    original->setName("Ground");
    CHECK_NOT_NULL(original);

    TilemapComponent* tm = original->addComponent<TilemapComponent>();
    tm->tilemapPath        = "tilemaps/ground.aytilemap";
    tm->atlasTexturePath   = "textures/terrain.aytex";
    tm->atlasTilesPerRow   = 8;
    tm->atlasTilesPerColumn = 4;
    tm->layer              = 2;
    tm->sortingKey         = 123;

    SpriteComponent* sp = original->addComponent<SpriteComponent>();
    sp->texturePath   = "textures/hero.aytex";
    sp->position      = ayt::math::FVector3(1.0f, 2.0f, 3.0f);
    sp->rotationZ     = 0.5f;
    sp->scaleX        = 2.0f;
    sp->scaleY        = 0.5f;
    sp->sourceRectMin = ayt::math::FVector2(0.1f, 0.2f);
    sp->sourceRectMax = ayt::math::FVector2(0.9f, 0.8f);
    sp->colorRGBA     = ayt::math::FVector4(1.0f, 0.5f, 0.25f, 1.0f);
    sp->flip          = 1;
    sp->layer         = 3;
    sp->sortingKey    = 7;

    OrthoCameraComponent* cam = original->addComponent<OrthoCameraComponent>();
    cam->positionX       = 5.0f;
    cam->positionY       = -3.0f;
    cam->zoom            = 2.0f;
    cam->rotationRadians = 0.1f;
    cam->viewSize        = 10.0f;
    cam->viewportAspect  = 2.0f;
    cam->nearZ           = -2.0f;
    cam->farZ            = 2.0f;
    cam->layerMask       = 0xFFu;

    const char* path = "test_cm3_2d_components.ayscene";
    CHECK(saveScene(World::instance(), path));

    World::instance().destroyEntity(original);
    CHECK_INT_EQ(static_cast<int>(World::instance().getAllEntities().size()), 0);

    ayt::serializer::SerializeError err;
    CHECK(loadScene(World::instance(), path, &err));
    CHECK(err.ok());

    Entity* loaded = World::instance().findEntity("Ground");
    CHECK_NOT_NULL(loaded);
    CHECK_TRUE(loaded->hasComponent<TilemapComponent>());
    CHECK_TRUE(loaded->hasComponent<SpriteComponent>());
    CHECK_TRUE(loaded->hasComponent<OrthoCameraComponent>());

    const TilemapComponent* ltm = loaded->getComponent<TilemapComponent>();
    CHECK_TRUE(ltm->tilemapPath == "tilemaps/ground.aytilemap");
    CHECK_TRUE(ltm->atlasTexturePath == "textures/terrain.aytex");
    CHECK_INT_EQ(ltm->atlasTilesPerRow, 8);
    CHECK_INT_EQ(ltm->atlasTilesPerColumn, 4);
    CHECK_INT_EQ(ltm->layer, 2);
    CHECK_INT_EQ(ltm->sortingKey, 123);

    const SpriteComponent* lsp = loaded->getComponent<SpriteComponent>();
    CHECK_TRUE(lsp->texturePath == "textures/hero.aytex");
    CHECK_FLOAT_EQ(lsp->position.x, 1.0f, 1e-5f);
    CHECK_FLOAT_EQ(lsp->position.z, 3.0f, 1e-5f);
    CHECK_FLOAT_EQ(lsp->rotationZ, 0.5f, 1e-5f);
    CHECK_FLOAT_EQ(lsp->scaleX, 2.0f, 1e-5f);
    CHECK_FLOAT_EQ(lsp->scaleY, 0.5f, 1e-5f);
    CHECK_FLOAT_EQ(lsp->sourceRectMin.x, 0.1f, 1e-5f);
    CHECK_FLOAT_EQ(lsp->sourceRectMax.y, 0.8f, 1e-5f);
    CHECK_FLOAT_EQ(lsp->colorRGBA.y, 0.5f, 1e-5f);
    CHECK_FLOAT_EQ(lsp->colorRGBA.w, 1.0f, 1e-5f);
    CHECK_INT_EQ(lsp->flip, 1);
    CHECK_INT_EQ(lsp->layer, 3);
    CHECK_INT_EQ(lsp->sortingKey, 7);

    const OrthoCameraComponent* lcam = loaded->getComponent<OrthoCameraComponent>();
    CHECK_FLOAT_EQ(lcam->positionX, 5.0f, 1e-5f);
    CHECK_FLOAT_EQ(lcam->positionY, -3.0f, 1e-5f);
    CHECK_FLOAT_EQ(lcam->zoom, 2.0f, 1e-5f);
    CHECK_FLOAT_EQ(lcam->rotationRadians, 0.1f, 1e-5f);
    CHECK_FLOAT_EQ(lcam->viewSize, 10.0f, 1e-5f);
    CHECK_FLOAT_EQ(lcam->viewportAspect, 2.0f, 1e-5f);
    CHECK_FLOAT_EQ(lcam->nearZ, -2.0f, 1e-5f);
    CHECK_FLOAT_EQ(lcam->farZ, 2.0f, 1e-5f);
    CHECK(lcam->layerMask == 0xFFu);

    std::remove(path);
    World::instance().shutdown();
}

TEST_SUITE_END
