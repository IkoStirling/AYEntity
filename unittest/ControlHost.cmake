# Stage56 finite enemy decisions and actual attack/death in standard Host.
function(ay_entity_add_control_host_check)
    if(NOT BUILD_TESTING OR NOT TARGET AYEntity_DetControlExample)
        return()
    endif()
    add_test(NAME AYEntity_ControlHostExample COMMAND ${CMAKE_COMMAND}
        -DEXAMPLE=$<TARGET_FILE:AYEntity_DetControlExample>
        -DLEVEL=${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../examples/detcontrol/content.dlt
        -DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/control-host-example
        -P "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/RunControlExample.cmake")
    set_tests_properties(AYEntity_ControlHostExample PROPERTIES TIMEOUT 120 RUN_SERIAL TRUE
        LABELS "ayentity;determinism;vitality;periodic;content;save;host;replay;example")
endfunction()
