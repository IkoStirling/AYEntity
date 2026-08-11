cmake_minimum_required(VERSION 3.20)

project(AYEntity
    VERSION 0.1.0
    DESCRIPTION "AYEntity — ECS + Network binding + World scene save/load"
)

option(AY_ENTITY_PRECOMPILE_COMPONENTS "Precompile common components" ON)

set(SOURCES
    src/AYWorld.cpp
    src/AYEntity.cpp
    src/AYEntitySubSystem.cpp
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

target_link_libraries(${PROJECT_NAME} PUBLIC
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

if(AY_ENTITY_PRECOMPILE_COMPONENTS)
    target_compile_definitions(${PROJECT_NAME} PUBLIC AY_ENTITY_PRECOMPILE_COMPONENTS)
endif()

enable_testing()
add_subdirectory(unittest)

message(STATUS "AYEntity v0.1.0 (P2.1 BlendSpace + NetworkComponent + P4-B .ayscene save) — see ${CMAKE_CURRENT_SOURCE_DIR}/design.md")