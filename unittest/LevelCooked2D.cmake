# design reference: AYEntity/design.md Stage45 bounded cooked level envelope.
if(NOT DEFINED _det_character_library)
    set(_det_character_library AYEntityDeterminism)
endif()
add_executable(AYEntity_LevelCooked2DChecks "${AYENTITY_ROOT}/unittest/portable/levelcooked2d.cpp")
target_link_libraries(AYEntity_LevelCooked2DChecks PRIVATE ${_det_character_library})
target_compile_features(AYEntity_LevelCooked2DChecks PRIVATE cxx_std_23)
if(MSVC)
    target_compile_options(AYEntity_LevelCooked2DChecks PRIVATE /utf-8)
endif()
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/level-cooked-artifacts")
add_test(NAME AYEntity_LevelCooked2DChecks COMMAND AYEntity_LevelCooked2DChecks
    --output "${CMAKE_CURRENT_BINARY_DIR}/level-cooked-artifacts"
    --legacy "${AYENTITY_ROOT}/examples/detlevel/terrain.dlt"
    --interaction "${AYENTITY_ROOT}/examples/detinteraction/interaction.dlt")
set_tests_properties(AYEntity_LevelCooked2DChecks PROPERTIES TIMEOUT 180 LABELS "ayentity;determinism;level;cooked;2d")
