cmake_minimum_required(VERSION 3.20)

project(AYEntityTest)

set(SOURCES
    TestMain.cpp
    EntityTest.cpp
    ComponentTest.cpp
    ComponentRegistryTest.cpp
    SparseSetTest.cpp
    WorldTest.cpp
    EntityHandleTest.cpp
    SystemTest.cpp
    AYTest_DeterministicLane.cpp
    SkinnedAnimationTest.cpp
    CharacterEntityTest.cpp
    AYTest_SceneSerializer.cpp
    AYTest_ComponentIdentity.cpp
    AYTest_PerspectiveCamera.cpp
    AYTest_ActorClass.cpp
    # P2.1 (2026-07-27) — BlendSpaceSystem ECS integration tests
    # (priority wiring + skinMatricesBlendSpace handoff to renderer).
    AYTest_BlendSpaceSystem.cpp
    # P2.2 (2026-08-03) — Skeleton Mask bridge tests (maskPath
    # component field + AnimationSystem rebind cache + fail-soft
    # ResourceManager::load<ISkeletonMask> path). 6 cases.
    AYTest_SkeletonMaskBridge.cpp
    # P3.1 (2026-08-06) — StateMachineSystem ECS integration tests
    # (priority wiring + AnimationPlayer clip push + AnimStateChangedEvent).
    AYTest_StateMachineSystem.cpp
    # CM-3 (2026-08-11) — 2D lane: component defaults + .ayscene
    # round-trip + UV/ortho math cross-asserts vs real AY2D headers.
    AYTest_2DComponents.cpp
    # CM-3 (2026-08-11) — 2D render systems: priority wiring +
    # buildRenderScene closed loop (sticky-Noop) + cull + lazy-load
    # failure + draw-order pin.
    AYTest_2DRenderSystems.cpp
    AYTest_SpriteAnimationSystem.cpp
    # CM-5 (2026-08-12) — animation table runtime: cross-assert vs the
    # real ayt::ay2d tick/resolve on a shared deterministic case + the
    # sticky-Noop render closed loop (animated tile payload flips frame).
    AYTest_TilemapAnimationRuntime.cpp
    AYTest_TilemapChunkGeometry.cpp
    # Collider (2026-08-19) — EntityPhysicsBridge integration tests
    # (body creation params + collider create/rebuild/destroy against a
    # live PhysicsManager with Mock/Null backends).
    EntityPhysicsBridgeTest.cpp
)

if(TARGET AYEntityParticleIntegration)
    list(APPEND SOURCES AYTest_ParticleSystems.cpp)
endif()
add_executable(${PROJECT_NAME} ${SOURCES})

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../interface
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../AYFoundation/AYTest
    ${CMAKE_CURRENT_SOURCE_DIR}/../../AYRuntime/AYAnimation/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../AYRuntime/AYAnimation/include/AYAnimation
    ${CMAKE_CURRENT_SOURCE_DIR}/../../AYFoundation/AYMath/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../AYRuntime/AYResource/interface
    ${CMAKE_CURRENT_SOURCE_DIR}/../../AYRuntime/AYResource/interface/assetsDefs
    ${CMAKE_CURRENT_SOURCE_DIR}/../../AYRuntime/AYResource/include
    # CM-3 (2026-08-11): header-only AY2D math for cross-asserts
    # (AY2D/TileSamplerUV.h / AY2D/OrthographicCamera.h — no link, no dep).
    # The AYEntity lib itself does NOT depend on AY2D; test-only.
    # Path: unittest -> AYEntity -> AYRuntime, then into AY2D. (The
    # ../../AYRuntime/... entries above are legacy-redundant — they
    # resolve via PUBLIC link includes — but this one must be exact:
    # AY2D is never linked, so there is no transitive include.)
    ${CMAKE_CURRENT_SOURCE_DIR}/../../AY2D/include
    # Collider (2026-08-19): AYPhysics public headers + testaccess for
    # EntityPhysicsBridgeTest (Mock backend command/payload capture).
    ${CMAKE_CURRENT_SOURCE_DIR}/../../AYPhysics/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../AYPhysics/interface
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    AYEntity
    AYTest
    AYCore
    AYGameLoop
    AYSerializer
    # CM-5 (2026-08-12): AY2D static lib — test-only, for the
    # cross-assert against the real ayt::ay2d::tickTilemapAnimation /
    # resolveAnimatedTileId (AY2D links only AYMath + AYLog, no cycle;
    # the AYEntity lib itself never links AY2D — dependency lock stays
    # intact). AY2D's include dir is already on the test include path.
    AY2D
    # Collider (2026-08-19): AYPhysics static lib — test-only, for
    # EntityPhysicsBridgeTest (bridge tests construct a live
    # PhysicsManager; the AYEntity lib links AYPhysics PRIVATE).
    AYPhysics
)

# CM-5 (2026-08-12): shaderc hints — the sticky-Noop render closed loop
# (cm5_tilemap_render_animation_closed_loop, and the CM-3 siblings in
# AYTest_2DRenderSystems.cpp) compiles the 2D shader at runtime through
# shaderc; without the hint the cases SKIP. Mirror of the AY2D/demo
# skeleton (do not diverge): shaderc and shader includes come from the
# centralized AYShader vcpkg integration.
set(AY_SHADER_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../AYShader")
include("${AY_SHADER_ROOT}/cmake/AYBgfx.cmake")

target_compile_definitions(${PROJECT_NAME} PRIVATE
    AY_ENTITY_HAS_SCRIPT_INTEGRATION=$<BOOL:${AY_ENABLE_SCRIPT}>
    AY_ENTITY_HAS_NETWORK_INTEGRATION=$<BOOL:${AY_ENABLE_NETWORK}>
    AY_SHADER_SHADERC_HINT="${AY_SHADER_SHADERC_PATH}"
    AY_SHADER_BGFX_COMMON_HINT="${AY_SHADER_BGFX_COMMON_DIR}"
    AY_SHADER_BGFX_SRC_HINT="${AY_SHADER_BGFX_SRC_DIR}"
)

target_compile_options(${PROJECT_NAME} PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>: /W4 /wd4200>
    $<$<CXX_COMPILER_ID:GNU,Clang>: -Wall -Wextra -Wpedantic>
)

ay_add_test(${PROJECT_NAME})
ay_add_test(${PROJECT_NAME}
    NAME AYEntity_2DBaseline
    SUITE AYEntity2DComponents
    LABELS "2d-baseline;ayentity")

# Registration-only IModule pilot. It runs in a dedicated process because the
# process ComponentRegistry is deliberately sealed for the remainder of its
# lifetime after ModuleManager::registerTypes().
add_executable(AYEntityComponentModuleTest
    EntityComponentModuleTestMain.cpp
    EntityComponentModuleTest.cpp
)

target_link_libraries(AYEntityComponentModuleTest PRIVATE
    AYEntity
    AY::Module
    AYTest
)

target_compile_options(AYEntityComponentModuleTest PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>: /W4 /wd4200>
    $<$<CXX_COMPILER_ID:GNU,Clang>: -Wall -Wextra -Wpedantic>
)

ay_add_test(AYEntityComponentModuleTest)

# Focused particle bridge regressions avoid rebuilding unrelated Entity suites.
if(TARGET AYEntityParticleIntegration)
    add_executable(AYEntity_ParticleCollisionSmoke ParticleCollisionSmoke.cpp)
    target_link_libraries(AYEntity_ParticleCollisionSmoke PRIVATE AYEntityCore AYEntityParticleIntegration AYCore AYSerializer)
    if(MSVC)
        target_link_options(AYEntity_ParticleCollisionSmoke PRIVATE /DEBUG:NONE /INCREMENTAL:NO)
    endif()
    ay_add_test(AYEntity_ParticleCollisionSmoke)
    add_executable(AYEntity_ParticleTests TestMain.cpp AYTest_ParticleSystems.cpp)
    target_link_libraries(AYEntity_ParticleTests PRIVATE AYEntity AYTest AYCore AYSerializer)
    target_compile_definitions(AYEntity_ParticleTests PRIVATE
        AY_SHADER_SHADERC_HINT="${AY_SHADER_SHADERC_PATH}"
        AY_SHADER_BGFX_COMMON_HINT="${AY_SHADER_BGFX_COMMON_DIR}"
        AY_SHADER_BGFX_SRC_HINT="${AY_SHADER_BGFX_SRC_DIR}")
    if(MSVC)
        target_link_options(AYEntity_ParticleTests PRIVATE /DEBUG:NONE /INCREMENTAL:NO)
    endif()
    ay_add_test(AYEntity_ParticleTests)
endif()
