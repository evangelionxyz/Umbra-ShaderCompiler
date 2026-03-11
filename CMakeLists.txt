# Copyright (c) 2026 Evangelion Manuhutu

cmake_minimum_required(VERSION 3.20)

project(Umbra)
set(CMAKE_CXX_STANDARD 20)

file(GLOB_RECURSE SRC_FILES 
    Include/Umbra/*.h
    Source/Umbra/*.cpp
)

add_library(UmbraCompiler SHARED ${SRC_FILES})

target_include_directories(UmbraCompiler
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/Include>
        $<INSTALL_INTERFACE:include>
)

target_compile_definitions(UmbraCompiler PRIVATE UMBRACOMPILER_BUILD_SHARED)

# defines
if (WIN32)
    message("   >> Platform: Windows")
    
    # enable Hot Reload for MSVC compilers if supported.
    if (POLICY CMP0141)
        cmake_policy(SET CMP0141 NEW)
        set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT $<IF:$<AND:$<C_COMPILER_ID:MSVC>,$<CXX_COMPILER_ID:MSVC>>,$<$<CONFIG:Debug,RelWithDebInfo>:EditAndContinue>,$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>>)
    endif()

elseif (UNIX AND NOT APPLE)
    # check if we're running in WSL
    execute_process(COMMAND uname -r OUTPUT_VARIABLE WSL_KERNEL_NAME)
    string(FIND "${WSL_KERNEL_NAME}" "Microsoft" WSL_FOUND)
    
    if (WSL_FOUND EQUAL -1)
        message("   >> Platform: WSL")
    else()
        message("   >> Platform: LINUX")
    endif()
endif()

install(TARGETS UmbraCompiler
    EXPORT UmbraCompilerTargets
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)

install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/Source/
    DESTINATION include
    FILES_MATCHING PATTERN "*.h"
)

install(EXPORT UmbraCompilerTargets
    FILE UmbraCompiler.cmake
    NAMESPACE Umbra::
    DESTINATION lib/cmake/UmbraCompiler
)

option(UMBRACOMPILER_BUILD_EXAMPLES "Build UmbraCompiler examples" ON)
if (UMBRACOMPILER_BUILD_EXAMPLES)
    include(${CMAKE_CURRENT_SOURCE_DIR}/Example/c_example.cmake)
    include(${CMAKE_CURRENT_SOURCE_DIR}/Example/cpp_example.cmake)
endif()

# linking
if (WIN32)
    if (CMAKE_VS_PLATFORM_NAME STREQUAL "Win32")
        set(VULKAN_SDK_LIB_PATHS
            "$ENV{VULKAN_SDK}/Lib32"
            "$ENV{VULKAN_SDK}/Lib/Win32"
            "$ENV{VULKAN_SDK}/Lib"
            "$ENV{VULKAN_SDK}/Lib/x64"
            "$ENV{VULKAN_SDK}/Lib/arm64"
            "$ENV{VULKAN_SDK}/Lib/ARM64"
        )
    elseif (CMAKE_VS_PLATFORM_NAME STREQUAL "ARM" OR CMAKE_VS_PLATFORM_NAME STREQUAL "ARM64")
        set(VULKAN_SDK_LIB_PATHS
            "$ENV{VULKAN_SDK}/Lib/arm64"
            "$ENV{VULKAN_SDK}/Lib/ARM64"
            "$ENV{VULKAN_SDK}/Lib"
            "$ENV{VULKAN_SDK}/Lib/x64"
            "$ENV{VULKAN_SDK}/Lib32"
            "$ENV{VULKAN_SDK}/Lib/Win32"
        )
    else()
        set(VULKAN_SDK_LIB_PATHS
            "$ENV{VULKAN_SDK}/Lib"
            "$ENV{VULKAN_SDK}/Lib/x64"
            "$ENV{VULKAN_SDK}/Lib32"
            "$ENV{VULKAN_SDK}/Lib/Win32"
            "$ENV{VULKAN_SDK}/Lib/arm64"
            "$ENV{VULKAN_SDK}/Lib/ARM64"
        )
    endif()

    function(umbra_resolve_vulkan_lib outVar baseName)
        find_library(_${baseName}_RELEASE
            NAMES ${baseName}
            PATHS ${VULKAN_SDK_LIB_PATHS}
            NO_DEFAULT_PATH
        )

        find_library(_${baseName}_DEBUG
            NAMES ${baseName}d
            PATHS ${VULKAN_SDK_LIB_PATHS}
            NO_DEFAULT_PATH
        )

        if (NOT _${baseName}_RELEASE)
            message(FATAL_ERROR "${baseName}.lib not found in Vulkan SDK. Checked: ${VULKAN_SDK_LIB_PATHS}")
        endif()

        if (_${baseName}_DEBUG)
            set(${outVar} "$<$<CONFIG:Debug>:${_${baseName}_DEBUG}>$<$<NOT:$<CONFIG:Debug>>:${_${baseName}_RELEASE}>" PARENT_SCOPE)
        else()
            message(WARNING "${baseName}d.lib not found. Using ${baseName}.lib for Debug configuration.")
            set(${outVar} "${_${baseName}_RELEASE}" PARENT_SCOPE)
        endif()
    endfunction()

    find_library(DXCOMPILER_LIB
        NAMES dxcompiler
        PATHS ${VULKAN_SDK_LIB_PATHS}
        NO_DEFAULT_PATH
    )

    if (NOT DXCOMPILER_LIB)
        message(FATAL_ERROR "dxcompiler.lib not found. Please install DXC/Vulkan SDK and set VULKAN_SDK.")
    endif()

    umbra_resolve_vulkan_lib(SHADERC_SHARED_LIB shaderc_shared)
    umbra_resolve_vulkan_lib(SPIRV_CROSS_C_LIB spirv-cross-c)
    umbra_resolve_vulkan_lib(SPIRV_CROSS_CORE_LIB spirv-cross-core)
    umbra_resolve_vulkan_lib(SPIRV_CROSS_GLSL_LIB spirv-cross-glsl)
    umbra_resolve_vulkan_lib(SPIRV_CROSS_HLSL_LIB spirv-cross-hlsl)
    umbra_resolve_vulkan_lib(SPIRV_CROSS_MSL_LIB spirv-cross-msl)
    umbra_resolve_vulkan_lib(SPIRV_CROSS_CPP_LIB spirv-cross-cpp)
    umbra_resolve_vulkan_lib(SPIRV_CROSS_REFLECT_LIB spirv-cross-reflect)

    target_include_directories(UmbraCompiler PRIVATE "$ENV{VULKAN_SDK}/Include")
    target_link_libraries(UmbraCompiler PRIVATE
        ${DXCOMPILER_LIB}

        "d3d12.lib"
        "dxgi.lib"
        "d3dcompiler"
        "dxguid"

        ${SHADERC_SHARED_LIB}
        ${SPIRV_CROSS_C_LIB}
        ${SPIRV_CROSS_CORE_LIB}
        ${SPIRV_CROSS_GLSL_LIB}
        ${SPIRV_CROSS_HLSL_LIB}
        ${SPIRV_CROSS_MSL_LIB}
        ${SPIRV_CROSS_CPP_LIB}
        ${SPIRV_CROSS_REFLECT_LIB}
    )
elseif (UNIX AND NOT APPLE)
    target_include_directories(UmbraCompiler PRIVATE "/usr/include")
    target_link_directories(UmbraCompiler PRIVATE /usr/lib)

    target_link_libraries(UmbraCompiler PRIVATE
        vulkan
        shaderc_shared
        spirv-cross-c
        spirv-cross-core
        spirv-cross-glsl
        spirv-cross-hlsl
        spirv-cross-msl
        spirv-cross-cpp
        spirv-cross-reflect
        pthread dl m rt
    )
endif()
