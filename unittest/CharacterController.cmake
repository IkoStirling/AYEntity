# design reference: AYEntity/design.md Stage21; registered-state movement, support and platform carry.
if(NOT DEFINED _det_character_library)
    set(_det_character_library AYEntityDeterminism)
endif()
add_executable(AYEntity_CharacterControllerChecks "${AYENTITY_ROOT}/unittest/portable/charactercontroller.cpp")
target_link_libraries(AYEntity_CharacterControllerChecks PRIVATE ${_det_character_library})
target_compile_features(AYEntity_CharacterControllerChecks PRIVATE cxx_std_23)
if(MSVC)
    target_compile_options(AYEntity_CharacterControllerChecks PRIVATE /utf-8)
endif()
set(_character_artifacts "${CMAKE_CURRENT_BINARY_DIR}/character-controller-artifacts")
file(MAKE_DIRECTORY "${_character_artifacts}")
add_test(NAME AYEntity_CharacterControllerChecks COMMAND AYEntity_CharacterControllerChecks --output "${_character_artifacts}")
# Preserve the same 2048-tick/full-replay workload under instrumentation.
# AddressSanitizer has a separate measured budget; ordinary native Release stays240s.
set(_character_timeout "$<IF:$<CONFIG:Debug>,600,240>")
if(CMAKE_CXX_FLAGS MATCHES "[-/]fsanitize=address")
    set(_character_timeout 900)
endif()
set_tests_properties(AYEntity_CharacterControllerChecks PROPERTIES
    TIMEOUT "${_character_timeout}" LABELS "ayentity;determinism;character;2d;rollback;replay")
