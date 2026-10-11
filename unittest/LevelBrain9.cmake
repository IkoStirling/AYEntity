# Stage54 short authored content closure; core rollback/replay belongs to Brain2DChecks.
if(NOT DEFINED _det_character_library)
    set(_det_character_library AYEntityDeterminism)
endif()
add_executable(AYEntity_LevelBrain9Checks "${AYENTITY_ROOT}/unittest/portable/levelbrain9.cpp")
target_link_libraries(AYEntity_LevelBrain9Checks PRIVATE ${_det_character_library})
target_compile_features(AYEntity_LevelBrain9Checks PRIVATE cxx_std_23)
if(MSVC)
    target_compile_options(AYEntity_LevelBrain9Checks PRIVATE /utf-8)
endif()
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/level-brain9-artifacts")
add_test(NAME AYEntity_LevelBrain9Checks COMMAND AYEntity_LevelBrain9Checks
    --output "${CMAKE_CURRENT_BINARY_DIR}/level-brain9-artifacts"
    --legacy "${AYENTITY_ROOT}/examples/detattack/content.dlt")
set_tests_properties(AYEntity_LevelBrain9Checks PROPERTIES TIMEOUT 180 LABELS "ayentity;determinism;level;brain;attack;vitality;2d")
