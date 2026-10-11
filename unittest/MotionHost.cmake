# design reference: AYEntity/design.md Stage27–29; one short real standard Host content integration.
function(ay_entity_add_motion_host_check)
    if(NOT BUILD_TESTING OR NOT TARGET AYEntity_DetCharacterExample)
        return()
    endif()
    add_test(NAME AYEntity_MotionHostExample COMMAND ${CMAKE_COMMAND}
        -DEXAMPLE=$<TARGET_FILE:AYEntity_DetCharacterExample>
        -DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/motion-host-example
        -P "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/RunMotionExample.cmake")
    set_tests_properties(AYEntity_MotionHostExample PROPERTIES TIMEOUT 120 RUN_SERIAL TRUE
        LABELS "ayentity;determinism;character;motion;slope;2d;host;example")
endfunction()
