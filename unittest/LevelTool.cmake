# Real authoring commands against old and extended resources; no simulation campaign.
if(NOT TARGET AYEntity_DetLevelTool)
    get_filename_component(_det_tool_math_root "${AYENTITY_ROOT}/../../AYFoundation/AYMath" ABSOLUTE)
    add_executable(AYEntity_DetLevelTool "${AYENTITY_ROOT}/tools/detlevel/main.cpp"
        "${_det_tool_math_root}/src/DetFloatText.cpp")
    set_target_properties(AYEntity_DetLevelTool PROPERTIES OUTPUT_NAME aydetlevel)
    target_link_libraries(AYEntity_DetLevelTool PRIVATE ${_det_character_library})
    target_compile_features(AYEntity_DetLevelTool PRIVATE cxx_std_23)
endif()
add_test(NAME AYEntity_LevelToolChecks COMMAND "${CMAKE_COMMAND}"
    "-DTOOL=$<TARGET_FILE:AYEntity_DetLevelTool>"
    "-DLEVEL1=${AYENTITY_ROOT}/examples/detlevel/terrain.dlt"
    "-DLEVEL2=${AYENTITY_ROOT}/examples/detinteraction/interaction.dlt"
    "-DLEVEL3=${AYENTITY_ROOT}/examples/detcontent/content.dlt"
    "-DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/level-tool-artifacts"
    -P "${AYENTITY_ROOT}/unittest/RunLevelTool.cmake")
set_tests_properties(AYEntity_LevelToolChecks PROPERTIES TIMEOUT 60
    LABELS "ayentity;determinism;content;authoring;cli")
