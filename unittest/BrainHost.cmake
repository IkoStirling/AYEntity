# Stage54 finite enemy decisions and actual attack/death in standard Host.
function(ay_entity_add_brain_host_check)
    if(NOT BUILD_TESTING OR NOT TARGET AYEntity_DetBrainExample)
        return()
    endif()
    add_test(NAME AYEntity_BrainHostExample COMMAND ${CMAKE_COMMAND}
        -DEXAMPLE=$<TARGET_FILE:AYEntity_DetBrainExample>
        -DLEVEL=${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../examples/detbrain/content.dlt
        -DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/brain-host-example
        -P "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/RunBrainExample.cmake")
    set_tests_properties(AYEntity_BrainHostExample PROPERTIES TIMEOUT 120 RUN_SERIAL TRUE
        LABELS "ayentity;determinism;vitality;periodic;content;save;host;replay;example")
endfunction()
