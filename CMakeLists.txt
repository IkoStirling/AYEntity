cmake_minimum_required(VERSION 3.20)

project(AYEntity
    VERSION 0.2.0
    DESCRIPTION "AYEntity — dependency-light ECS core plus opt-in integrations"
)

option(AY_ENTITY_PRECOMPILE_COMPONENTS "Precompile common components" ON)

# These defaults also allow AYEntity to be configured as a standalone
# subproject. The engine root exposes the same cache options to product builds.
foreach(_feature ANIMATION RENDERER PHYSICS SCRIPT NETWORK)
    if(NOT DEFINED AY_ENABLE_${_feature})
        set(AY_ENABLE_${_feature} ON)
    endif()
endforeach()
if(NOT DEFINED AY_ENABLE_AY2D)
    set(AY_ENABLE_AY2D ON)
endif()
if(NOT DEFINED AY_ENABLE_ENTITY_2D_SCHEMA)
    set(AY_ENABLE_ENTITY_2D_SCHEMA ${AY_ENABLE_AY2D})
endif()

set(AYENTITY_PUBLIC_INCLUDE_DIRS
    ${CMAKE_CURRENT_SOURCE_DIR}/interface
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}
)

function(ay_configure_entity_target target)
    target_include_directories(${target} PUBLIC
        ${AYENTITY_PUBLIC_INCLUDE_DIRS}
    )
    target_compile_options(${target} PRIVATE
        $<$<CXX_COMPILER_ID:MSVC>:/W4>
        $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic>
    )
endfunction()

# ---------------------------------------------------------------------------
# Core: World/Entity/serialization/registry and deterministic ECS lanes only.
# This target must stay free of Renderer, Animation, Physics, Script, Network,
# Resource, and EventSystem link dependencies.
# ---------------------------------------------------------------------------
add_library(AYEntityCore STATIC
    src/AYWorld.cpp
    src/AYWorldLifecycle.cpp
    src/AYEntity.cpp
    src/AYComponentRegistry.cpp
    src/AYEntityReflection.cpp
    src/AYPerspectiveCameraSelection.cpp
    src/AYEntityCoreModule.cpp
    src/AYEntityComponentModule.cpp
    src/AYEntityRuntimeModule.cpp
    src/AYEntitySubSystem.cpp
    src/AYSimToPresentBridgeSystem.cpp
    src/AYComponentFactory.cpp
    src/AYSceneSerializer.cpp
    src/AYActorClassAsset.cpp
    src/detail/AYEntityPrecompile.cpp
)
add_library(AYEntity::Core ALIAS AYEntityCore)
ay_configure_entity_target(AYEntityCore)
target_link_libraries(AYEntityCore PUBLIC
    AY::Module
    AYAssetFormat
    AYCore
    AYGameLoop
    AYReflect
    AYSerializer
    AYIO
    AYMath
)
target_compile_definitions(AYEntityCore PUBLIC
    AYENTITY_2D_ATLAS_LAYOUT_VERSION=2
)
if(AY_ENTITY_PRECOMPILE_COMPONENTS)
    target_compile_definitions(AYEntityCore PUBLIC
        AY_ENTITY_PRECOMPILE_COMPONENTS)
endif()

# CPU particle metadata remains usable in headless scene loading.
if(TARGET AYParticle)
    target_sources(AYEntityCore PRIVATE src/AYParticleEmitterComponent.cpp)
    target_link_libraries(AYEntityCore PUBLIC AYParticle AYParticleAssets)
    target_compile_definitions(AYEntityCore PRIVATE AY_ENTITY_HAS_PARTICLE=1)
else()
    target_compile_definitions(AYEntityCore PRIVATE AY_ENTITY_HAS_PARTICLE=0)
endif()

set(_ay_entity_integrations)

# Explicit session/replay adapter; Core retains its existing dependency boundary.
if(TARGET AYReplay)
    add_library(AYEntityDeterminismKernel STATIC
        src/AYDeterministicSession.cpp src/AYDeterministicSessionWire.cpp src/AYDeterministicState.cpp
        src/AYDeterministicCollision2D.cpp
        src/AYDeterministicLockstepProtocol.cpp src/AYDeterministicLockstep.cpp
        src/AYDeterministicRollback.cpp src/AYDeterministicRollbackNetwork.cpp)
    add_library(AYEntity::DeterminismKernel ALIAS AYEntityDeterminismKernel)
    ay_configure_entity_target(AYEntityDeterminismKernel)
    target_link_libraries(AYEntityDeterminismKernel PUBLIC AYMath AYReplay)
    add_library(AYEntityDeterminism STATIC src/AYDeterministicSessionWorld.cpp src/AYDeterministicReplay.cpp src/AYDeterministicRollbackReplay.cpp)
    add_library(AYEntity::Determinism ALIAS AYEntityDeterminism)
    ay_configure_entity_target(AYEntityDeterminism)
    target_link_libraries(AYEntityDeterminism PUBLIC AYEntityCore AYEntityDeterminismKernel AYReplay)
endif()

# Declared here, invoked after AYApplication/AYScene exist in the superproject.
# Keep this opt-in adapter outside the compatibility facade to avoid a Host cycle.
function(ay_entity_add_determinism_host_integration)
    if(NOT TARGET AYEntityDeterminism OR NOT TARGET AYApplication OR NOT TARGET AYScene)
        return()
    endif()
    set(_root "${CMAKE_CURRENT_FUNCTION_LIST_DIR}")
    add_library(AYEntityDeterminismHost STATIC "${_root}/src/AYDeterministicHost.cpp")
    add_library(AYEntity::DeterminismHost ALIAS AYEntityDeterminismHost)
    target_link_libraries(AYEntityDeterminismHost PUBLIC AYEntityDeterminism AYApplication AYScene AYEventSystem)
    target_compile_options(AYEntityDeterminismHost PRIVATE
        $<$<CXX_COMPILER_ID:MSVC>:/W4>
        $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic>)
      if(BUILD_TESTING)
        add_executable(AYEntity_DeterministicRollbackReplayWorker "${_root}/unittest/DeterministicRollbackReplayWorker.cpp")
        target_link_libraries(AYEntity_DeterministicRollbackReplayWorker PRIVATE AYEntityDeterminism)
        add_executable(AYEntity_DeterministicHostSessionTests
            "${_root}/unittest/DeterministicHostSessionTestMain.cpp"
            "${_root}/unittest/AYTest_DeterministicHostSession.cpp"
            "${_root}/unittest/AYTest_DeterministicHostNetwork.cpp"
            "${_root}/unittest/AYTest_DeterministicHostRollback.cpp"
            "${_root}/unittest/AYTest_DeterministicRollbackReplay.cpp")
        target_link_libraries(AYEntity_DeterministicHostSessionTests PRIVATE AYEntityDeterminismHost AYTest)
        ay_add_test(AYEntity_DeterministicHostSessionTests LABELS "ayentity;determinism;host;replay" TIMEOUT 240 RUN_SERIAL)
        if(TARGET AYNetwork AND WIN32)
            add_executable(AYEntity_DeterministicHostRollbackWorker "${_root}/unittest/DeterministicHostRollbackWorker.cpp")
            target_link_libraries(AYEntity_DeterministicHostRollbackWorker PRIVATE AYEntityDeterminismHost AYNetwork)
            add_dependencies(AYEntity_DeterministicHostRollbackWorker AYEntity_DeterministicRollbackReplayWorker)
            add_test(NAME AYEntity_DeterministicHostRollbackProcesses COMMAND pwsh -NoProfile -File
                "${_root}/unittest/RunDeterministicNetworkCheck.ps1" -Worker $<TARGET_FILE:AYEntity_DeterministicHostRollbackWorker>
                -OutputDir "${CMAKE_CURRENT_BINARY_DIR}/host-rollback-process-check" -TimeoutSeconds 660 -CompareTrace
                -ReplayWorker $<TARGET_FILE:AYEntity_DeterministicRollbackReplayWorker>)
            set_tests_properties(AYEntity_DeterministicHostRollbackProcesses PROPERTIES TIMEOUT 1440 RUN_SERIAL TRUE LABELS "ayentity;determinism;host;network;rollback;process")
            add_executable(AYEntity_DeterministicHostNetworkWorker "${_root}/unittest/DeterministicHostNetworkWorker.cpp")
            target_link_libraries(AYEntity_DeterministicHostNetworkWorker PRIVATE AYEntityDeterminismHost AYNetwork)
            add_test(NAME AYEntity_DeterministicHostNetworkProcesses COMMAND pwsh -NoProfile -File
                "${_root}/unittest/RunDeterministicNetworkCheck.ps1" -Worker $<TARGET_FILE:AYEntity_DeterministicHostNetworkWorker>
                -OutputDir "${CMAKE_CURRENT_BINARY_DIR}/host-network-process-check" -TimeoutSeconds 450 -CompareTrace)
            set_tests_properties(AYEntity_DeterministicHostNetworkProcesses PROPERTIES TIMEOUT 960 RUN_SERIAL TRUE LABELS "ayentity;determinism;host;network;process")
        endif()
        add_executable(AYEntity_DeterministicHostSessionWorker "${_root}/unittest/DeterministicHostSessionWorker.cpp")
        target_link_libraries(AYEntity_DeterministicHostSessionWorker PRIVATE AYEntityDeterminismHost)
        add_test(NAME AYEntity_DeterministicHostSessionProcesses
            COMMAND ${CMAKE_COMMAND}
                -DWORKER=$<TARGET_FILE:AYEntity_DeterministicHostSessionWorker>
                -DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/host-session-process-check
                -P "${_root}/unittest/RunDeterministicHostProcessCheck.cmake")
        set_tests_properties(AYEntity_DeterministicHostSessionProcesses PROPERTIES
            LABELS "ayentity;determinism;host;replay;process" TIMEOUT 960 RUN_SERIAL TRUE)
        add_executable(AYEntity_DeterministicCollisionHostWorker "${_root}/unittest/DeterministicCollisionHostWorker.cpp")
        target_link_libraries(AYEntity_DeterministicCollisionHostWorker PRIVATE AYEntityDeterminismHost)
        add_executable(AYEntity_DeterministicExtendedCollisionHostWorker "${_root}/unittest/DeterministicCollisionHostWorker.cpp")
        target_compile_definitions(AYEntity_DeterministicExtendedCollisionHostWorker PRIVATE AYDET_COLLISION_EXTENDED=1)
        target_link_libraries(AYEntity_DeterministicExtendedCollisionHostWorker PRIVATE AYEntityDeterminismHost)
        add_test(NAME AYEntity_DeterministicExtendedCollisionProcesses
            COMMAND ${CMAKE_COMMAND} -DWORKER=$<TARGET_FILE:AYEntity_DeterministicExtendedCollisionHostWorker>
                -DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/extended-collision-host-process-check
                -P "${_root}/unittest/RunDeterministicHostProcessCheck.cmake")
        set_tests_properties(AYEntity_DeterministicExtendedCollisionProcesses PROPERTIES
            LABELS "ayentity;determinism;collision;2d;host;replay;process" TIMEOUT 960 RUN_SERIAL TRUE)
        add_test(NAME AYEntity_DeterministicCollisionProcesses
            COMMAND ${CMAKE_COMMAND}
                -DWORKER=$<TARGET_FILE:AYEntity_DeterministicCollisionHostWorker>
                -DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/collision-host-process-check
                -P "${_root}/unittest/RunDeterministicHostProcessCheck.cmake")
        set_tests_properties(AYEntity_DeterministicCollisionProcesses PROPERTIES
            LABELS "ayentity;determinism;collision;2d;host;replay;process" TIMEOUT 960 RUN_SERIAL TRUE)
    endif()
endfunction()

# Serialized 2D component metadata is useful to headless tools and servers.
# Keep it separate from the renderer-backed systems so scene loading and
# validation do not require a graphics device.
if(AY_ENABLE_ENTITY_2D_SCHEMA)
    target_sources(AYEntityCore PRIVATE src/AYEntity2DComponents.cpp)
    target_compile_definitions(AYEntityCore PRIVATE
        AY_ENTITY_HAS_2D_SCHEMA=1)
    add_library(AYEntity2DComponents INTERFACE)
    add_library(AYEntity::TwoDComponents ALIAS AYEntity2DComponents)
    target_link_libraries(AYEntity2DComponents INTERFACE AYEntityCore AYMath)
    list(APPEND _ay_entity_integrations AYEntity2DComponents)
else()
    target_compile_definitions(AYEntityCore PRIVATE
        AY_ENTITY_HAS_2D_SCHEMA=0)
endif()

if(AY_ENABLE_ANIMATION)
    add_library(AYEntityAnimationIntegration STATIC
        src/AYEntityAnimationIntegrationModule.cpp
        src/AYAnimationSystem.cpp
        src/AYBlendSpaceSystem.cpp
        src/AYStateMachineSystem.cpp
        src/AYSkeletonComponent.cpp
    )
    add_library(AYEntity::AnimationIntegration ALIAS
        AYEntityAnimationIntegration)
    ay_configure_entity_target(AYEntityAnimationIntegration)
    target_link_libraries(AYEntityAnimationIntegration PUBLIC
        AYEntityCore
        AYAnimation
        AYResource
        AYEventSystem
    )
    list(APPEND _ay_entity_integrations AYEntityAnimationIntegration)
endif()

if(AY_ENABLE_RENDERER)
    if(NOT AY_ENABLE_ANIMATION)
        message(FATAL_ERROR
            "AY_ENABLE_RENDERER requires AY_ENABLE_ANIMATION for skinned ECS rendering")
    endif()
    add_library(AYEntityRenderIntegration STATIC
        src/AYEntityRenderIntegrationModule.cpp
        src/AYPerspectiveCameraUpdateSystem.cpp
        src/AYRenderSystem.cpp
        src/AYSkinnedMeshRenderSystem.cpp
        src/AYCharacterEntity.cpp
    )
    add_library(AYEntity::RenderIntegration ALIAS AYEntityRenderIntegration)
    ay_configure_entity_target(AYEntityRenderIntegration)
    target_link_libraries(AYEntityRenderIntegration PUBLIC
        AYEntityCore
        AYEntityAnimationIntegration
        AYRenderer
        AYResource
        AYMath
    )
    list(APPEND _ay_entity_integrations AYEntityRenderIntegration)

    if(AY_ENABLE_AY2D)
        add_library(AYEntity2DIntegration STATIC
            src/AYEntity2DIntegrationModule.cpp
            src/AYOrthoCameraSelection.cpp
            src/AYOrthoCameraUpdateSystem.cpp
            src/AYTilemapStreamingSystem.cpp
            src/AYTilemapVisibilityRuntime.cpp
            src/AYTilemapChunkGeometry.cpp
            src/AYTilemapAnimationRuntime.cpp
            src/AYTilemapAnimationTickSystem.cpp
            src/AYTilemapRenderSystem.cpp
            src/AYSpriteAnimationSystem.cpp
            src/AYSpriteRenderSystem.cpp
        )
        add_library(AYEntity::TwoDIntegration ALIAS AYEntity2DIntegration)
        ay_configure_entity_target(AYEntity2DIntegration)
        target_link_libraries(AYEntity2DIntegration PUBLIC
            AYEntity2DComponents
            AYEntityCore
            AYRenderer
            AYResource
            AYMath
        )
        list(APPEND _ay_entity_integrations AYEntity2DIntegration)
    endif()
endif()

if(AY_ENABLE_PHYSICS)
    add_library(AYEntityPhysicsIntegration STATIC
        src/AYEntityPhysicsIntegrationModule.cpp
        src/AYEntityPhysicsBridge.cpp
    )
    add_library(AYEntity::PhysicsIntegration ALIAS
        AYEntityPhysicsIntegration)
    ay_configure_entity_target(AYEntityPhysicsIntegration)
    target_link_libraries(AYEntityPhysicsIntegration PUBLIC
        AYEntityCore
        AYPhysics
    )
    target_link_libraries(AYEntityPhysicsIntegration PRIVATE AYLog)
    list(APPEND _ay_entity_integrations AYEntityPhysicsIntegration)
endif()

if(AY_ENABLE_SCRIPT)
    add_library(AYEntityScriptIntegration STATIC
        src/AYEntityScriptIntegrationModule.cpp
    )
    add_library(AYEntity::ScriptIntegration ALIAS AYEntityScriptIntegration)
    ay_configure_entity_target(AYEntityScriptIntegration)
    target_link_libraries(AYEntityScriptIntegration PUBLIC AYEntityCore)
    list(APPEND _ay_entity_integrations AYEntityScriptIntegration)
endif()

if(AY_ENABLE_NETWORK)
    add_library(AYEntityNetworkIntegration STATIC
        src/AYEntityNetworkIntegrationModule.cpp
        src/AYNetworkComponent.cpp
    )
    add_library(AYEntity::NetworkIntegration ALIAS
        AYEntityNetworkIntegration)
    ay_configure_entity_target(AYEntityNetworkIntegration)
    target_link_libraries(AYEntityNetworkIntegration PUBLIC AYEntityCore)
    list(APPEND _ay_entity_integrations AYEntityNetworkIntegration)
endif()

if(AY_ENABLE_RENDERER AND TARGET AYParticle)
    add_library(AYEntityParticleIntegration STATIC
        src/AYParticleSystem.cpp src/AYEntityParticleIntegrationModule.cpp)
    add_library(AYEntity::ParticleIntegration ALIAS AYEntityParticleIntegration)
    ay_configure_entity_target(AYEntityParticleIntegration)
    target_link_libraries(AYEntityParticleIntegration PUBLIC AYEntityCore AYParticle AYParticleResources AYRenderer AYMath)
    target_sources(AYEntityParticleIntegration PRIVATE src/ParticleGpuState.cpp)
    if(TARGET AYParticleGpu)
        target_compile_definitions(AYEntityParticleIntegration PRIVATE AY_ENTITY_PARTICLE_GPU=1)
        target_link_libraries(AYEntityParticleIntegration PRIVATE AYParticleGpu)
    endif()
    list(APPEND _ay_entity_integrations AYEntityParticleIntegration)
endif()

# Compatibility facade. Existing consumers that link AYEntity keep the full
# default feature surface; new hosts should link AYEntityCore and only the
# integration targets they assemble.
add_library(AYEntity STATIC src/AYEntityModule.cpp)
add_library(AYEntity::All ALIAS AYEntity)
ay_configure_entity_target(AYEntity)
target_link_libraries(AYEntity PUBLIC
    AYEntityCore
    ${_ay_entity_integrations}
)
target_compile_definitions(AYEntity PRIVATE
    AY_ENTITY_HAS_PARTICLE_INTEGRATION=$<BOOL:$<TARGET_EXISTS:AYEntityParticleIntegration>>
    AY_ENTITY_HAS_ANIMATION_INTEGRATION=$<BOOL:${AY_ENABLE_ANIMATION}>
    AY_ENTITY_HAS_RENDER_INTEGRATION=$<BOOL:${AY_ENABLE_RENDERER}>
    AY_ENTITY_HAS_2D_COMPONENTS=$<BOOL:${AY_ENABLE_ENTITY_2D_SCHEMA}>
    AY_ENTITY_HAS_2D_INTEGRATION=$<AND:$<BOOL:${AY_ENABLE_RENDERER}>,$<BOOL:${AY_ENABLE_AY2D}>>
    AY_ENTITY_HAS_PHYSICS_INTEGRATION=$<BOOL:${AY_ENABLE_PHYSICS}>
    AY_ENTITY_HAS_SCRIPT_INTEGRATION=$<BOOL:${AY_ENABLE_SCRIPT}>
    AY_ENTITY_HAS_NETWORK_INTEGRATION=$<BOOL:${AY_ENABLE_NETWORK}>
)

# Identity and Scene authoring regressions also run without visual integrations.
if(BUILD_TESTING)
    if(TARGET AYEntityDeterminism)
        if(TARGET AYNetwork AND WIN32)
            add_executable(AYEntity_DeterministicNetworkWorker unittest/DeterministicNetworkWorker.cpp)
            target_link_libraries(AYEntity_DeterministicNetworkWorker PRIVATE AYEntityDeterminism AYNetwork)
            add_test(NAME AYEntity_DeterministicNetworkProcesses COMMAND powershell -NoProfile -ExecutionPolicy Bypass
                -File ${CMAKE_CURRENT_SOURCE_DIR}/unittest/RunDeterministicNetworkCheck.ps1
                -Worker $<TARGET_FILE:AYEntity_DeterministicNetworkWorker> -OutputDir ${CMAKE_CURRENT_BINARY_DIR}/network-lockstep-check)
            set_tests_properties(AYEntity_DeterministicNetworkProcesses PROPERTIES TIMEOUT 160 RUN_SERIAL TRUE LABELS "ayentity;determinism;network;process")
        endif()
        add_executable(AYEntity_DeterministicCollisionTests
            unittest/DeterministicCollisionTestMain.cpp unittest/AYTest_DeterministicCollision2D.cpp)
        target_link_libraries(AYEntity_DeterministicCollisionTests PRIVATE AYEntityDeterminism AYTest)
        ay_add_test(AYEntity_DeterministicCollisionTests LABELS "ayentity;determinism;collision;2d" TIMEOUT 120 RUN_SERIAL)
        add_executable(AYEntity_DeterministicSessionTests
            unittest/DeterministicSessionTestMain.cpp unittest/AYTest_DeterministicSession.cpp
            unittest/AYTest_DeterministicState.cpp unittest/AYTest_DeterministicLockstep.cpp)
        target_link_libraries(AYEntity_DeterministicSessionTests PRIVATE AYEntityDeterminism AYTest)
        ay_add_test(AYEntity_DeterministicSessionTests LABELS "ayentity;determinism;replay" TIMEOUT 120 RUN_SERIAL)
        add_executable(AYEntity_DeterministicSessionWorker unittest/DeterministicSessionWorker.cpp)
        target_link_libraries(AYEntity_DeterministicSessionWorker PRIVATE AYEntityDeterminism)
        add_test(NAME AYEntity_DeterministicSessionProcesses
            COMMAND ${CMAKE_COMMAND}
                -DWORKER=$<TARGET_FILE:AYEntity_DeterministicSessionWorker>
                -DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/session-process-check
                -P ${CMAKE_CURRENT_SOURCE_DIR}/unittest/RunDeterministicSessionProcessCheck.cmake)
        set_tests_properties(AYEntity_DeterministicSessionProcesses PROPERTIES
            LABELS "ayentity;determinism;replay;process" TIMEOUT 480 RUN_SERIAL TRUE)
    endif()
    add_executable(AYEntity_DeterministicHostTests
        unittest/DeterministicHostTestMain.cpp unittest/AYTest_DeterministicHost.cpp)
    target_link_libraries(AYEntity_DeterministicHostTests PRIVATE AYEntityCore AYTest)
    ay_add_test(AYEntity_DeterministicHostTests LABELS "ayentity;determinism;host" TIMEOUT 60 RUN_SERIAL)
    add_executable(AYEntity_DeterministicTests
        unittest/DeterministicTestMain.cpp
        unittest/AYTest_DeterministicLane.cpp unittest/AYTest_DetFloatSim.cpp)
    target_link_libraries(AYEntity_DeterministicTests PRIVATE AYEntityCore AYTest)
    ay_add_test(AYEntity_DeterministicTests LABELS "ayentity;determinism" TIMEOUT 60 RUN_SERIAL)
    add_executable(AYEntity_PerspectiveCameraTests
        unittest/PerspectiveCameraTestMain.cpp unittest/AYTest_PerspectiveCamera.cpp)
    target_link_libraries(AYEntity_PerspectiveCameraTests PRIVATE AYEntityCore AYTest)
    ay_add_test(AYEntity_PerspectiveCameraTests LABELS "ayentity;camera3d" TIMEOUT 60)
endif()

if(BUILD_TESTING AND AY_ENABLE_ENTITY_2D_SCHEMA)
    add_executable(AYEntity_IdentityTests
        unittest/TestMain.cpp
        unittest/ComponentRegistryTest.cpp
        unittest/AYTest_ActorClass.cpp
        unittest/AYTest_ComponentIdentity.cpp)
    target_link_libraries(AYEntity_IdentityTests PRIVATE AYEntity AYTest)
    ay_add_test(AYEntity_IdentityTests LABELS "ayentity;entity-identity" TIMEOUT 60 RUN_SERIAL)
endif()

if(BUILD_TESTING AND AY_ENABLE_AY2D AND AY_ENABLE_PHYSICS)
    enable_testing()
    add_subdirectory(unittest)
elseif(BUILD_TESTING)
    message(STATUS
        "AYEntity full integration tests skipped for this feature profile")
endif()

set(AYENTITY_PUBLIC_HEADER_DIR "${CMAKE_CURRENT_SOURCE_DIR}/include")
add_custom_target(ayentity_check_no_bgfx_in_public_headers
    COMMAND ${CMAKE_COMMAND}
        -DAYTGT_PUBLIC_HEADER_DIR="${AYENTITY_PUBLIC_HEADER_DIR}"
        -DAYTGT_MODULE_NAME="${PROJECT_NAME}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/CheckNoBgfxInPublicHeaders.cmake"
    COMMENT "AYEntity public-header bgfx/bx leak guard"
    VERBATIM
)
add_dependencies(AYEntityCore ayentity_check_no_bgfx_in_public_headers)
add_dependencies(AYEntity ayentity_check_no_bgfx_in_public_headers)

message(STATUS
    "AYEntity v0.2.0 (Core + explicit Animation/Render/2D/Physics/Script/Network integrations)")
