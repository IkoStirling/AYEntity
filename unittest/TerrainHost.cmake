# Stage32 real standard Host public level-file recipe; one bounded Live scene.
# The superproject owns target creation/linking and calls this after creating it.
function(ay_entity_add_terrain_host_check)
    if(NOT BUILD_TESTING OR NOT TARGET AYEntity_DetTerrainExample)
        return()
    endif()
    add_test(NAME AYEntity_TerrainHostExample COMMAND ${CMAKE_COMMAND}
        -DEXAMPLE=$<TARGET_FILE:AYEntity_DetTerrainExample>
        -DLEVEL=${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../examples/detlevel/terrain.dlt
        -DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/terrain-host-example
        -P "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/RunTerrainExample.cmake")
    set_tests_properties(AYEntity_TerrainHostExample PROPERTIES TIMEOUT 120 RUN_SERIAL TRUE
        LABELS "ayentity;determinism;terrain;level;2d;host;example")
endfunction()
