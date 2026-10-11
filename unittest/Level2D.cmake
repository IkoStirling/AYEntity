# Stage32 bounded level-resource loading, exact identity and real Session/replay use.
if(NOT DEFINED _det_character_library)
    set(_det_character_library AYEntityDeterminism)
endif()
add_executable(AYEntity_Level2DChecks "${AYENTITY_ROOT}/unittest/portable/level2d.cpp")
target_link_libraries(AYEntity_Level2DChecks PRIVATE ${_det_character_library})
target_compile_features(AYEntity_Level2DChecks PRIVATE cxx_std_23)
if(MSVC)
    target_compile_options(AYEntity_Level2DChecks PRIVATE /utf-8)
endif()
set(_level2d_artifacts "${CMAKE_CURRENT_BINARY_DIR}/level2d-artifacts")
file(MAKE_DIRECTORY "${_level2d_artifacts}")
add_test(NAME AYEntity_Level2DChecks COMMAND AYEntity_Level2DChecks --checks
    --level "${AYENTITY_ROOT}/examples/detlevel/terrain.dlt" --output "${_level2d_artifacts}")
set_tests_properties(AYEntity_Level2DChecks PROPERTIES TIMEOUT 120
    LABELS "ayentity;determinism;level;content;2d;replay")
