# Stage48 actual authored HP, external death and accepted recovery through standard Host.
function(ay_entity_add_vitality_host_check)
    if(NOT BUILD_TESTING OR NOT TARGET AYEntity_DetVitalityExample)
        return()
    endif()
    add_test(NAME AYEntity_VitalityHostExample COMMAND ${CMAKE_COMMAND}
        -DEXAMPLE=$<TARGET_FILE:AYEntity_DetVitalityExample>
        -DLEVEL=${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../examples/detvitality/content.dlt
        -DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/vitality-host-example
        -P "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/RunVitalityExample.cmake")
    set_tests_properties(AYEntity_VitalityHostExample PROPERTIES TIMEOUT 120 RUN_SERIAL TRUE
        LABELS "ayentity;determinism;vitality;death;content;save;host;replay;example")
endfunction()
