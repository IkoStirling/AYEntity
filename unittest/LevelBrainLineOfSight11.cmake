# Stage59 finite authoring closure; query geometry/core replay belong to prior gates.
if(NOT DEFINED _det_character_library)
    set(_det_character_library AYEntityDeterminism)
endif()
add_executable(AYEntity_LevelBrainLineOfSight11Checks "${AYENTITY_ROOT}/unittest/portable/levelbrainlos11.cpp")
target_link_libraries(AYEntity_LevelBrainLineOfSight11Checks PRIVATE ${_det_character_library})
target_compile_features(AYEntity_LevelBrainLineOfSight11Checks PRIVATE cxx_std_23)
if(MSVC)
    target_compile_options(AYEntity_LevelBrainLineOfSight11Checks PRIVATE /utf-8)
endif()
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/level-brain-los11-artifacts")
add_test(NAME AYEntity_LevelBrainLineOfSight11Checks COMMAND AYEntity_LevelBrainLineOfSight11Checks
    --output "${CMAKE_CURRENT_BINARY_DIR}/level-brain-los11-artifacts"
    --legacy "${AYENTITY_ROOT}/examples/detcontrol/content.dlt")
set_tests_properties(AYEntity_LevelBrainLineOfSight11Checks PROPERTIES TIMEOUT 180 LABELS "ayentity;determinism;level;brain;query;2d")
