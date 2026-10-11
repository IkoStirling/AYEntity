# Stage52 authored attacks, jump miss and accepted recovery in standard Host.
function(ay_entity_add_attack_host_check)
    if(NOT BUILD_TESTING OR NOT TARGET AYEntity_DetAttackExample)
        return()
    endif()
    add_test(NAME AYEntity_AttackHostExample COMMAND ${CMAKE_COMMAND}
        -DEXAMPLE=$<TARGET_FILE:AYEntity_DetAttackExample>
        -DLEVEL=${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../examples/detattack/content.dlt
        -DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/attack-host-example
        -P "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/RunAttackExample.cmake")
    set_tests_properties(AYEntity_AttackHostExample PROPERTIES TIMEOUT 120 RUN_SERIAL TRUE
        LABELS "ayentity;determinism;vitality;periodic;content;save;host;replay;example")
endfunction()
