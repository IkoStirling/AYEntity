# Stage69 one finite opt-in scene; reuse the existing default-OFF ControlHost executable.
function(ay_entity_add_challenge_host_check)
    if(NOT BUILD_TESTING OR NOT TARGET AYEntity_DetControlExample)
        return()
    endif()
    set(_challenge_level "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../examples/detcontrol/workshop-challenge.dlt")
    if(DEFINED AYENTITY_CHALLENGE_LEVEL)
        # Root may select the independently byte-matched fresh authoring output.
        set(_challenge_level "${AYENTITY_CHALLENGE_LEVEL}")
    endif()
    add_test(NAME AYEntity_ChallengeHostExample COMMAND ${CMAKE_COMMAND}
        -DEXAMPLE=$<TARGET_FILE:AYEntity_DetControlExample>
        -DLEVEL=${_challenge_level}
        -DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/challenge-host-example
        -P "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/RunChallengeExample.cmake")
    set_tests_properties(AYEntity_ChallengeHostExample PROPERTIES TIMEOUT 90 RUN_SERIAL TRUE
        LABELS "ayentity;determinism;content;terrain;path;control;sight;attack;host;record;restore")
endfunction()
