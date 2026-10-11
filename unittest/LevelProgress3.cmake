# design reference: AYEntity/design.md Stage42 complete .dlt progress authoring.
if(NOT DEFINED _det_character_library)
    set(_det_character_library AYEntityDeterminism)
endif()
add_executable(AYEntity_LevelProgress3Checks "${AYENTITY_ROOT}/unittest/portable/levelprogress3.cpp")
target_link_libraries(AYEntity_LevelProgress3Checks PRIVATE ${_det_character_library})
target_compile_features(AYEntity_LevelProgress3Checks PRIVATE cxx_std_23)
if(MSVC)
    target_compile_options(AYEntity_LevelProgress3Checks PRIVATE /utf-8)
endif()
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/level-progress3-artifacts")
add_test(NAME AYEntity_LevelProgress3Checks COMMAND AYEntity_LevelProgress3Checks
    --output "${CMAKE_CURRENT_BINARY_DIR}/level-progress3-artifacts"
    --legacy "${AYENTITY_ROOT}/examples/detlevel/terrain.dlt")
set_tests_properties(AYEntity_LevelProgress3Checks PROPERTIES TIMEOUT 180 LABELS "ayentity;determinism;level;progress;flow;2d")
