# design reference: AYEntity/design.md Stage50 bounded explicit moving-trigger content.
if(NOT DEFINED _det_character_library)
    set(_det_character_library AYEntityDeterminism)
endif()
add_executable(AYEntity_LevelMovingTrigger7Checks "${AYENTITY_ROOT}/unittest/portable/levelmovingtrigger7.cpp")
target_link_libraries(AYEntity_LevelMovingTrigger7Checks PRIVATE ${_det_character_library})
target_compile_features(AYEntity_LevelMovingTrigger7Checks PRIVATE cxx_std_23)
if(MSVC)
    target_compile_options(AYEntity_LevelMovingTrigger7Checks PRIVATE /utf-8)
endif()
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/level-moving-trigger7-artifacts")
add_test(NAME AYEntity_LevelMovingTrigger7Checks COMMAND AYEntity_LevelMovingTrigger7Checks
    --output "${CMAKE_CURRENT_BINARY_DIR}/level-moving-trigger7-artifacts"
    --legacy "${AYENTITY_ROOT}/unittest/fixtures/level-periodic6-canonical.dlt")
set_tests_properties(AYEntity_LevelMovingTrigger7Checks PROPERTIES TIMEOUT 180 LABELS "ayentity;determinism;level;path;trigger;vitality;2d")
