# Stage49 real periodic regions and accepted recovery in standard Host.
function(ay_entity_add_periodic_host_check)
    if(NOT BUILD_TESTING OR NOT TARGET AYEntity_DetPeriodicExample)
        return()
    endif()
    add_test(NAME AYEntity_PeriodicHostExample COMMAND ${CMAKE_COMMAND}
        -DEXAMPLE=$<TARGET_FILE:AYEntity_DetPeriodicExample>
        -DLEVEL=${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../examples/detperiodic/content.dlt
        -DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/periodic-host-example
        -P "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/RunPeriodicExample.cmake")
    set_tests_properties(AYEntity_PeriodicHostExample PROPERTIES TIMEOUT 120 RUN_SERIAL TRUE
        LABELS "ayentity;determinism;vitality;periodic;content;save;host;replay;example")
endfunction()
