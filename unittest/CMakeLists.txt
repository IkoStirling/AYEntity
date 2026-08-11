cmake_minimum_required(VERSION 3.20)

project(AYEntityTest)

set(SOURCES
    TestMain.cpp
    EntityTest.cpp
    ComponentTest.cpp
    SparseSetTest.cpp
    WorldTest.cpp
    EntityHandleTest.cpp
    SystemTest.cpp
    SkinnedAnimationTest.cpp
    CharacterEntityTest.cpp
    AYTest_SceneSerializer.cpp
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
)

add_executable(${PROJECT_NAME} ${SOURCES})

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../interface
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../AYFoundation/AYTest
    ${CMAKE_CURRENT_SOURCE_DIR}/../../AYRuntime/AYAnimation/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../AYRuntime/AYAnimation/include/ayanimation
    ${CMAKE_CURRENT_SOURCE_DIR}/../../AYFoundation/AYMath/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../AYRuntime/AYResource/interface
    ${CMAKE_CURRENT_SOURCE_DIR}/../../AYRuntime/AYResource/interface/assetsDefs
    ${CMAKE_CURRENT_SOURCE_DIR}/../../AYRuntime/AYResource/include
    # CM-3 (2026-08-11): header-only AY2D math for cross-asserts
    # (AYTileSamplerUV.h / AYOrthographicCamera.h — no link, no dep).
    # The AYEntity lib itself does NOT depend on AY2D; test-only.
    # Path: unittest -> AYEntity -> AYRuntime, then into AY2D. (The
    # ../../AYRuntime/... entries above are legacy-redundant — they
    # resolve via PUBLIC link includes — but this one must be exact:
    # AY2D is never linked, so there is no transitive include.)
    ${CMAKE_CURRENT_SOURCE_DIR}/../../AY2D/include
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    AYEntity
    AYTest
    AYCore
    AYGameLoop
    AYSerializer
)

target_compile_options(${PROJECT_NAME} PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>: /W4 /wd4200>
    $<$<CXX_COMPILER_ID:GNU,Clang>: -Wall -Wextra -Wpedantic>
)

ay_configure_test_tmpdir(${PROJECT_NAME})
