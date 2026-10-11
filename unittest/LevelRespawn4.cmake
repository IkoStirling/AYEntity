# design reference: AYEntity/design.md Stage46 complete .dlt respawn routing.
if(NOT DEFINED _det_character_library)
    set(_det_character_library AYEntityDeterminism)
endif()
add_executable(AYEntity_LevelRespawn4Checks "${AYENTITY_ROOT}/unittest/portable/levelrespawn4.cpp")
target_link_libraries(AYEntity_LevelRespawn4Checks PRIVATE ${_det_character_library})
target_compile_features(AYEntity_LevelRespawn4Checks PRIVATE cxx_std_23)
if(MSVC)
    target_compile_options(AYEntity_LevelRespawn4Checks PRIVATE /utf-8)
endif()
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/level-respawn4-artifacts")
add_test(NAME AYEntity_LevelRespawn4Checks COMMAND AYEntity_LevelRespawn4Checks
    --output "${CMAKE_CURRENT_BINARY_DIR}/level-respawn4-artifacts"
    --legacy "${AYENTITY_ROOT}/examples/detlevel/terrain.dlt")
set_tests_properties(AYEntity_LevelRespawn4Checks PROPERTIES TIMEOUT 180 LABELS "ayentity;determinism;level;respawn;flow;2d")
