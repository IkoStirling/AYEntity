# Stage50 moving trigger paths and independent pause/mask in standard Host.
function(ay_entity_add_moving_region_host_check)
    if(NOT BUILD_TESTING OR NOT TARGET AYEntity_DetMovingRegionExample)
        return()
    endif()
    add_test(NAME AYEntity_MovingRegionHostExample COMMAND ${CMAKE_COMMAND}
        -DEXAMPLE=$<TARGET_FILE:AYEntity_DetMovingRegionExample>
        -DLEVEL=${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../examples/detmovingregion/content.dlt
        -DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/moving-region-host-example
        -P "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/RunMovingRegionExample.cmake")
    set_tests_properties(AYEntity_MovingRegionHostExample PROPERTIES TIMEOUT 120 RUN_SERIAL TRUE
        LABELS "ayentity;determinism;vitality;periodic;content;save;host;replay;example")
endfunction()
