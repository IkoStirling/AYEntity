cmake_minimum_required(VERSION 3.20)
project(AYEntityTypedStateChecks LANGUAGES C CXX)
get_filename_component(AYENTITY_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
get_filename_component(AYENGINE_ROOT "${AYENTITY_ROOT}/../.." ABSOLUTE)
set(AYMATH_ROOT "${AYENGINE_ROOT}/AYFoundation/AYMath")
include("${AYMATH_ROOT}/cmake/SoftFloat.cmake")
aymath_add_softfloat(TypedStateBackend "${AYMATH_ROOT}")
add_executable(AYEntity_TypedStateChecks main.cpp "${AYENTITY_ROOT}/src/AYDeterministicState.cpp"
    "${AYMATH_ROOT}/src/DetFloat.cpp" "${AYMATH_ROOT}/src/DetMath.cpp"
    $<TARGET_OBJECTS:TypedStateBackend>)
target_compile_features(AYEntity_TypedStateChecks PRIVATE cxx_std_23)
target_include_directories(AYEntity_TypedStateChecks PRIVATE "${AYENTITY_ROOT}/include"
    "${AYMATH_ROOT}/include" "${AYMATH_ROOT}/third_party/softfloat/include")
find_package(Threads REQUIRED)
add_executable(AYEntity_StateAccessBenchmark benchmark.cpp "${AYENTITY_ROOT}/src/AYDeterministicState.cpp"
    "${AYMATH_ROOT}/src/DetFloat.cpp" "${AYMATH_ROOT}/src/DetMath.cpp" $<TARGET_OBJECTS:TypedStateBackend>)
target_compile_features(AYEntity_StateAccessBenchmark PRIVATE cxx_std_23)
target_include_directories(AYEntity_StateAccessBenchmark PRIVATE "${AYENTITY_ROOT}/include" "${AYMATH_ROOT}/include"
    "${AYMATH_ROOT}/third_party/softfloat/include")
target_link_libraries(AYEntity_StateAccessBenchmark PRIVATE Threads::Threads)
target_link_libraries(AYEntity_TypedStateChecks PRIVATE Threads::Threads)
option(AYSTATE_CHECK_FAST_MATH "Check under aggressive native floating optimization" ON)
if(MSVC)
    target_compile_options(AYEntity_TypedStateChecks PRIVATE /utf-8)
    if(AYSTATE_CHECK_FAST_MATH)
        target_compile_options(AYEntity_TypedStateChecks PRIVATE /fp:fast)
    endif()
elseif(AYSTATE_CHECK_FAST_MATH)
    target_compile_options(AYEntity_TypedStateChecks PRIVATE -ffast-math)
endif()
enable_testing()
add_executable(AYEntity_RollbackChecks rollback.cpp
    "${AYENTITY_ROOT}/src/AYDeterministicSession.cpp" "${AYENTITY_ROOT}/src/AYDeterministicSessionWire.cpp"
    "${AYENTITY_ROOT}/src/AYDeterministicState.cpp" "${AYENTITY_ROOT}/src/AYDeterministicCollision2D.cpp"
    "${AYENTITY_ROOT}/src/AYDeterministicRollback.cpp" "${AYENTITY_ROOT}/src/AYDeterministicRollbackNetwork.cpp"
    "${AYENTITY_ROOT}/src/AYDeterministicTransport.cpp" transport.cpp
    "${AYMATH_ROOT}/src/DetFloat.cpp" "${AYMATH_ROOT}/src/DetMath.cpp" $<TARGET_OBJECTS:TypedStateBackend>)
target_compile_features(AYEntity_RollbackChecks PRIVATE cxx_std_23)
target_include_directories(AYEntity_RollbackChecks PRIVATE "${AYENTITY_ROOT}/include" "${AYMATH_ROOT}/include"
    "${AYMATH_ROOT}/third_party/softfloat/include" "${AYENGINE_ROOT}/AYFoundation/AYReplay/include")
if(MSVC)
    target_compile_options(AYEntity_RollbackChecks PRIVATE /utf-8 /fp:fast)
else()
    target_compile_options(AYEntity_RollbackChecks PRIVATE -ffast-math -Wall -Wextra -Wpedantic)
endif()
add_test(NAME AYEntity_RollbackChecks COMMAND AYEntity_RollbackChecks)
# Debug retains the complete 100000+ advance workload with checked iterators.
# Its measured unoptimized run needs a separate budget; native Release stays 240s.
set_tests_properties(AYEntity_RollbackChecks PROPERTIES TIMEOUT "$<IF:$<CONFIG:Debug>,1500,240>")
add_executable(AYEntity_SessionCollisionChecks session.cpp
    "${AYENTITY_ROOT}/src/AYDeterministicSession.cpp" "${AYENTITY_ROOT}/src/AYDeterministicSessionWire.cpp"
    "${AYENTITY_ROOT}/src/AYDeterministicState.cpp" "${AYENTITY_ROOT}/src/AYDeterministicCollision2D.cpp"
    "${AYMATH_ROOT}/src/DetFloat.cpp" "${AYMATH_ROOT}/src/DetMath.cpp" $<TARGET_OBJECTS:TypedStateBackend>)
target_compile_features(AYEntity_SessionCollisionChecks PRIVATE cxx_std_23)
target_include_directories(AYEntity_SessionCollisionChecks PRIVATE "${AYENTITY_ROOT}/include"
    "${AYMATH_ROOT}/include" "${AYMATH_ROOT}/third_party/softfloat/include"
    "${AYENGINE_ROOT}/AYFoundation/AYReplay/include")
if(MSVC)
    target_compile_options(AYEntity_SessionCollisionChecks PRIVATE /utf-8 /fp:fast)
else()
    target_compile_options(AYEntity_SessionCollisionChecks PRIVATE -ffast-math)
endif()
add_test(NAME AYEntity_SessionCollisionChecks COMMAND AYEntity_SessionCollisionChecks)
set_tests_properties(AYEntity_SessionCollisionChecks PROPERTIES TIMEOUT 120)
add_executable(AYEntity_LockstepProtocolChecks lockstep.cpp "${AYENTITY_ROOT}/src/AYDeterministicLockstepProtocol.cpp")
target_compile_features(AYEntity_LockstepProtocolChecks PRIVATE cxx_std_23)
target_include_directories(AYEntity_LockstepProtocolChecks PRIVATE "${AYENTITY_ROOT}/include")
add_test(NAME AYEntity_LockstepProtocolChecks COMMAND AYEntity_LockstepProtocolChecks)
set_tests_properties(AYEntity_LockstepProtocolChecks PROPERTIES TIMEOUT 60)
add_test(NAME AYEntity_TypedStateChecks COMMAND AYEntity_TypedStateChecks)
set_tests_properties(AYEntity_TypedStateChecks PROPERTIES TIMEOUT 60)

# Actual production file adapters, not an in-memory substitute. IO sources are
# compiled from the exact pinned module revisions, with the system LZ4 library.
set(AYREPLAY_ROOT "${AYENGINE_ROOT}/AYFoundation/AYReplay")
set(AYIO_ROOT "${AYENGINE_ROOT}/AYFoundation/AYIO")
set(AYASSETFORMAT_ROOT "${AYENGINE_ROOT}/AYFoundation/AYAssetFormat")
find_path(REPLAY_LZ4_INCLUDE lz4.h REQUIRED)
find_library(REPLAY_LZ4_LIBRARY NAMES lz4 REQUIRED)
add_library(ReplayFileBackend STATIC "${AYREPLAY_ROOT}/src/FileReplayRecorder.cpp" "${AYREPLAY_ROOT}/src/FileReplayPlayer.cpp"
    "${AYIO_ROOT}/src/File.cpp" "${AYIO_ROOT}/src/Directory.cpp" "${AYIO_ROOT}/src/Path.cpp" "${AYIO_ROOT}/src/Env.cpp"
    "${AYASSETFORMAT_ROOT}/src/AssetFormat.cpp")
target_compile_features(ReplayFileBackend PUBLIC cxx_std_23)
target_include_directories(ReplayFileBackend PUBLIC "${AYREPLAY_ROOT}/include" "${AYIO_ROOT}/include"
    "${AYENGINE_ROOT}/AYFoundation/AYTime/include" "${AYENGINE_ROOT}/AYFoundation/AYTime"
    "${AYENGINE_ROOT}/AYFoundation/AYString/include" "${AYENGINE_ROOT}/AYFoundation/AYString"
    "${AYASSETFORMAT_ROOT}/include" "${REPLAY_LZ4_INCLUDE}" PRIVATE "${AYIO_ROOT}/include/AYIO")
target_link_libraries(ReplayFileBackend PUBLIC "${REPLAY_LZ4_LIBRARY}" Threads::Threads)
if(MSVC)
    target_compile_definitions(ReplayFileBackend PUBLIC NOMINMAX PRIVATE _CRT_SECURE_NO_WARNINGS)
    target_compile_options(ReplayFileBackend PUBLIC /utf-8)
endif()
add_library(ReplayArchiveKernel STATIC "${AYENTITY_ROOT}/src/AYDeterministicSession.cpp" "${AYENTITY_ROOT}/src/AYDeterministicSessionWire.cpp"
    "${AYENTITY_ROOT}/src/AYDeterministicState.cpp" "${AYENTITY_ROOT}/src/AYDeterministicCollision2D.cpp"
    "${AYENTITY_ROOT}/src/AYDeterministicCharacterController2D.cpp" "${AYENTITY_ROOT}/src/AYDeterministicReplay.cpp"
    "${AYENTITY_ROOT}/src/AYDeterministicRollback.cpp" "${AYENTITY_ROOT}/src/AYDeterministicRollbackNetwork.cpp"
    "${AYENTITY_ROOT}/src/AYDeterministicRollbackReplay.cpp" "${AYMATH_ROOT}/src/DetFloat.cpp" "${AYMATH_ROOT}/src/DetMath.cpp"
    $<TARGET_OBJECTS:TypedStateBackend>)
target_compile_features(ReplayArchiveKernel PUBLIC cxx_std_23)
target_include_directories(ReplayArchiveKernel PUBLIC "${AYENTITY_ROOT}/include" "${AYMATH_ROOT}/include" "${AYMATH_ROOT}/third_party/softfloat/include")
target_link_libraries(ReplayArchiveKernel PUBLIC ReplayFileBackend)
if(MSVC)
    target_compile_options(ReplayArchiveKernel PRIVATE /utf-8 /fp:fast)
else()
    target_compile_options(ReplayArchiveKernel PRIVATE -ffast-math)
endif()
add_executable(AYEntity_ReplayArchiveChecks replayarchive.cpp)
target_link_libraries(AYEntity_ReplayArchiveChecks PRIVATE ReplayArchiveKernel)
add_executable(AYEntity_ReplayArchiveTool "${AYENTITY_ROOT}/tools/detreplay/main.cpp")
set_target_properties(AYEntity_ReplayArchiveTool PROPERTIES OUTPUT_NAME ayreplay)
add_library(ReplayProcessBackend STATIC "${AYENGINE_ROOT}/AYFoundation/AYPlatform/src/Process.cpp")
target_compile_features(ReplayProcessBackend PUBLIC cxx_std_23)
target_include_directories(ReplayProcessBackend PUBLIC "${AYENGINE_ROOT}/AYFoundation/AYPlatform/include")
target_link_libraries(ReplayProcessBackend PUBLIC Threads::Threads)
if(MSVC)
    target_compile_options(ReplayProcessBackend PRIVATE /utf-8)
endif()
target_link_libraries(AYEntity_ReplayArchiveTool PRIVATE ReplayArchiveKernel ReplayProcessBackend)
add_executable(AYIO_FileErrorChecks "${AYIO_ROOT}/unittest/portable/main.cpp")
target_link_libraries(AYIO_FileErrorChecks PRIVATE ReplayFileBackend)
set(_archive_fixture "${AYENTITY_ROOT}/unittest/fixtures/replay-stage18")
set(_crash_marker "${CMAKE_CURRENT_BINARY_DIR}/crash-recording-path.txt")
add_test(NAME AYEntity_ReplayCrashWriter COMMAND AYEntity_ReplayArchiveChecks --interrupt "${_crash_marker}")
set_tests_properties(AYEntity_ReplayCrashWriter PROPERTIES FIXTURES_SETUP ArchiveCrash TIMEOUT 60)
add_test(NAME AYEntity_ReplayArchiveChecks COMMAND AYEntity_ReplayArchiveChecks --check "${_archive_fixture}" "${_crash_marker}")
set_tests_properties(AYEntity_ReplayArchiveChecks PROPERTIES FIXTURES_REQUIRED ArchiveCrash TIMEOUT 120)
add_test(NAME AYEntity_ReplayArchiveCLI COMMAND AYEntity_ReplayArchiveTool inspect "${_archive_fixture}/sample.rpi")
set_tests_properties(AYEntity_ReplayArchiveCLI PROPERTIES TIMEOUT 60)
add_test(NAME AYIO_FileErrorChecks COMMAND AYIO_FileErrorChecks)
set(_replay_regression_library ReplayArchiveKernel)
include("${AYENTITY_ROOT}/unittest/ReplayRegression.cmake")
set(_simulation_campaign_library ReplayArchiveKernel)
include("${AYENTITY_ROOT}/unittest/SimulationCampaign.cmake")

set(_det_character_library ReplayArchiveKernel)
include("${AYENTITY_ROOT}/unittest/CharacterController.cmake")
