# design reference: AYEntity/design.md Stage39; complete local content lifecycle.
function(ay_entity_add_content_host_check)
    if(NOT BUILD_TESTING OR NOT TARGET AYEntity_DetContentExample)
        return()
    endif()
    add_test(NAME AYEntity_ContentHostExample COMMAND ${CMAKE_COMMAND}
        -DEXAMPLE=$<TARGET_FILE:AYEntity_DetContentExample>
        -DLEVEL=${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../examples/detcontent/content.dlt
        -DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/content-host-example
        -P "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/RunContentExample.cmake")
    set_tests_properties(AYEntity_ContentHostExample PROPERTIES TIMEOUT 120 RUN_SERIAL TRUE
        LABELS "ayentity;determinism;content;progress;save;host;replay;example")
endfunction()
