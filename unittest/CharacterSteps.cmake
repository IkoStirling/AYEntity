# design reference: AYEntity/design.md Stage26; bounded targeted static step-up checks.
if(NOT DEFINED _det_character_library)
    set(_det_character_library AYEntityDeterminism)
endif()
add_executable(AYEntity_CharacterStepsChecks "${AYENTITY_ROOT}/unittest/portable/charactersteps.cpp")
target_link_libraries(AYEntity_CharacterStepsChecks PRIVATE ${_det_character_library})
target_compile_features(AYEntity_CharacterStepsChecks PRIVATE cxx_std_23)
if(MSVC)
    target_compile_options(AYEntity_CharacterStepsChecks PRIVATE /utf-8)
endif()
set(_character_steps_artifacts "${CMAKE_CURRENT_BINARY_DIR}/character-steps-artifacts")
file(MAKE_DIRECTORY "${_character_steps_artifacts}")
add_test(NAME AYEntity_CharacterStepsChecks COMMAND AYEntity_CharacterStepsChecks --output "${_character_steps_artifacts}")
set_tests_properties(AYEntity_CharacterStepsChecks PROPERTIES TIMEOUT 120
    LABELS "ayentity;determinism;character;steps;2d;rollback;replay")

# The standard Host target is created later by the superproject integration.
function(ay_entity_add_character_steps_host_check)
    if(NOT BUILD_TESTING OR NOT TARGET AYEntity_DetCharacterExample)
        return()
    endif()
    add_test(NAME AYEntity_CharacterStepsHostExample COMMAND ${CMAKE_COMMAND}
        -DEXAMPLE=$<TARGET_FILE:AYEntity_DetCharacterExample>
        -DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/character-steps-host-example
        -P "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/RunCharacterStepsExample.cmake")
    set_tests_properties(AYEntity_CharacterStepsHostExample PROPERTIES TIMEOUT 120 RUN_SERIAL TRUE
        LABELS "ayentity;determinism;character;steps;2d;host;example")
endfunction()
