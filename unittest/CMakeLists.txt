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
    # CM-5 (2026-08-12) — animation table runtime: cross-assert vs the
    # real ayt::ay2d tick/resolve on a shared deterministic case + the
    # sticky-Noop render closed loop (animated tile payload flips frame).
    AYTest_TilemapAnimationRuntime.cpp
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
    # CM-5 (2026-08-12): AY2D static lib — test-only, for the
    # cross-assert against the real ayt::ay2d::tickTilemapAnimation /
    # resolveAnimatedTileId (AY2D links only AYMath + AYLog, no cycle;
    # the AYEntity lib itself never links AY2D — dependency lock stays
    # intact). AY2D's include dir is already on the test include path.
    AY2D
)

# CM-5 (2026-08-12): shaderc hints — the sticky-Noop render closed loop
# (cm5_tilemap_render_animation_closed_loop, and the CM-3 siblings in
# AYTest_2DRenderSystems.cpp) compiles the 2D shader at runtime through
# shaderc; without the hint the cases SKIP. Mirror of the AY2D/demo
# skeleton (do not diverge): SHADERC = bgfx-install shaderc.exe,
# BGFX_COMMON/SRC = bgfx examples/common + src for common.sh includes.
set(AY_SHADER_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../AYShader")
if(NOT DEFINED AY_SHADER_SHADERC_PATH)
    if(WIN32)
        set(_ays_shaderc_name "shaderc.exe")
    else()
        set(_ays_shaderc_name "shaderc")
    endif()
    set(AY_SHADER_SHADERC_PATH
        "${AY_SHADER_ROOT}/thirdParty/bgfx-install/debug/bin/${_ays_shaderc_name}")
endif()
if(NOT DEFINED AY_SHADER_BGFX_COMMON_DIR)
    set(_ays_bgfx_search
        "${CMAKE_CURRENT_SOURCE_DIR}/../../../thirdparty/bgfx/examples/common"
    )
    set(AY_SHADER_BGFX_COMMON_DIR "")
    foreach(_dir IN LISTS _ays_bgfx_search)
        if(EXISTS "${_dir}/common.sh")
            set(AY_SHADER_BGFX_COMMON_DIR "${_dir}")
            break()
        endif()
    endforeach()
endif()
if(NOT DEFINED AY_SHADER_BGFX_SRC_DIR AND AY_SHADER_BGFX_COMMON_DIR)
    set(AY_SHADER_BGFX_SRC_DIR "${AY_SHADER_BGFX_COMMON_DIR}/../../src")
endif()
get_filename_component(AY_SHADER_SHADERC_PATH "${AY_SHADER_SHADERC_PATH}" ABSOLUTE)
get_filename_component(AY_SHADER_BGFX_COMMON_DIR "${AY_SHADER_BGFX_COMMON_DIR}" ABSOLUTE)
get_filename_component(AY_SHADER_BGFX_SRC_DIR "${AY_SHADER_BGFX_SRC_DIR}" ABSOLUTE)

target_compile_definitions(${PROJECT_NAME} PRIVATE
    AY_SHADER_SHADERC_HINT="${AY_SHADER_SHADERC_PATH}"
    AY_SHADER_BGFX_COMMON_HINT="${AY_SHADER_BGFX_COMMON_DIR}"
    AY_SHADER_BGFX_SRC_HINT="${AY_SHADER_BGFX_SRC_DIR}"
)

target_compile_options(${PROJECT_NAME} PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>: /W4 /wd4200>
    $<$<CXX_COMPILER_ID:GNU,Clang>: -Wall -Wextra -Wpedantic>
)

ay_configure_test_tmpdir(${PROJECT_NAME})
