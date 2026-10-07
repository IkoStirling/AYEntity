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
