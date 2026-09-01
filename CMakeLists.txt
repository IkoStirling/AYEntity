cmake_minimum_required(VERSION 3.20)

project(AYEntity
    VERSION 0.1.0
    DESCRIPTION "AYEntity — ECS + Network binding + World scene save/load"
)

option(AY_ENTITY_PRECOMPILE_COMPONENTS "Precompile common components" ON)

set(SOURCES
    src/AYWorld.cpp
    src/AYEntity.cpp
    src/AYComponentRegistry.cpp
    src/AYEntityComponentModule.cpp
    src/AYEntitySubSystem.cpp
    # Collider bridge (2026-08-19) — body creation + collider sync +
    # destroy lifecycle extracted out of AYEntitySubSystem for testability.
    src/AYEntityPhysicsBridge.cpp
    src/AYRenderSystem.cpp
    src/AYEntityReflection.cpp
    src/AYEntityModule.cpp
    src/AYAnimationSystem.cpp
    src/AYSkinnedMeshRenderSystem.cpp
    src/AYCharacterEntity.cpp
    src/AYComponentFactory.cpp
    src/AYSceneSerializer.cpp
    src/AYSkeletonComponent.cpp
    # P2.1 (2026-07-27) — BlendSpace 1D/2D BlendTree driver. Priority
    # 430 (before AnimationSystem@450). Writes
    # SkeletonComponent::skinMatricesBlendSpace; AnimationSystem
    # memcpy picks it as the authoritative base when non-null.
    src/AYBlendSpaceSystem.cpp
    # P3.1 (2026-08-06) — L1 state machine driver system. Priority 460
    # (after AnimationSystem@450); transitions decide which clip plays
    # next frame; AnimStateChangedEvent emitted on transition.
    src/AYStateMachineSystem.cpp
    # CM-3 (2026-08-11) — 2D lane: ortho camera driver (405), streaming
    # shell (430), animation-tick shell (460), tilemap render (510),
    # sprite render (510). All five registered by bootstrapModule().
    src/AYOrthoCameraUpdateSystem.cpp
    src/AYTilemapStreamingSystem.cpp
    src/AYTilemapVisibilityRuntime.cpp
    src/AYTilemapChunkGeometry.cpp
    # CM-5 (2026-08-12) — per-path animation table mirror (tick + resolve
    # + resolve buffer). Driven by TilemapAnimationTickSystem (460),
    # consumed by TilemapRenderSystem (510).
    src/AYTilemapAnimationRuntime.cpp
    src/AYTilemapAnimationTickSystem.cpp
    src/AYTilemapRenderSystem.cpp
    src/AYSpriteRenderSystem.cpp
    src/AYNetworkComponent.cpp
    # Cross-module precompile barrel — explicit template instantiation
    # declarations for Entity::addComponent<T>/getComponent<T>/hasComponent<T>
    # over the AY_ENTITY_PRECOMPILE_COMPONENTS set. Without this TU,
    # any downstream consumer that statically links AYEntity.lib without
    # itself referencing those members directly hits LNK2019. See
    # `src/detail/AYEntityPrecompile.cpp` for the full rationale.
    #
    # Note: this TU exists to close the AYScene PR-1 follow-up landmine
    # (Test_EntityLinkGlue.cpp can be removed after this lands + AYScene
    # submodule consumes the new AYEntity precompile barrel).
    src/detail/AYEntityPrecompile.cpp
)

add_library(${PROJECT_NAME} STATIC ${SOURCES} ${HEADERS})

target_include_directories(${PROJECT_NAME} PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/interface
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# AtlasGridDesc is a public value type used by inline UV helpers. Keep an
# explicit layout version in the consumer command line so incremental MSVC
# builds cannot retain a stale COMDAT after the descriptor grows.
target_compile_definitions(${PROJECT_NAME} PUBLIC AYENTITY_2D_ATLAS_LAYOUT_VERSION=2)

target_link_libraries(${PROJECT_NAME} PUBLIC
    AY::Module
    AYCore
    AYGameLoop
    AYReflect
    AYSerializer
    AYRenderer
    AYMath
    AYAnimation
    AYResource
    # Phase 1.5: AnimNotify bridge — AnimationSystem::onUpdate emits
    # AnimNotifyEvent via the engine EventBus; this link makes the type
    # definition available to subscribers inside this module's tests
    # too.
    AYEventSystem
)

# Entity's fixed pre/post bridge binds ECS rigid-body components to the latest
# AYPhysics snapshot. The component itself keeps only an opaque uint32 handle.
target_link_libraries(${PROJECT_NAME} PRIVATE AYPhysics AYLog)

if(AY_ENTITY_PRECOMPILE_COMPONENTS)
    target_compile_definitions(${PROJECT_NAME} PUBLIC AY_ENTITY_PRECOMPILE_COMPONENTS)
endif()

enable_testing()
add_subdirectory(unittest)

# ----------------------------------------------------------------------------
# M4 (lh-rh-split-entity audit 2026-08-24): public-header bgfx-leak guard.
# AYEntity is a downstream consumer of AYRenderer; mirrors AY2D / AYScene
# (audit F-5, design.md §11.2). Public headers under include/AYEntity/**
# MUST NOT include <bgfx/*.h> or <bx/*.h>; such includes belong in
# src/detail/* or private header paths only. Re-running the guard:
#   cmake --build . --target ayentity_check_no_bgfx_in_public_headers
# ----------------------------------------------------------------------------
set(AYENTITY_PUBLIC_HEADER_DIR "${CMAKE_CURRENT_SOURCE_DIR}/include")

add_custom_target(ayentity_check_no_bgfx_in_public_headers
    COMMAND ${CMAKE_COMMAND}
        -DAYTGT_PUBLIC_HEADER_DIR="${AYENTITY_PUBLIC_HEADER_DIR}"
        -DAYTGT_MODULE_NAME="${PROJECT_NAME}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/CheckNoBgfxInPublicHeaders.cmake"
    COMMENT "AYEntity public-header bgfx/bx leak guard (audit F-5 / M4)"
    VERBATIM
)

add_dependencies(${PROJECT_NAME} ayentity_check_no_bgfx_in_public_headers)

message(STATUS "AYEntity v0.1.0 (P2.1 BlendSpace + NetworkComponent + P4-B .ayscene save) — see ${CMAKE_CURRENT_SOURCE_DIR}/design.md")
