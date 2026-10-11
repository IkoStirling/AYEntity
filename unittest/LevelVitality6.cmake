# design reference: AYEntity/design.md Stage49 bounded periodic Level6 authoring.
if(NOT DEFINED _det_character_library)
    set(_det_character_library AYEntityDeterminism)
endif()
add_executable(AYEntity_LevelVitality6Checks "${AYENTITY_ROOT}/unittest/portable/levelvitality6.cpp")
target_link_libraries(AYEntity_LevelVitality6Checks PRIVATE ${_det_character_library})
target_compile_features(AYEntity_LevelVitality6Checks PRIVATE cxx_std_23)
if(MSVC)
    target_compile_options(AYEntity_LevelVitality6Checks PRIVATE /utf-8)
endif()
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/level-vitality6-artifacts")
add_test(NAME AYEntity_LevelVitality6Checks COMMAND AYEntity_LevelVitality6Checks
    --output "${CMAKE_CURRENT_BINARY_DIR}/level-vitality6-artifacts"
    --legacy "${AYENTITY_ROOT}/unittest/fixtures/level-vitality5-canonical.dlt")
set_tests_properties(AYEntity_LevelVitality6Checks PROPERTIES TIMEOUT 180 LABELS "ayentity;determinism;level;vitality;periodic;2d")
