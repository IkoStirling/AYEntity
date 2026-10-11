# design reference: AYEntity/design.md Stage48 complete .dlt HP and recovery content.
if(NOT DEFINED _det_character_library)
    set(_det_character_library AYEntityDeterminism)
endif()
add_executable(AYEntity_LevelVitality5Checks "${AYENTITY_ROOT}/unittest/portable/levelvitality5.cpp")
target_link_libraries(AYEntity_LevelVitality5Checks PRIVATE ${_det_character_library})
target_compile_features(AYEntity_LevelVitality5Checks PRIVATE cxx_std_23)
if(MSVC)
    target_compile_options(AYEntity_LevelVitality5Checks PRIVATE /utf-8)
endif()
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/level-vitality5-artifacts")
add_test(NAME AYEntity_LevelVitality5Checks COMMAND AYEntity_LevelVitality5Checks
    --output "${CMAKE_CURRENT_BINARY_DIR}/level-vitality5-artifacts"
    --legacy "${AYENTITY_ROOT}/examples/detlevel/terrain.dlt")
set_tests_properties(AYEntity_LevelVitality5Checks PROPERTIES TIMEOUT 180 LABELS "ayentity;determinism;level;vitality;respawn;2d")
