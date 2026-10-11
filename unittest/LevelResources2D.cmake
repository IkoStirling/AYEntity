# Optional Stage45 actual resource/import/project/cook integration; not portable-kernel linkage.
if(BUILD_TESTING AND TARGET AYEntityLevelResources AND TARGET AYProjectBuild AND TARGET AYResourceCook)
    add_executable(AYEntity_LevelResource2DChecks "${AYENTITY_ROOT}/unittest/LevelResource2DTests.cpp")
    target_link_libraries(AYEntity_LevelResource2DChecks PRIVATE
        AYEntityLevelResources AYProjectBuild AYResourceCook)
    target_compile_features(AYEntity_LevelResource2DChecks PRIVATE cxx_std_23)
    if(MSVC)
        target_compile_options(AYEntity_LevelResource2DChecks PRIVATE /utf-8)
    endif()
    set(_level_resource2d_artifacts "${CMAKE_CURRENT_BINARY_DIR}/level-resource2d-artifacts")
    file(MAKE_DIRECTORY "${_level_resource2d_artifacts}")
    add_test(NAME AYEntity_LevelResource2DChecks COMMAND AYEntity_LevelResource2DChecks
        --output "${_level_resource2d_artifacts}")
    set_tests_properties(AYEntity_LevelResource2DChecks PROPERTIES TIMEOUT 120 RUN_SERIAL TRUE
        LABELS "ayentity;determinism;level;resource;cook;pak;2d")
endif()
if(TARGET AYEntityLevelResources)
    add_executable(AYEntity_DetLevelResourceExample "${AYENTITY_ROOT}/examples/detlevel-resource/main.cpp")
    target_link_libraries(AYEntity_DetLevelResourceExample PRIVATE AYEntityLevelResources)
    target_compile_features(AYEntity_DetLevelResourceExample PRIVATE cxx_std_23)
    if(MSVC)
        target_compile_options(AYEntity_DetLevelResourceExample PRIVATE /utf-8)
    endif()
endif()
