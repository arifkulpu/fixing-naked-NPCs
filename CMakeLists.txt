cmake_minimum_required(VERSION 3.24)

project(NakedNPCFix VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find and include CommonLibSSE
find_package(CommonLibSSE REQUIRED)

add_library(${PROJECT_NAME} SHARED)

target_sources(${PROJECT_NAME} 
    PRIVATE 
        src/main.cpp
        src/VisibilityFixer.cpp
        src/PCH.h
)

target_link_libraries(${PROJECT_NAME} 
    PRIVATE 
        CommonLibSSE::CommonLibSSE
)

# SKSE Plugin information
target_compile_definitions(${PROJECT_NAME}
    PRIVATE
        SKSE_SUPPORT_AE
        PROJECT_NAME="${PROJECT_NAME}"
)

set_target_properties(${PROJECT_NAME} PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# Precompiled headers
# target_precompiled_headers(${PROJECT_NAME} PRIVATE src/PCH.h)

# PCH support
if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /MP /Zm200)
endif()
