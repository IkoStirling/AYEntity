# design reference: AYEntity/design.md Stage23; bounded seeded advanced-character evidence.
if(NOT DEFINED _det_character_library)
    set(_det_character_library AYEntityDeterminism)
endif()
add_executable(AYEntity_CharacterCampaignChecks "${AYENTITY_ROOT}/unittest/portable/charactercampaign.cpp")
target_link_libraries(AYEntity_CharacterCampaignChecks PRIVATE ${_det_character_library})
target_compile_features(AYEntity_CharacterCampaignChecks PRIVATE cxx_std_23)
if(MSVC)
    target_compile_options(AYEntity_CharacterCampaignChecks PRIVATE /utf-8)
endif()
set(_character_campaign_artifacts "${CMAKE_CURRENT_BINARY_DIR}/character-campaign-artifacts")
file(MAKE_DIRECTORY "${_character_campaign_artifacts}")
set(_character_campaign_timeout "$<IF:$<CONFIG:Debug>,900,300>")
set(_character_campaign_max_timeout "$<IF:$<CONFIG:Debug>,2400,1200>")
if(CMAKE_CXX_FLAGS MATCHES "[-/]fsanitize=address")
    set(_character_campaign_timeout 1200)
    set(_character_campaign_max_timeout 2400)
endif()
add_test(NAME AYEntity_CharacterCampaignChecks COMMAND AYEntity_CharacterCampaignChecks
    --seed 1 --cases 4 --ticks 256 --output "${_character_campaign_artifacts}")
add_test(NAME AYEntity_CharacterCampaignSeedEndpoints COMMAND AYEntity_CharacterCampaignChecks
    --seed 18446744073709551614 --cases 4 --ticks 192 --output "${_character_campaign_artifacts}")
add_test(NAME AYEntity_CharacterCampaignMaxTicks COMMAND AYEntity_CharacterCampaignChecks
    --seed 23 --cases 2 --ticks 1024 --output "${_character_campaign_artifacts}")
set_tests_properties(AYEntity_CharacterCampaignMaxTicks PROPERTIES
    TIMEOUT "${_character_campaign_max_timeout}" LABELS "ayentity;determinism;character;advanced;2d;campaign;rollback;replay")
set_tests_properties(AYEntity_CharacterCampaignChecks AYEntity_CharacterCampaignSeedEndpoints PROPERTIES
    TIMEOUT "${_character_campaign_timeout}" LABELS "ayentity;determinism;character;advanced;2d;campaign;rollback;replay")
option(AY_ENTITY_CHARACTER_LONG_BOUNDARIES "Run actual65535-tick character window boundary fixtures" OFF)
if(AY_ENTITY_CHARACTER_LONG_BOUNDARIES)
    add_test(NAME AYEntity_CharacterCampaignLongBoundaries COMMAND AYEntity_CharacterCampaignChecks
        --seed 1 --cases 1 --ticks 192 --long-boundaries --output "${_character_campaign_artifacts}")
    set_tests_properties(AYEntity_CharacterCampaignLongBoundaries PROPERTIES
        TIMEOUT 10800 LABELS "ayentity;determinism;character;advanced;2d;campaign;long")
endif()
