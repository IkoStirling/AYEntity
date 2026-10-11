# design reference: AYEntity/design.md Stage52 bounded Attack1 authoring.
if(NOT DEFINED _det_character_library)
    set(_det_character_library AYEntityDeterminism)
endif()
add_executable(AYEntity_LevelAttack8Checks "${AYENTITY_ROOT}/unittest/portable/levelattack8.cpp")
target_link_libraries(AYEntity_LevelAttack8Checks PRIVATE ${_det_character_library})
target_compile_features(AYEntity_LevelAttack8Checks PRIVATE cxx_std_23)
if(MSVC)
    target_compile_options(AYEntity_LevelAttack8Checks PRIVATE /utf-8)
endif()
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/level-attack8-artifacts")
add_test(NAME AYEntity_LevelAttack8Checks COMMAND AYEntity_LevelAttack8Checks
    --output "${CMAKE_CURRENT_BINARY_DIR}/level-attack8-artifacts"
    --legacy "${AYENTITY_ROOT}/unittest/fixtures/level-periodic6-canonical.dlt")
set_tests_properties(AYEntity_LevelAttack8Checks PROPERTIES TIMEOUT 180 LABELS "ayentity;determinism;level;attack;vitality;2d")
