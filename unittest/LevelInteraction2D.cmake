# design reference: AYEntity/design.md Stage36 complete .dlt interaction content.
if(NOT DEFINED _det_character_library)
    set(_det_character_library AYEntityDeterminism)
endif()
add_executable(AYEntity_LevelInteraction2DChecks "${AYENTITY_ROOT}/unittest/portable/levelinteraction2d.cpp")
target_link_libraries(AYEntity_LevelInteraction2DChecks PRIVATE ${_det_character_library})
target_compile_features(AYEntity_LevelInteraction2DChecks PRIVATE cxx_std_23)
if(MSVC)
    target_compile_options(AYEntity_LevelInteraction2DChecks PRIVATE /utf-8)
endif()
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/level-interaction-artifacts")
add_test(NAME AYEntity_LevelInteraction2DChecks COMMAND AYEntity_LevelInteraction2DChecks
    --output "${CMAKE_CURRENT_BINARY_DIR}/level-interaction-artifacts"
    --legacy "${AYENTITY_ROOT}/examples/detlevel/terrain.dlt")
set_tests_properties(AYEntity_LevelInteraction2DChecks PROPERTIES TIMEOUT 180 LABELS "ayentity;determinism;level;interaction;2d")
