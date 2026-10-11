function(ay_entity_add_interaction_host_check)
    if(NOT BUILD_TESTING OR NOT TARGET AYEntity_DetInteractionExample)
        return()
    endif()
    add_test(NAME AYEntity_InteractionHostExample COMMAND ${CMAKE_COMMAND}
        -DEXAMPLE=$<TARGET_FILE:AYEntity_DetInteractionExample>
        -DLEVEL=${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../examples/detinteraction/interaction.dlt
        -DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/interaction-host-example
        -P "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/RunInteractionExample.cmake")
    set_tests_properties(AYEntity_InteractionHostExample PROPERTIES TIMEOUT 120 RUN_SERIAL TRUE
        LABELS "ayentity;determinism;interaction;content;2d;host;replay;example")
endfunction()
