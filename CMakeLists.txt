cmake_minimum_required(VERSION 3.20)

project(AYEntity)

option(AY_ENTITY_PRECOMPILE_COMPONENTS "Precompile common components" ON)

set(SOURCES
    src/AYWorld.cpp
    src/AYEntity.cpp
    src/AYEntitySubSystem.cpp
    src/AYRenderSystem.cpp
    src/AYEntityReflection.cpp
    src/AYEntityModule.cpp
    src/AYResourceAnimationAdapter.cpp
    src/AYAnimationSystem.cpp
    src/AYSkinnedMeshRenderSystem.cpp
    src/AYCharacterEntity.cpp
)

add_library(${PROJECT_NAME} STATIC ${SOURCES} ${HEADERS})

target_include_directories(${PROJECT_NAME} PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/interface
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(${PROJECT_NAME} PUBLIC
    AYCore
    AYGameLoop
    AYReflect
    AYSerializer
    AYRenderer
    AYMath
    AYAnimation
    AYResource
)

if(AY_ENTITY_PRECOMPILE_COMPONENTS)
    target_compile_definitions(${PROJECT_NAME} PUBLIC AY_ENTITY_PRECOMPILE_COMPONENTS)
endif()

enable_testing()
add_subdirectory(unittest)