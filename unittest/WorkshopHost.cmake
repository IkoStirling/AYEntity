# Stage66 one finite opt-in scene; reuse the existing default-OFF ControlHost executable.
function(ay_entity_add_workshop_host_check)
    if(NOT BUILD_TESTING OR NOT TARGET AYEntity_DetControlExample)
        return()
    endif()
    set(_workshop_level "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../examples/detcontrol/workshop.dlt")
    if(DEFINED AYENTITY_WORKSHOP_LEVEL)
        # Root may select the independently byte-matched fresh authoring output.
        set(_workshop_level "${AYENTITY_WORKSHOP_LEVEL}")
    endif()
    add_test(NAME AYEntity_WorkshopHostExample COMMAND ${CMAKE_COMMAND}
        -DEXAMPLE=$<TARGET_FILE:AYEntity_DetControlExample>
        -DLEVEL=${_workshop_level}
        -DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/workshop-host-example
        -P "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/RunWorkshopExample.cmake")
    set_tests_properties(AYEntity_WorkshopHostExample PROPERTIES TIMEOUT 90 RUN_SERIAL TRUE
        LABELS "ayentity;determinism;content;terrain;path;control;sight;attack;host;record;restore")
endfunction()
