# Stage46 complete authored checkpoint/death/manual content through actual standard Host.
function(ay_entity_add_checkpoint_host_check)
    if(NOT BUILD_TESTING OR NOT TARGET AYEntity_DetCheckpointExample)
        return()
    endif()
    add_test(NAME AYEntity_CheckpointHostExample COMMAND ${CMAKE_COMMAND}
        -DEXAMPLE=$<TARGET_FILE:AYEntity_DetCheckpointExample>
        -DLEVEL=${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../examples/detcheckpoint/content.dlt
        -DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/checkpoint-host-example
        -P "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/RunCheckpointExample.cmake")
    set_tests_properties(AYEntity_CheckpointHostExample PROPERTIES TIMEOUT 120 RUN_SERIAL TRUE
        LABELS "ayentity;determinism;checkpoint;death;content;save;host;replay;example")
endfunction()
