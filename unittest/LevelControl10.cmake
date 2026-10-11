# Stage56 finite authored Control1 closure; core replay/late belongs to its sole core gate.
if(NOT DEFINED _det_character_library)
    set(_det_character_library AYEntityDeterminism)
endif()
add_executable(AYEntity_LevelCharacterControl10Checks "${AYENTITY_ROOT}/unittest/portable/levelcontrol10.cpp")
target_link_libraries(AYEntity_LevelCharacterControl10Checks PRIVATE ${_det_character_library})
target_compile_features(AYEntity_LevelCharacterControl10Checks PRIVATE cxx_std_23)
if(MSVC)
    target_compile_options(AYEntity_LevelCharacterControl10Checks PRIVATE /utf-8)
endif()
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/level-control10-artifacts")
add_test(NAME AYEntity_LevelCharacterControl10Checks COMMAND AYEntity_LevelCharacterControl10Checks
    --output "${CMAKE_CURRENT_BINARY_DIR}/level-control10-artifacts"
    --legacy "${AYENTITY_ROOT}/examples/detbrain/content.dlt")
set_tests_properties(AYEntity_LevelCharacterControl10Checks PROPERTIES TIMEOUT 180 LABELS "ayentity;determinism;level;control;character;2d")
