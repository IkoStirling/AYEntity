# Stage59 actual action-controlled door and historical Brain2 sight observation.
function(ay_entity_add_sight_host_check)
    if(NOT BUILD_TESTING OR NOT TARGET AYEntity_DetSightExample)
        return()
    endif()
    add_test(NAME AYEntity_SightHostExample COMMAND ${CMAKE_COMMAND}
        -DEXAMPLE=$<TARGET_FILE:AYEntity_DetSightExample>
        -DLEVEL=${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../examples/detsight/content.dlt
        -DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/sight-host-example
        -P "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/RunSightExample.cmake")
    set_tests_properties(AYEntity_SightHostExample PROPERTIES TIMEOUT 120 RUN_SERIAL TRUE
        LABELS "ayentity;determinism;brain;query;actions;content;save;host;replay;example")
endfunction()
