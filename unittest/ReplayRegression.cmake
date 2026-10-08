if(NOT DEFINED _replay_regression_library)
    set(_replay_regression_library AYEntityDeterminism)
endif()
add_executable(AYEntity_ReplayRegressionChecks "${AYENTITY_ROOT}/unittest/portable/replayregression.cpp")
target_link_libraries(AYEntity_ReplayRegressionChecks PRIVATE ${_replay_regression_library})
foreach(_mode IN ITEMS Runner FaultRunner SlowRunner NoReportRunner)
    add_executable(AYEntity_ReplayRegression${_mode} "${AYENTITY_ROOT}/unittest/portable/replayrunner.cpp")
    target_link_libraries(AYEntity_ReplayRegression${_mode} PRIVATE ${_replay_regression_library})
endforeach()
target_compile_definitions(AYEntity_ReplayRegressionFaultRunner PRIVATE AY_REPLAY_TEST_FAULT=1)
target_compile_definitions(AYEntity_ReplayRegressionSlowRunner PRIVATE AY_REPLAY_TEST_SLEEP=1)
target_compile_definitions(AYEntity_ReplayRegressionNoReportRunner PRIVATE AY_REPLAY_TEST_NO_REPORT=1)
set_target_properties(AYEntity_ReplayRegressionRunner PROPERTIES OUTPUT_NAME "ay replay runner")
set(_regression_fixture "${AYENTITY_ROOT}/unittest/fixtures/replay-stage18/sample.rpi")
set(_regression_artifacts "${CMAKE_CURRENT_BINARY_DIR}/replay-regression-artifacts")
add_test(NAME AYEntity_ReplayRegressionChecks COMMAND AYEntity_ReplayRegressionChecks "${_regression_fixture}" "${_regression_artifacts}")
set_tests_properties(AYEntity_ReplayRegressionChecks PROPERTIES TIMEOUT 120)
add_test(NAME AYEntity_ReplayRegressionProcesses COMMAND ${CMAKE_COMMAND}
    -DTOOL=$<TARGET_FILE:AYEntity_ReplayArchiveTool>
    -DRUNNER=$<TARGET_FILE:AYEntity_ReplayRegressionRunner>
    -DFAULT=$<TARGET_FILE:AYEntity_ReplayRegressionFaultRunner>
    -DSLOW=$<TARGET_FILE:AYEntity_ReplayRegressionSlowRunner>
    -DNOREPORT=$<TARGET_FILE:AYEntity_ReplayRegressionNoReportRunner>
    -DFIXTURE=${_regression_fixture} -DOUTPUT=${_regression_artifacts}
    -P "${AYENTITY_ROOT}/unittest/RunReplayRegression.cmake")
set_tests_properties(AYEntity_ReplayRegressionProcesses PROPERTIES TIMEOUT 120)
